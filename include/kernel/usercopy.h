#ifndef USERCOPY_H
#define USERCOPY_H

#include <stdint.h>
#include <stddef.h>

/*
 * LiwusOS User/Kernel boundary enforcement.
 *
 * The kernel image is linked in the higher half (0xffffffff80000000) and
 * loaded at 32 MiB. The direct map is in the upper canonical half. Until step
 * 9c removes the low identity alias, reject the physical 16 MiB–1 GiB window
 * from userspace. PTE_U checks remain authoritative for mapped pages.
 */

#define USER_ADDR_MAX   0x0000800000000000ULL /* exclusive, canonical user ceiling */
#define USER_ADDR_MIN   0x1000ULL             /* guard null page */
#define USER_STACK_TOP  0x00007ffffffff000ULL
#define USER_MMAP_TOP   0x00007f0000000000ULL
#define USER_PIE_BASE   0x0000555555554000ULL

#define EFAULT  14
#define E2BIG    7
#define ENOEXEC  8
#define EINVAL  22
#define ENOMEM  12
#define ENOENT   2
#define EACCES  13
#define EEXIST  17
#define ENOTDIR 20
#define ENOSPC  28
#define EPERM    1
#define EPIPE   32

/*
 * Check if a virtual address range lies entirely in user space and
 * every page in the range is mapped with PTE_U.
 * Returns 0 on valid, -EFAULT on invalid.
 */
int validate_user_pointer(const void *ptr, size_t size);

/* Check a complete user destination range for present, user, writable pages. */
int validate_user_write(const void *ptr, size_t size);

/*
 * Copy `n` bytes from user-space address `user_src` to kernel buffer `dst`.
 * Returns 0 on success, -EFAULT on invalid user pointer.
 */
int copy_from_user(void *dst, const void *user_src, size_t n);

/* Atomic aligned 32-bit user load for futex compare-and-block. */
int copy_from_user_u32_atomic(uint32_t *dst, const uint32_t *user_src);

/*
 * Copy `n` bytes from kernel buffer `src` to user-space address `user_dst`.
 * Returns 0 on success, -EFAULT on invalid user pointer.
 */
int copy_to_user(void *user_dst, const void *src, size_t n);

/*
 * Copy a null-terminated string from user space to kernel buffer.
 * Copies at most `maxlen` bytes (including null terminator).
 * Returns 0 on success, -EFAULT if the string exceeds maxlen or
 *         the pointer is invalid.
 */
int strncpy_from_user(char *dst, const char *user_src, size_t maxlen);

/* Clear AC immediately on entry to an interrupt/exception handler. */
void usercopy_clear_ac(void);

/*
 * Quick coarse check: must be below the user address ceiling. The fine
 * PTE_U verification is done by validate_user_pointer().
 * Returns 1 if plausible user address, 0 otherwise.
 */
static inline int is_user_addr(const void *ptr) {
    uint64_t addr = (uint64_t)ptr;
    return addr >= USER_ADDR_MIN && addr < USER_ADDR_MAX;
}

#endif
