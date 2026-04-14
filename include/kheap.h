#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>
#include <stdint.h>

/* Aloca n bytes no heap do kernel */
void *kmalloc(size_t size);
void kheap_set_start(uint32_t start);

/* Versão que garante alinhamento em página (4KB) */
void *kmalloc_a(size_t size);

/* Versão que retorna o endereço físico também */
/* Versão que retorna o endereço físico também */
void *kmalloc_ap(size_t size, uint32_t *phys);

/* Libera memória (Stub ou implementação futura) */
void kfree(void *ptr);

#endif
