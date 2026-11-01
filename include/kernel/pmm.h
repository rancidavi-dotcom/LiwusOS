#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stdbool.h>

void pmm_init(uint64_t start_addr, uint64_t size);
/* Restricts allocations during bootstrap while retaining the full bitmap. */
void pmm_set_allocation_limit(uint64_t physical_limit);
void pmm_init_region(uint64_t base, uint64_t size);
void pmm_deinit_region(uint64_t base, uint64_t size);
/* Marks a physical range in use.  Kernel, modules and the bump heap must
 * never be returned by pmm_alloc_block(). */
void pmm_reserve_region(uint64_t base, uint64_t size);
int pmm_claim_contiguous_region(uint64_t hint, uint64_t desired_size,
                                uint64_t minimum_size, uint64_t *base_out,
                                uint64_t *size_out);

/* Allocations are returned as pointers in the direct map. Page-table entries
 * and device descriptors must store virt_to_phys() of the returned pointer. */
void* pmm_alloc_block();
/* Test/driver allocation constrained to physical frames at or above `min`. */
void* pmm_alloc_block_above(uint64_t min_phys);
/* Frees a pointer returned by pmm_alloc_block() or another direct-map alias. */
void pmm_free_block(void* addr);

uint64_t pmm_get_free_memory();
uint64_t pmm_get_total_memory();
uint64_t pmm_get_used_memory();

#endif
