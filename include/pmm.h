#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stdbool.h>

void pmm_init(uint64_t start_addr, uint64_t size);
void pmm_init_region(uint64_t base, uint64_t size);
void pmm_deinit_region(uint64_t base, uint64_t size);

void* pmm_alloc_block();
void pmm_free_block(void* addr);

uint64_t pmm_get_free_memory();
uint64_t pmm_get_total_memory();
uint64_t pmm_get_used_memory();

#endif
