#include "elf.h"
#include "kheap.h"
#include "string.h"
#include "vmm.h"
#include "pmm.h"

int elf_class(void *file_buffer) {
  if (!file_buffer) return -1;
  uint8_t *ident = (uint8_t *)file_buffer;
  if (ident[0] != 0x7F || ident[1] != 'E' || ident[2] != 'L' || ident[3] != 'F')
    return -1;
  if (ident[EI_CLASS] != ELFCLASS32 && ident[EI_CLASS] != ELFCLASS64)
    return -1;
  return ident[EI_CLASS];
}

uint64_t elf32_load_file(void *file_buffer) {
  Elf32_Ehdr *hdr = (Elf32_Ehdr *)file_buffer;

  if (hdr->e_machine != EM_386) return 0;

  Elf32_Phdr *phdr = (Elf32_Phdr *)((uint64_t)file_buffer + hdr->e_phoff);

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      uint64_t vaddr = phdr[i].p_vaddr;
      uint64_t memsz = phdr[i].p_memsz;
      uint64_t filesz = phdr[i].p_filesz;
      uint64_t offset = phdr[i].p_offset;

      uint64_t start_page = vaddr & ~0xFFFULL;
      uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

      for (uint64_t p = start_page; p < end_page; p += 4096) {
        void *phys = pmm_alloc_block();
        vmm_map_page(phys, (void *)p, 0x7);
        memset((void *)p, 0, 4096);
      }

      memcpy((void *)vaddr, (uint8_t *)file_buffer + offset, (uint32_t)filesz);

      if (memsz > filesz) {
        memset((void *)(vaddr + filesz), 0, (size_t)(memsz - filesz));
      }
    }
  }

  return hdr->e_entry;
}

uint64_t elf64_load_file(void *file_buffer) {
  Elf64_Ehdr *hdr = (Elf64_Ehdr *)file_buffer;

  if (hdr->e_machine != EM_X86_64) return 0;

  Elf64_Phdr *phdr = (Elf64_Phdr *)((uint64_t)file_buffer + hdr->e_phoff);

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      uint64_t vaddr = phdr[i].p_vaddr;
      uint64_t memsz = phdr[i].p_memsz;
      uint64_t filesz = phdr[i].p_filesz;
      uint64_t offset = phdr[i].p_offset;

      uint64_t start_page = vaddr & ~0xFFFULL;
      uint64_t end_page = (vaddr + memsz + 0xFFF) & ~0xFFFULL;

      for (uint64_t p = start_page; p < end_page; p += 4096) {
        void *phys = pmm_alloc_block();
        vmm_map_page(phys, (void *)p, 0x7);
        memset((void *)p, 0, 4096);
      }

      memcpy((void *)vaddr, (uint8_t *)file_buffer + offset, (uint32_t)filesz);

      if (memsz > filesz) {
        memset((void *)(vaddr + filesz), 0, (size_t)(memsz - filesz));
      }
    }
  }

  return hdr->e_entry;
}
