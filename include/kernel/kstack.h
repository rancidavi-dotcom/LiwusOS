#ifndef KSTACK_H
#define KSTACK_H

#include <stddef.h>
#include <stdint.h>

#define KERNEL_STACK_MIN_SIZE (16U * 1024U)
#define KERNEL_STACK_DEFAULT_SIZE (128U * 1024U)
#define KERNEL_STACK_MAX_SIZE (512U * 1024U)
#define KERNEL_STACK_DESKTOP_SIZE (512U * 1024U)

/* Returns the low address of the mapped usable stack region. A guard page is
 * left unmapped on both sides. `size_out` receives the rounded usable size. */
void *kernel_stack_alloc(size_t requested_size, size_t *size_out);
void kernel_stack_free(void *usable_base, size_t usable_size);
int kernel_stack_is_guard(uint64_t address);

/* The bootstrap continuation stack becomes the permanent stack of task 0. */
void kernel_stack_adopt_boot(void *usable_base, size_t usable_size);
void *kernel_stack_boot_base(void);
size_t kernel_stack_boot_size(void);

#endif
