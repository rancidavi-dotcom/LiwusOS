#ifndef USERCOPY_H
#define USERCOPY_H

#include <stdint.h>
#include <stddef.h>

/*
 * LiwusOS User/Kernel boundary enforcement.
 *
 * The kernel does not use a high-half layout: user code lives around 4MB,
 * the stack at ~3GB, and the kernel itself at 16MB+. Therefore a fixed
 * address-range check is not sufficient. User pages are always mapped with
 * PTE_U, while kernel pages are not. Validation therefore checks that every
 * covered page is mapped and has PTE_U set.
 */

#define USER_ADDR_MAX  0xC0000000ULL /* top of the user stack space */
#define USER_ADDR_MIN  0x1000ULL       /* guard null page */

#define EFAULT  14
#define EINVAL  22
#define ENOMEM  12
#define ENOENT   2
#define EACCES  13
#define EEXIST  17
#define ENOTDIR 20
#define ENOSPC  28
#define EPERM    1

/*
 * Check if a virtual address range lies entirely in user space and
 * every page in the range is mapped with PTE_U.
 * Returns 0 on valid, -EFAULT on invalid.
 */
int validate_user_pointer(const void *ptr, size_t size);

/*
 * Copy `n` bytes from user-space address `user_src` to kernel buffer `dst`.
 * Returns 0 on success, -EFAULT on invalid user pointer.
 */
int copy_from_user(void *dst, const void *user_src, size_t n);

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

/*
 * Quick coarse check: must be below the user address ceiling. The fine
 * PTE_U verification is done by validate_user_pointer().
 * Returns 1 if plausible user address, 0 otherwise.
 */
static inline int is_user_addr(const void *ptr) {
    uint64_t addr = (uint64_t)ptr;
    return (addr >= USER_ADDR_MIN && addr < USER_ADDR_MAX);
}

#endif
