#include "pmm.h"

/* Tamanho de cada bloco (página) de memória: 4KB */
#define BLOCK_SIZE 4096
#define BLOCKS_PER_BYTE 8

static uint32_t* pmm_bitmap = 0;
static uint32_t  pmm_max_blocks = 0;
static uint32_t  pmm_used_blocks = 0;

/* Funções internas para manipular o bitmap */
static inline void bitmap_set(uint32_t bit) {
    pmm_bitmap[bit / 32] |= (1 << (bit % 32));
}

static inline void bitmap_unset(uint32_t bit) {
    pmm_bitmap[bit / 32] &= ~(1 << (bit % 32));
}

static inline bool bitmap_test(uint32_t bit) {
    return pmm_bitmap[bit / 32] & (1 << (bit % 32));
}

void pmm_init(uint32_t start_addr, uint32_t size) {
    pmm_bitmap = (uint32_t*)start_addr;
    pmm_max_blocks = size / BLOCK_SIZE;
    pmm_used_blocks = pmm_max_blocks;

    /* Inicialmente, marca toda a memória como ocupada */
    for (uint32_t i = 0; i < pmm_max_blocks / 32; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }
}

void pmm_init_region(uint32_t base, uint32_t size) {
    uint32_t align = base / BLOCK_SIZE;
    uint32_t blocks = size / BLOCK_SIZE;

    for (; blocks > 0; blocks--) {
        bitmap_unset(align++);
        pmm_used_blocks--;
    }
}

void pmm_deinit_region(uint32_t base, uint32_t size) {
    uint32_t align = base / BLOCK_SIZE;
    uint32_t blocks = size / BLOCK_SIZE;

    for (; blocks > 0; blocks--) {
        bitmap_set(align++);
        pmm_used_blocks++;
    }
}

void* pmm_alloc_block() {
    for (uint32_t i = 0; i < pmm_max_blocks / 32; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t bit = 1 << j;
                if (!(pmm_bitmap[i] & bit)) {
                    uint32_t block = i * 32 + j;
                    bitmap_set(block);
                    pmm_used_blocks++;
                    return (void*)(block * BLOCK_SIZE);
                }
            }
        }
    }
    return 0; /* Out of memory */
}

void pmm_free_block(void* addr) {
    uint32_t block = (uint32_t)addr / BLOCK_SIZE;
    bitmap_unset(block);
    pmm_used_blocks--;
}

uint32_t pmm_get_free_memory() {
    return (pmm_max_blocks - pmm_used_blocks) * BLOCK_SIZE;
}

uint32_t pmm_get_total_memory() {
    return pmm_max_blocks * BLOCK_SIZE;
}

uint32_t pmm_get_used_memory() {
    return pmm_used_blocks * BLOCK_SIZE;
}
