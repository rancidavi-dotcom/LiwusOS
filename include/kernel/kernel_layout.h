#ifndef KERNEL_LAYOUT_H
#define KERNEL_LAYOUT_H

#include <stdint.h>

/* Physical addresses used by the boot stub and by the higher-half image.
 * Keep these in sync with src/boot/linker.ld and src/boot/boot.s. */
#define KERNEL_BOOT_PHYS_BASE 0x01000000ULL /* boot stub, VMA == LMA */
#define KERNEL_PHYS_BASE      0x02000000ULL /* higher-half image load address */
#define KERNEL_VIRT_BASE      0xffffffff80000000ULL
#define DIRECT_MAP_BASE       0xffff800000000000ULL
#define DIRECT_MAP_BOOT_SIZE 0x40000000ULL /* boot stub maps first 1 GiB */
#define DIRECT_MAP_SIZE      0x8000000000ULL /* PML4[256]: up to 512 GiB */

/* virt = phys + KERNEL_VIRT_OFFSET (0xffffffff7e000000) */
#define KERNEL_VIRT_OFFSET (KERNEL_VIRT_BASE - KERNEL_PHYS_BASE)

/* Convert RAM physical addresses to the dedicated direct-map alias. */
static inline void *phys_to_virt(uint64_t phys) {
  return (void *)(DIRECT_MAP_BASE + phys);
}

/* Convert a kernel pointer to its physical address. Kernel pointers may be in
 * the direct map, the linked kernel image, or the temporary identity alias. */
static inline uint64_t virt_to_phys(const void *virt) {
  uint64_t addr = (uint64_t)virt;
  if (addr >= DIRECT_MAP_BASE && addr < DIRECT_MAP_BASE + DIRECT_MAP_SIZE)
    return addr - DIRECT_MAP_BASE;
  if (addr >= KERNEL_VIRT_BASE)
    return addr - KERNEL_VIRT_OFFSET;
  return addr;
}

#endif
