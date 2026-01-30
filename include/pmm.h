#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stdbool.h>

void pmm_init(uint32_t start_addr, uint32_t size);
void pmm_init_region(uint32_t base, uint32_t size);
void pmm_deinit_region(uint32_t base, uint32_t size);

void* pmm_alloc_block();
void pmm_free_block(void* addr);

uint32_t pmm_get_free_memory();
uint32_t pmm_get_total_memory();
uint32_t pmm_get_used_memory();

#endif
