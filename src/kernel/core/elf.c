#include "elf.h"
#include "kheap.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "usercopy.h"

uint64_t last_elf_heap_start = 0;
int last_elf_load_error = -8; /* -ENOEXEC */

int elf_class(void *file_buffer, uint32_t file_size) {
  if (!file_buffer || file_size < 16) return -1;
  uint8_t *ident = (uint8_t *)file_buffer;
  if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F')
    return -1;
  if (ident[EI_CLASS] != ELFCLASS32 && ident[EI_CLASS] != ELFCLASS64)
    return -1;
  return ident[EI_CLASS];
}

static int file_range_ok(uint32_t file_size, uint64_t offset, uint64_t size) {
  return offset <= file_size && size <= (uint64_t)file_size - offset;
}

uint64_t elf32_load_file(void *file_buffer, uint32_t file_size) {
  last_elf_load_error = -8;
  if (!file_buffer || file_size < sizeof(Elf32_Ehdr)) return 0;
  Elf32_Ehdr *hdr = (Elf32_Ehdr *)file_buffer;

  if (hdr->e_machine != EM_386 || hdr->e_phentsize != sizeof(Elf32_Phdr) ||
      !file_range_ok(file_size, hdr->e_phoff,
                     (uint64_t)hdr->e_phnum * hdr->e_phentsize)) return 0;

  Elf32_Phdr *phdr = (Elf32_Phdr *)((uint64_t)file_buffer + hdr->e_phoff);
  int entry_executable = 0;

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      uint64_t vaddr = phdr[i].p_vaddr;
      uint64_t memsz = phdr[i].p_memsz;
      uint64_t filesz = phdr[i].p_filesz;
      uint64_t offset = phdr[i].p_offset;

      if (memsz == 0 && filesz == 0) continue;

      if (memsz < filesz || vaddr + memsz < vaddr ||
          !file_range_ok(file_size, offset, filesz) ||
          vaddr < USER_ADDR_MIN || vaddr + memsz > USER_ADDR_MAX ||
          (vaddr < 0x40000000ULL && vaddr + memsz > 0x01000000ULL)) return 0;
      if ((phdr[i].p_flags & 1) && hdr->e_entry >= vaddr &&
          hdr->e_entry < vaddr + memsz) entry_executable = 1;

      uint64_t start_page = vaddr & ~0xFFFULL;
      if (vaddr + memsz > UINT64_MAX - 0xFFF) return 0;
      uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

      for (uint64_t p = start_page; p < end_page; p += 4096) {
        if (validate_user_pointer((void *)p, 1) < 0) {
          void *phys = pmm_alloc_block();
          if (!phys) { last_elf_load_error = -12; return 0; }
          /* Populate every segment while writable; final permissions are
           * applied after all PT_LOAD copies complete (CR0.WP is enabled). */
          uint64_t map_flags = PTE_P | PTE_U | PTE_W;
          vmm_map_page(phys, (void *)p, map_flags);
          if (validate_user_pointer((void *)p, 1) < 0) {
            pmm_free_block(phys);
            last_elf_load_error = -12;
            return 0;
          }
          memset((void *)p, 0, 4096);
        }
      }

      memcpy((void *)vaddr, (uint8_t *)file_buffer + offset, (uint32_t)filesz);

      if (memsz > filesz) {
        memset((void *)(vaddr + filesz), 0, (size_t)(memsz - filesz));
      }
    }
  }

  /* ELF permissions apply at page granularity. A page shared by segments is
   * writable if any PT_LOAD covering that page requires writes. */
  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) continue;
    uint64_t start = (uint64_t)phdr[i].p_vaddr & ~0xFFFULL;
    uint64_t end = ((uint64_t)phdr[i].p_vaddr + phdr[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
      int writable = 0;
      for (int j = 0; j < hdr->e_phnum; j++) {
        if (phdr[j].p_type != PT_LOAD || !(phdr[j].p_flags & 2) ||
            phdr[j].p_memsz == 0) continue;
        uint64_t other_start = (uint64_t)phdr[j].p_vaddr & ~0xFFFULL;
        uint64_t other_end = ((uint64_t)phdr[j].p_vaddr + phdr[j].p_memsz + 0xFFFULL) & ~0xFFFULL;
        if (page < other_end && page + PAGE_SIZE > other_start) {
          writable = 1;
          break;
        }
      }
      uint64_t flags = PTE_P | PTE_U | (writable ? PTE_W : 0);
      if (vmm_set_page_flags((void *)page, flags) < 0) {
        last_elf_load_error = -12;
        return 0;
      }
    }
  }

  if (!entry_executable || validate_user_pointer((void *)(uint64_t)hdr->e_entry, 1) < 0)
    return 0;
  last_elf_load_error = 0;
  return hdr->e_entry;
}

uint64_t elf64_load_file(void *file_buffer, uint32_t file_size) {
  last_elf_load_error = -8;
  if (!file_buffer || file_size < sizeof(Elf64_Ehdr)) return 0;
  Elf64_Ehdr *hdr = (Elf64_Ehdr *)file_buffer;

  if (hdr->e_machine != EM_X86_64 || hdr->e_phentsize != sizeof(Elf64_Phdr) ||
      !file_range_ok(file_size, hdr->e_phoff,
                     (uint64_t)hdr->e_phnum * hdr->e_phentsize)) return 0;

  Elf64_Phdr *phdr = (Elf64_Phdr *)((uint64_t)file_buffer + hdr->e_phoff);

  uint64_t max_end = 0;
  int entry_executable = 0;

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      uint64_t vaddr = phdr[i].p_vaddr;
      uint64_t memsz = phdr[i].p_memsz;
      uint64_t filesz = phdr[i].p_filesz;
      uint64_t offset = phdr[i].p_offset;

      if (memsz == 0 && filesz == 0) continue;

      if (memsz < filesz || vaddr + memsz < vaddr ||
          !file_range_ok(file_size, offset, filesz) ||
          vaddr < USER_ADDR_MIN || vaddr + memsz > USER_ADDR_MAX ||
          (vaddr < 0x40000000ULL && vaddr + memsz > 0x01000000ULL)) return 0;
      if ((phdr[i].p_flags & 1) && hdr->e_entry >= vaddr &&
          hdr->e_entry < vaddr + memsz) entry_executable = 1;

      uint64_t start_page = vaddr & ~0xFFFULL;
      if (vaddr + memsz > UINT64_MAX - 0xFFF) return 0;
      uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

      if (end_page > max_end) max_end = end_page;

      for (uint64_t p = start_page; p < end_page; p += 4096) {
        if (validate_user_pointer((void *)p, 1) < 0) {
          void *phys = pmm_alloc_block();
          if (!phys) { last_elf_load_error = -12; return 0; }
          uint64_t map_flags = PTE_P | PTE_U | PTE_W;
          vmm_map_page(phys, (void *)p, map_flags);
          if (validate_user_pointer((void *)p, 1) < 0) {
            pmm_free_block(phys);
            last_elf_load_error = -12;
            return 0;
          }
          memset((void *)p, 0, 4096);
        }
      }

      memcpy((void *)vaddr, (uint8_t *)file_buffer + offset, (uint32_t)filesz);

      if (memsz > filesz) {
        memset((void *)(vaddr + filesz), 0, (size_t)(memsz - filesz));
      }
    }
  }

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) continue;
    uint64_t start = (uint64_t)phdr[i].p_vaddr & ~0xFFFULL;
    uint64_t end = ((uint64_t)phdr[i].p_vaddr + phdr[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
      int writable = 0;
      for (int j = 0; j < hdr->e_phnum; j++) {
        if (phdr[j].p_type != PT_LOAD || !(phdr[j].p_flags & 2) ||
            phdr[j].p_memsz == 0) continue;
        uint64_t other_start = (uint64_t)phdr[j].p_vaddr & ~0xFFFULL;
        uint64_t other_end = ((uint64_t)phdr[j].p_vaddr + phdr[j].p_memsz + 0xFFFULL) & ~0xFFFULL;
        if (page < other_end && page + PAGE_SIZE > other_start) {
          writable = 1;
          break;
        }
      }
      uint64_t flags = PTE_P | PTE_U | (writable ? PTE_W : 0);
      if (vmm_set_page_flags((void *)page, flags) < 0) {
        last_elf_load_error = -12;
        return 0;
      }
    }
  }

  last_elf_heap_start = max_end;

  if (!entry_executable || validate_user_pointer((void *)hdr->e_entry, 1) < 0)
    return 0;
  last_elf_load_error = 0;
  return hdr->e_entry;
}
