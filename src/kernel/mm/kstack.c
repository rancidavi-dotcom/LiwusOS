#include "kstack.h"
#include "kernel_layout.h"
#include "pmm.h"
#include "spinlock.h"
#include "string.h"
#include "vmm.h"

#define KSTACK_SLOT_COUNT 1024U
#define KSTACK_MAX_PAGES (KERNEL_STACK_MAX_SIZE / PAGE_SIZE)
#define KSTACK_SLOT_PAGES (KSTACK_MAX_PAGES + 2U)
#define KSTACK_REGION_BASE 0xffffc00000000000ULL

static volatile uint8_t slots[KSTACK_SLOT_COUNT];
static spinlock_t stack_lock = {0};
static void *boot_stack_base;
static size_t boot_stack_size;

static uint64_t stack_lock_irqsave(void) {
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
  spinlock_acquire(&stack_lock);
  return flags;
}

static void stack_unlock_irqrestore(uint64_t flags) {
  spinlock_release(&stack_lock);
  if (flags & (1ULL << 9)) asm volatile("sti" ::: "memory");
}

static uint64_t slot_base(unsigned slot) {
  return KSTACK_REGION_BASE +
         (uint64_t)slot * KSTACK_SLOT_PAGES * PAGE_SIZE;
}

void *kernel_stack_alloc(size_t requested_size, size_t *size_out) {
  if (requested_size < KERNEL_STACK_MIN_SIZE)
    requested_size = KERNEL_STACK_MIN_SIZE;
  if (requested_size > KERNEL_STACK_MAX_SIZE) return 0;
  size_t rounded = (requested_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);
  unsigned pages = (unsigned)(rounded / PAGE_SIZE);

  uint64_t irq_flags = stack_lock_irqsave();
  unsigned slot;
  for (slot = 0; slot < KSTACK_SLOT_COUNT && slots[slot]; slot++) { }
  if (slot == KSTACK_SLOT_COUNT) {
    stack_unlock_irqrestore(irq_flags);
    return 0;
  }
  uint64_t guard_base = slot_base(slot);
  uint64_t base = guard_base + PAGE_SIZE;
  unsigned mapped = 0;
  for (; mapped < pages; mapped++) {
    void *frame = pmm_alloc_block();
    if (!frame) break;
    memset(frame, 0, PAGE_SIZE);
    if (vmm_map_kernel_page(virt_to_phys(frame), base +
                            (uint64_t)mapped * PAGE_SIZE,
                            PTE_P | PTE_W | PTE_NX) < 0) {
      pmm_free_block(frame);
      break;
    }
  }
  if (mapped != pages) {
    while (mapped) {
      uint64_t phys = 0;
      mapped--;
      if (vmm_unmap_kernel_page(base + (uint64_t)mapped * PAGE_SIZE,
                                &phys) == 0)
        pmm_free_block(phys_to_virt(phys));
    }
    stack_unlock_irqrestore(irq_flags);
    return 0;
  }

  slots[slot] = (uint8_t)pages;
  stack_unlock_irqrestore(irq_flags);
  if (size_out) *size_out = rounded;
  return (void *)base;
}

void kernel_stack_free(void *usable_base, size_t usable_size) {
  uint64_t base = (uint64_t)usable_base;
  if (base < KSTACK_REGION_BASE + PAGE_SIZE) return;
  uint64_t offset = base - KSTACK_REGION_BASE - PAGE_SIZE;
  uint64_t slot_bytes = (uint64_t)KSTACK_SLOT_PAGES * PAGE_SIZE;
  if (offset % slot_bytes) return;
  unsigned slot = (unsigned)(offset / slot_bytes);
  if (slot >= KSTACK_SLOT_COUNT) return;

  uint64_t irq_flags = stack_lock_irqsave();
  unsigned pages = slots[slot];
  if (!pages || usable_size != (size_t)pages * PAGE_SIZE) {
    stack_unlock_irqrestore(irq_flags);
    return;
  }
  for (unsigned page = 0; page < pages; page++) {
    uint64_t phys = 0;
    if (vmm_unmap_kernel_page(base + (uint64_t)page * PAGE_SIZE, &phys) == 0)
      pmm_free_block(phys_to_virt(phys));
  }
  slots[slot] = 0;
  stack_unlock_irqrestore(irq_flags);
}

int kernel_stack_is_guard(uint64_t address) {
  uint64_t page = address & ~(uint64_t)(PAGE_SIZE - 1);
  for (unsigned slot = 0; slot < KSTACK_SLOT_COUNT; slot++) {
    unsigned pages = slots[slot];
    if (!pages) continue;
    uint64_t guard = slot_base(slot);
    uint64_t mapped_start = guard + PAGE_SIZE;
    uint64_t mapped_end = mapped_start + (uint64_t)pages * PAGE_SIZE;
    uint64_t slot_end = guard + (uint64_t)KSTACK_SLOT_PAGES * PAGE_SIZE;
    if (page >= guard && page < mapped_start) return 1;
    if (page >= mapped_end && page < slot_end) return 1;
  }
  return 0;
}

void kernel_stack_adopt_boot(void *usable_base, size_t usable_size) {
  boot_stack_base = usable_base;
  boot_stack_size = usable_size;
}

void *kernel_stack_boot_base(void) { return boot_stack_base; }
size_t kernel_stack_boot_size(void) { return boot_stack_size; }
