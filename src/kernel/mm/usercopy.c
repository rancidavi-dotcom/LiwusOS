#include "usercopy.h"
#include "vmm.h"
#include "serial.h"
#include "string.h"

/*
 * Walk the 4-level page table for `vaddr` in the current page directory.
 * Returns the PTE if mapped, or NULL if any level is not present.
 * If `check_user` is set, also verifies the PTE_U bit at every level.
 */
static pte_t *walk_user_pt(uint64_t vaddr, int check_user) {
    pml4_t *pml4 = current_directory->pml4_virt;
    uint64_t pml4_i = vaddr_pml4i(vaddr);

    if (!(pml4->entries[pml4_i] & PTE_P)) return NULL;
    if (check_user && !(pml4->entries[pml4_i] & PTE_U)) return NULL;

    pdp_t *pdp = (pdp_t *)(pml4->entries[pml4_i] & ~0xFFFULL);
    uint64_t pdp_i = vaddr_pdpi(vaddr);

    if (!(pdp->entries[pdp_i] & PTE_P)) return NULL;
    if (check_user && !(pdp->entries[pdp_i] & PTE_U)) return NULL;

    pd_t *pd = (pd_t *)(pdp->entries[pdp_i] & ~0xFFFULL);
    uint64_t pd_i = vaddr_pdi(vaddr);

    if (pd->entries[pd_i] & PTE_PS) {
        /* 2MB large page — check present + user */
        if (!(pd->entries[pd_i] & PTE_P)) return NULL;
        if (check_user && !(pd->entries[pd_i] & PTE_U)) return NULL;
        return &pd->entries[pd_i];
    }

    if (!(pd->entries[pd_i] & PTE_P)) return NULL;
    if (check_user && !(pd->entries[pd_i] & PTE_U)) return NULL;

    pt_t *pt = (pt_t *)(pd->entries[pd_i] & ~0xFFFULL);
    uint64_t pt_i = vaddr_pti(vaddr);

    if (!(pt->entries[pt_i] & PTE_P)) return NULL;
    if (check_user && !(pt->entries[pt_i] & PTE_U)) return NULL;

    return &pt->entries[pt_i];
}

int validate_user_pointer(const void *ptr, size_t size) {
    if (size == 0) return 0;

    uint64_t start = (uint64_t)ptr;
    uint64_t end = start + size;

    /* Overflow check */
    if (end < start) return -EFAULT;

    /* Range check: must be above null guard and below user ceiling.
     * (Kernel pages in between are rejected by the PTE_U walk.) */
    if (start < USER_ADDR_MIN || end > USER_ADDR_MAX) return -EFAULT;

    /* Walk every page in the range */
    uint64_t page_start = start & ~(uint64_t)0xFFF;
    uint64_t page_end   = (end - 1) & ~(uint64_t)0xFFF;

    for (uint64_t page = page_start; page <= page_end; page += 0x1000) {
        if (!walk_user_pt(page, 1)) return -EFAULT;
    }

    return 0;
}

int copy_from_user(void *dst, const void *user_src, size_t n) {
    if (n == 0) return 0;
    if (validate_user_pointer(user_src, n) < 0) return -EFAULT;
    memcpy(dst, user_src, n);
    return 0;
}

int copy_to_user(void *user_dst, const void *src, size_t n) {
    if (n == 0) return 0;
    if (validate_user_pointer(user_dst, n) < 0) return -EFAULT;
    memcpy(user_dst, src, n);
    return 0;
}

int strncpy_from_user(char *dst, const char *user_src, size_t maxlen) {
    if (maxlen == 0) return -EINVAL;

    /* Validate at least the first byte */
    if (!is_user_addr(user_src)) return -EFAULT;

    /* Walk page by page to find the string length and validate */
    const char *src = user_src;
    size_t i = 0;

    for (i = 0; i < maxlen - 1; i++) {
        /* Check each page boundary */
        uint64_t addr = (uint64_t)(src + i);
        if (addr >= USER_ADDR_MAX) return -EFAULT;
        if ((addr & 0xFFF) == 0 || i == 0) {
            if (!walk_user_pt(addr, 1)) return -EFAULT;
        }
        char c = src[i];
        dst[i] = c;
        if (c == '\0') return 0;
    }
    dst[i] = '\0';

    /* Final page check for the last byte written */
    return 0;
}
