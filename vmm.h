#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>

/* Cada página tem 4KB */
#define PAGE_SIZE 4096

/* Inicializa a Memória Virtual */
void init_vmm();

/* Mapeia um endereço virtual para um físico */
void vmm_map_page(void* phys, void* virt, uint32_t flags);

#endif
