#include "pmm.h"

#define BLOCK_SIZE 4096
#define BLOCKS_PER_BYTE 8

static uint32_t* pmm_bitmap = 0;
static uint64_t  pmm_max_blocks = 0;
static uint64_t  pmm_used_blocks = 0;

static inline void bitmap_set(uint64_t bit) {
    pmm_bitmap[bit / 32] |= (1 << (bit % 32));
}

static inline void bitmap_unset(uint64_t bit) {
    pmm_bitmap[bit / 32] &= ~(1 << (bit % 32));
}

static inline bool bitmap_test(uint64_t bit) {
    return pmm_bitmap[bit / 32] & (1 << (bit % 32));
}

void pmm_init(uint64_t start_addr, uint64_t size) {
    pmm_bitmap = (uint32_t*)(uint64_t)start_addr;
    pmm_max_blocks = size / BLOCK_SIZE;
    pmm_used_blocks = pmm_max_blocks;

    for (uint64_t i = 0; i < pmm_max_blocks / 32; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }
}

void pmm_init_region(uint64_t base, uint64_t size) {
    uint64_t align = base / BLOCK_SIZE;
    uint64_t blocks = size / BLOCK_SIZE;

    for (; blocks > 0; blocks--) {
        bitmap_unset(align++);
        pmm_used_blocks--;
    }
}

void pmm_deinit_region(uint64_t base, uint64_t size) {
    uint64_t align = base / BLOCK_SIZE;
    uint64_t blocks = size / BLOCK_SIZE;

    for (; blocks > 0; blocks--) {
        bitmap_set(align++);
        pmm_used_blocks++;
    }
}

void* pmm_alloc_block() {
    for (uint64_t i = 0; i < pmm_max_blocks / 32; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t bit = 1 << j;
                if (!(pmm_bitmap[i] & bit)) {
                    uint64_t block = i * 32 + j;
                    bitmap_set(block);
                    pmm_used_blocks++;
                    return (void*)(uint64_t)(block * BLOCK_SIZE);
                }
            }
        }
    }
    return 0;
}

void pmm_free_block(void* addr) {
    uint64_t block = (uint64_t)addr / BLOCK_SIZE;
    bitmap_unset(block);
    pmm_used_blocks--;
}

uint64_t pmm_get_free_memory() {
    return (pmm_max_blocks - pmm_used_blocks) * BLOCK_SIZE;
}

uint64_t pmm_get_total_memory() {
    return pmm_max_blocks * BLOCK_SIZE;
}

uint64_t pmm_get_used_memory() {
    return pmm_used_blocks * BLOCK_SIZE;
}
