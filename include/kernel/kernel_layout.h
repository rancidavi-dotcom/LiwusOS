#ifndef KERNEL_LAYOUT_H
#define KERNEL_LAYOUT_H

#include <stdint.h>

/* Physical addresses used by the boot stub and by the higher-half image.
 * Keep these in sync with src/boot/linker.ld and src/boot/boot.s. */
#define KERNEL_BOOT_PHYS_BASE 0x01000000ULL /* boot stub, VMA == LMA */
#define KERNEL_PHYS_BASE      0x02000000ULL /* higher-half image load address */
#define KERNEL_VIRT_BASE      0xffffffff80000000ULL

/* virt = phys + KERNEL_VIRT_OFFSET (0xffffffff7e000000) */
#define KERNEL_VIRT_OFFSET (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE)

/* Convert an address of a symbol inside the kernel image to its physical
 * address. Only valid for the image itself: the kernel heap and PMM still use
 * identity-mapped memory during step 9a. */
static inline uint64_t kernel_virt_to_phys(const void *virt) {
  return (uint64_t)virt - KERNEL_VIRT_OFFSET;
}

static inline void *kernel_phys_to_virt(uint64_t phys) {
  return (void *)(phys + KERNEL_VIRT_OFFSET);
}

#endif
