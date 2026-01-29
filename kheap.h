#ifndef KHEAP_H
#define KHEAP_H

#include <stdint.h>
#include <stddef.h>

/* Aloca n bytes no heap do kernel */
void* kmalloc(size_t size);

/* Versão que garante alinhamento em página (4KB) */
void* kmalloc_a(size_t size);

/* Versão que retorna o endereço físico também */
void* kmalloc_ap(size_t size, uint32_t* phys);

#endif
