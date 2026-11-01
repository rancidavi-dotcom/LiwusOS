#include "pmm.h"
#include "kernel_layout.h"
#include "spinlock.h"

#define BLOCK_SIZE 4096
#define BLOCKS_PER_BYTE 8

static uint32_t* pmm_bitmap = 0;
static uint64_t  pmm_max_blocks = 0;
static uint64_t  pmm_used_blocks = 0;
static spinlock_t pmm_lock = {0};

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
    pmm_bitmap = (uint32_t *)phys_to_virt(start_addr);
    pmm_max_blocks = (size / BLOCK_SIZE) & ~31ULL;
    pmm_used_blocks = pmm_max_blocks;

    for (uint64_t i = 0; i < pmm_max_blocks / 32; i++) {
        pmm_bitmap[i] = 0xFFFFFFFF;
    }
}

void pmm_init_region(uint64_t base, uint64_t size) {
    spinlock_acquire(&pmm_lock);
    uint64_t end = base + size;
    if (end < base) end = UINT64_MAX;
    uint64_t align = (base + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint64_t last = end / BLOCK_SIZE;
    uint64_t blocks = last > align ? last - align : 0;

    for (; blocks > 0 && align < pmm_max_blocks; blocks--, align++) {
        if (bitmap_test(align)) {
            bitmap_unset(align);
            pmm_used_blocks--;
        }
    }
    spinlock_release(&pmm_lock);
}

void pmm_deinit_region(uint64_t base, uint64_t size) {
    spinlock_acquire(&pmm_lock);
    uint64_t end = base + size;
    if (end < base) end = UINT64_MAX;
    uint64_t align = (base + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint64_t last = end / BLOCK_SIZE;
    uint64_t blocks = last > align ? last - align : 0;

    for (; blocks > 0 && align < pmm_max_blocks; blocks--, align++) {
        if (!bitmap_test(align)) {
            bitmap_set(align);
            pmm_used_blocks++;
        }
    }
    spinlock_release(&pmm_lock);
}

void pmm_reserve_region(uint64_t base, uint64_t size) {
    /* Include partially covered pages: an allocation may start at any
     * byte, while the PMM tracks complete 4 KiB frames. */
    if (size == 0) return;
    uint64_t end = base + size;
    if (end < base) end = UINT64_MAX;
    uint64_t first = base / BLOCK_SIZE;
    uint64_t last = end / BLOCK_SIZE + ((end % BLOCK_SIZE) != 0);

    spinlock_acquire(&pmm_lock);
    for (uint64_t block = first; block < last && block < pmm_max_blocks;
         block++) {
        if (!bitmap_test(block)) {
            bitmap_set(block);
            pmm_used_blocks++;
        }
    }
    spinlock_release(&pmm_lock);
}

/* Claim a contiguous run made only of pages that the firmware memory map
 * marked available.  This keeps the physical bump heap out of MMIO holes and
 * reserved firmware ranges. */
int pmm_claim_contiguous_region(uint64_t hint, uint64_t desired_size,
                                uint64_t minimum_size, uint64_t *base_out,
                                uint64_t *size_out) {
    if (!base_out || !size_out || desired_size == 0 || minimum_size == 0 ||
        minimum_size > desired_size) return -1;
    uint64_t first = (hint + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint64_t desired = (desired_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint64_t minimum = (minimum_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint64_t best_start = 0, best_len = 0;

    spinlock_acquire(&pmm_lock);
    for (uint64_t i = first; i < pmm_max_blocks;) {
        while (i < pmm_max_blocks && bitmap_test(i)) i++;
        uint64_t start = i;
        while (i < pmm_max_blocks && !bitmap_test(i) && i - start < desired) i++;
        uint64_t len = i - start;
        if (len >= desired) {
            best_start = start;
            best_len = desired;
            break;
        }
        if (len > best_len) {
            best_start = start;
            best_len = len;
        }
    }
    if (best_len < minimum) {
        spinlock_release(&pmm_lock);
        return -1;
    }
    for (uint64_t i = 0; i < best_len; i++) {
        bitmap_set(best_start + i);
        pmm_used_blocks++;
    }
    spinlock_release(&pmm_lock);
    *base_out = best_start * BLOCK_SIZE;
    *size_out = best_len * BLOCK_SIZE;
    return 0;
}

void* pmm_alloc_block() {
    spinlock_acquire(&pmm_lock);
    for (uint64_t i = 0; i < pmm_max_blocks / 32; i++) {
        if (pmm_bitmap[i] != 0xFFFFFFFF) {
            for (uint32_t j = 0; j < 32; j++) {
                uint32_t bit = 1 << j;
                if (!(pmm_bitmap[i] & bit)) {
                    uint64_t block = i * 32 + j;
                    bitmap_set(block);
                    pmm_used_blocks++;
                    spinlock_release(&pmm_lock);
                    return phys_to_virt(block * BLOCK_SIZE);
                }
            }
        }
    }
    spinlock_release(&pmm_lock);
    return 0;
}

void pmm_free_block(void* addr) {
    spinlock_acquire(&pmm_lock);
    uint64_t block = virt_to_phys(addr) / BLOCK_SIZE;
    if (block < pmm_max_blocks && bitmap_test(block)) {
        bitmap_unset(block);
        if (pmm_used_blocks) pmm_used_blocks--;
    }
    spinlock_release(&pmm_lock);
}

uint64_t pmm_get_free_memory() {
    spinlock_acquire(&pmm_lock);
    uint64_t free_mem = (pmm_max_blocks - pmm_used_blocks) * BLOCK_SIZE;
    spinlock_release(&pmm_lock);
    return free_mem;
}

uint64_t pmm_get_total_memory() {
    return pmm_max_blocks * BLOCK_SIZE;
}

uint64_t pmm_get_used_memory() {
    spinlock_acquire(&pmm_lock);
    uint64_t used_mem = pmm_used_blocks * BLOCK_SIZE;
    spinlock_release(&pmm_lock);
    return used_mem;
}
