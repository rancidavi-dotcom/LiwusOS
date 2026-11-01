#include "elf.h"
#include "kheap.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"
#include "usercopy.h"
#include "vma.h"
#include "elf_loader.h"
#include "kernel_layout.h"

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

static int write_mm(page_directory_t *mm, uint64_t destination,
                    const void *source, size_t size) {
  const uint8_t *src = (const uint8_t *)source;
  while (size) {
    uint64_t phys, flags;
    if (vmm_get_mapping_in(mm, destination, &phys, &flags) < 0 ||
        !(flags & PTE_U) || !(flags & PTE_W)) return -1;
    size_t offset = (size_t)(destination & (PAGE_SIZE - 1));
    size_t chunk = PAGE_SIZE - offset;
    if (chunk > size) chunk = size;
    memcpy((uint8_t *)phys_to_virt(phys) + offset, src, chunk);
    destination += chunk;
    src += chunk;
    size -= chunk;
  }
  return 0;
}

static int zero_mm_range(page_directory_t *mm, uint64_t start, uint64_t size) {
  static const uint8_t zero_page[PAGE_SIZE] = {0};
  while (size) {
    size_t chunk = size > PAGE_SIZE ? PAGE_SIZE : (size_t)size;
    if (write_mm(mm, start, zero_page, chunk) < 0) return -1;
    start += chunk;
    size -= chunk;
  }
  return 0;
}

uint64_t elf32_load_file_in(page_directory_t *mm, void *file_buffer,
                            uint32_t file_size) {
  last_elf_load_error = -8;
  if (!mm || !file_buffer || file_size < sizeof(Elf32_Ehdr)) return 0;
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
          vaddr < USER_ADDR_MIN || vaddr + memsz > USER_ADDR_MAX) return 0;
      if ((phdr[i].p_flags & 1) && hdr->e_entry >= vaddr &&
          hdr->e_entry < vaddr + memsz) entry_executable = 1;

      uint64_t start_page = vaddr & ~0xFFFULL;
      if (vaddr + memsz > UINT64_MAX - 0xFFF) return 0;
      uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

      for (uint64_t p = start_page; p < end_page; p += 4096) {
        if (!vmm_page_present_in(mm, p)) {
          void *phys = pmm_alloc_block();
          if (!phys) { last_elf_load_error = -12; return 0; }
          memset(phys, 0, 4096);
          /* Populate every segment while writable; final permissions are
           * applied after all PT_LOAD copies complete (CR0.WP is enabled). */
          uint64_t map_flags = PTE_P | PTE_U | PTE_W | PTE_NX;
          if (vmm_map_page_in(mm, phys, (void *)p, map_flags) < 0) {
            pmm_free_block(phys);
            last_elf_load_error = -12;
            return 0;
          }
        }
      }

      if (write_mm(mm, vaddr, (uint8_t *)file_buffer + offset,
                   (size_t)filesz) < 0) {
        last_elf_load_error = -12;
        return 0;
      }

      if (memsz > filesz && zero_mm_range(mm, vaddr + filesz,
                                          memsz - filesz) < 0) {
        last_elf_load_error = -12;
        return 0;
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
      int executable = 0;
      for (int j = 0; j < hdr->e_phnum; j++) {
        if (phdr[j].p_type != PT_LOAD || phdr[j].p_memsz == 0) continue;
        uint64_t other_start = (uint64_t)phdr[j].p_vaddr & ~0xFFFULL;
        uint64_t other_end = ((uint64_t)phdr[j].p_vaddr + phdr[j].p_memsz + 0xFFFULL) & ~0xFFFULL;
        if (page < other_end && page + PAGE_SIZE > other_start) {
          if (phdr[j].p_flags & 2) writable = 1;
          if (phdr[j].p_flags & 1) executable = 1;
        }
      }
      if (writable && executable) {
        last_elf_load_error = -8;
        return 0;
      }
      uint64_t flags = PTE_P | PTE_U | (writable ? PTE_W : 0) |
                       (executable ? 0 : PTE_NX);
      if (vmm_set_page_flags_in(mm, (void *)page, flags) < 0) {
        last_elf_load_error = -12;
        return 0;
      }
    }
  }

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) continue;
    uint64_t start = (uint64_t)phdr[i].p_vaddr & ~0xFFFULL;
    uint64_t end = ((uint64_t)phdr[i].p_vaddr + phdr[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
    uint32_t prot = 0;
    if (phdr[i].p_flags & 4) prot |= VMA_PROT_READ;
    if (phdr[i].p_flags & 2) prot |= VMA_PROT_WRITE;
    if (phdr[i].p_flags & 1) prot |= VMA_PROT_EXEC;
    if (vma_insert(mm, start, end, prot,
                   VMA_PRIVATE | VMA_ANON | VMA_ELF, phdr[i].p_offset,
                   0) < 0) {
      last_elf_load_error = -12;
      return 0;
    }
  }

  uint64_t entry_flags = 0;
  if (!entry_executable ||
      vmm_get_mapping_in(mm, hdr->e_entry, 0, &entry_flags) < 0 ||
      !(entry_flags & PTE_U) || (entry_flags & PTE_NX))
    return 0;
  last_elf_load_error = 0;
  return hdr->e_entry;
}

uint64_t elf32_load_file(void *file_buffer, uint32_t file_size) {
  return elf32_load_file_in(current_directory, file_buffer, file_size);
}

uint64_t elf64_load_file_in(page_directory_t *mm, void *file_buffer,
                            uint32_t file_size) {
  last_elf_load_error = -8;
  if (!mm || !file_buffer || file_size < sizeof(Elf64_Ehdr)) return 0;
  Elf64_Ehdr *hdr = (Elf64_Ehdr *)file_buffer;

  if ((hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) ||
      hdr->e_machine != EM_X86_64 ||
      hdr->e_phentsize != sizeof(Elf64_Phdr) ||
      !file_range_ok(file_size, hdr->e_phoff,
                     (uint64_t)hdr->e_phnum * hdr->e_phentsize)) return 0;

  Elf64_Phdr *phdr = (Elf64_Phdr *)((uint64_t)file_buffer + hdr->e_phoff);

  uint64_t load_bias = 0;
  if (hdr->e_type == ET_DYN) {
    uint64_t min_vaddr = UINT64_MAX;
    for (int i = 0; i < hdr->e_phnum; i++) {
      if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) continue;
      uint64_t page = phdr[i].p_vaddr & ~0xFFFULL;
      if (page < min_vaddr) min_vaddr = page;
    }
    if (min_vaddr == UINT64_MAX || min_vaddr > USER_PIE_BASE) return 0;
    load_bias = USER_PIE_BASE - min_vaddr;
  }
  if (hdr->e_entry > UINT64_MAX - load_bias) return 0;
  uint64_t entry_addr = hdr->e_entry + load_bias;

  uint64_t max_end = 0;
  int entry_executable = 0;

  for (int i = 0; i < hdr->e_phnum; i++)
    if (phdr[i].p_type == PT_INTERP) return 0; /* dynamic linker is a later gate */

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      if (phdr[i].p_vaddr > UINT64_MAX - load_bias) return 0;
      uint64_t vaddr = phdr[i].p_vaddr + load_bias;
      uint64_t memsz = phdr[i].p_memsz;
      uint64_t filesz = phdr[i].p_filesz;
      uint64_t offset = phdr[i].p_offset;

      if (memsz == 0 && filesz == 0) continue;

      if (memsz < filesz || vaddr + memsz < vaddr ||
          !file_range_ok(file_size, offset, filesz) ||
          vaddr < USER_ADDR_MIN || vaddr + memsz > USER_ADDR_MAX) return 0;
      if ((phdr[i].p_flags & 1) && entry_addr >= vaddr &&
          entry_addr < vaddr + memsz) entry_executable = 1;

      uint64_t start_page = vaddr & ~0xFFFULL;
      if (vaddr + memsz > UINT64_MAX - 0xFFF) return 0;
      uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

      if (end_page > max_end) max_end = end_page;

      for (uint64_t p = start_page; p < end_page; p += 4096) {
        if (!vmm_page_present_in(mm, p)) {
          void *phys = pmm_alloc_block();
          if (!phys) { last_elf_load_error = -12; return 0; }
          memset(phys, 0, 4096);
          uint64_t map_flags = PTE_P | PTE_U | PTE_W | PTE_NX;
          if (vmm_map_page_in(mm, phys, (void *)p, map_flags) < 0) {
            pmm_free_block(phys);
            last_elf_load_error = -12;
            return 0;
          }
        }
      }

      if (write_mm(mm, vaddr, (uint8_t *)file_buffer + offset,
                   (size_t)filesz) < 0) {
        last_elf_load_error = -12;
        return 0;
      }

      if (memsz > filesz && zero_mm_range(mm, vaddr + filesz,
                                          memsz - filesz) < 0) {
        last_elf_load_error = -12;
        return 0;
      }
    }
  }

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) continue;
    uint64_t seg_start = (uint64_t)phdr[i].p_vaddr + load_bias;
    uint64_t start = seg_start & ~0xFFFULL;
    uint64_t end = (seg_start + phdr[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
    for (uint64_t page = start; page < end; page += PAGE_SIZE) {
      int writable = 0;
      int executable = 0;
      for (int j = 0; j < hdr->e_phnum; j++) {
        if (phdr[j].p_type != PT_LOAD || phdr[j].p_memsz == 0) continue;
        uint64_t other_vaddr = (uint64_t)phdr[j].p_vaddr + load_bias;
        uint64_t other_start = other_vaddr & ~0xFFFULL;
        uint64_t other_end = (other_vaddr + phdr[j].p_memsz + 0xFFFULL) & ~0xFFFULL;
        if (page < other_end && page + PAGE_SIZE > other_start) {
          if (phdr[j].p_flags & 2) writable = 1;
          if (phdr[j].p_flags & 1) executable = 1;
        }
      }
      if (writable && executable) {
        last_elf_load_error = -8;
        return 0;
      }
      uint64_t flags = PTE_P | PTE_U | (writable ? PTE_W : 0) |
                       (executable ? 0 : PTE_NX);
      if (vmm_set_page_flags_in(mm, (void *)page, flags) < 0) {
        last_elf_load_error = -12;
        return 0;
      }
    }
  }

  last_elf_heap_start = max_end;

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type != PT_LOAD || phdr[i].p_memsz == 0) continue;
    uint64_t seg_start = (uint64_t)phdr[i].p_vaddr + load_bias;
    uint64_t start = seg_start & ~0xFFFULL;
    uint64_t end = (seg_start + phdr[i].p_memsz + 0xFFFULL) & ~0xFFFULL;
    uint32_t prot = 0;
    if (phdr[i].p_flags & 4) prot |= VMA_PROT_READ;
    if (phdr[i].p_flags & 2) prot |= VMA_PROT_WRITE;
    if (phdr[i].p_flags & 1) prot |= VMA_PROT_EXEC;
    if (vma_insert(mm, start, end, prot,
                   VMA_PRIVATE | VMA_ANON | VMA_ELF, phdr[i].p_offset,
                   0) < 0) {
      last_elf_load_error = -12;
      return 0;
    }
  }

  uint64_t entry_flags = 0;
  if (!entry_executable ||
      vmm_get_mapping_in(mm, entry_addr, 0, &entry_flags) < 0 ||
      !(entry_flags & PTE_U) || (entry_flags & PTE_NX))
    return 0;
  last_elf_load_error = 0;
  return entry_addr;
}

uint64_t elf64_load_file(void *file_buffer, uint32_t file_size) {
  return elf64_load_file_in(current_directory, file_buffer, file_size);
}
