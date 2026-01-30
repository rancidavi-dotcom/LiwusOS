#include "elf.h"
#include "initrd.h" // Para initrd_get_file se precisar, mas recebemos void* buffer
#include "kheap.h"
#include "string.h"
#include "vmm.h"

// Helper externo para mapear (assumido vmm.h/c)
// void vmm_map_page(void* phys, void* virt, uint32_t flags);
// extern page_directory_t* current_directory;

uint32_t elf_load_file(void *file_buffer) {
  if (!file_buffer)
    return 0;

  Elf32_Ehdr *hdr = (Elf32_Ehdr *)file_buffer;

  // 1. Check Magic
  // Verifica \x7F ELF
  if (hdr->e_ident[0] != 0x7F || hdr->e_ident[1] != 'E' ||
      hdr->e_ident[2] != 'L' || hdr->e_ident[3] != 'F') {
    return 0; // Not ELF
  }

  // 2. Load Segments
  Elf32_Phdr *phdr = (Elf32_Phdr *)((uint32_t)file_buffer + hdr->e_phoff);

  for (int i = 0; i < hdr->e_phnum; i++) {
    if (phdr[i].p_type == PT_LOAD) {
      uint32_t vaddr = phdr[i].p_vaddr;
      uint32_t memsz = phdr[i].p_memsz;
      uint32_t filesz = phdr[i].p_filesz;
      uint32_t offset = phdr[i].p_offset;

      // Alinhamento de páginas
      uint32_t start_page = vaddr & 0xFFFFF000;
      uint32_t end_page = (vaddr + memsz + 0xFFF) & 0xFFFFF000;

      // Mapear páginas necessárias
      for (uint32_t p = start_page; p < end_page; p += 4096) {
        // TODO: Verificar se já mapeado?
        // Assumindo que o espaço de usuário está livre ou queremos overwrite.
        // Aloca novo frame físico
        void *phys = kmalloc_a(4096);
        vmm_map_page((void *)((uint32_t)phys), (void *)p,
                     0x7); // User RW Present
        memset((void *)p, 0, 4096);
      }

      // Copiar dados do arquivo para a memória virtual
      // Como mapeamos agora, podemos escrever direto em vaddr
      memcpy((void *)vaddr, (uint8_t *)file_buffer + offset, filesz);

      // BSS já foi zerado pelo memset da página, mas para garantir precisão
      // byte-a-byte se filesz < memsz no meio da página:
      if (memsz > filesz) {
        memset((void *)(vaddr + filesz), 0, memsz - filesz);
      }
    }
  }

  return hdr->e_entry;
}
