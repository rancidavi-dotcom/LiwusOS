#ifndef VMM_H
#define VMM_H

#include <stdbool.h>
#include <stdint.h>

/* Cada página tem 4KB */
/* Cada página tem 4KB */
#define PAGE_SIZE 4096

typedef struct {
  uint32_t tablesPhysical[1024];
  uint32_t *tablesVirtual[1024]; // array of pointers to pagetables
  uint32_t physicalAddr;         // physical address of tablesPhysical
} page_directory_t;

/* Inicializa a Memória Virtual */
void init_vmm();

/* Alterna diretório de páginas */
void switch_page_directory(page_directory_t *dir);

/* Cria um novo diretório (clonando kernel space) */
page_directory_t *vmm_create_directory();

/* Mapeia um endereço virtual para um físico */
void vmm_map_page(void *phys, void *virt, uint32_t flags);

/* Clona um diretório de páginas (para fork) */
page_directory_t *vmm_copy_directory(page_directory_t *src);

/* Gerenciamento de Heap (brk) */
uint32_t sys_brk(uint32_t addr);

#endif
