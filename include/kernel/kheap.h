#ifndef KHEAP_H
#define KHEAP_H

#include <stddef.h>
#include <stdint.h>

void *kmalloc(size_t size);
void kheap_set_start(uint64_t start);

void *kmalloc_a(size_t size);

void *kmalloc_ap(size_t size, uint64_t *phys);

void *krealloc(void *ptr, size_t new_size);

void kfree(void *ptr);

#endif
