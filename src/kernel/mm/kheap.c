#include "kheap.h"
#include "kernel_layout.h"
#include "pmm.h"
#include "serial.h"
#include "spinlock.h"
#include "string.h"

static uint64_t kheap_start = 0;
static uint64_t kheap_current = 0;
static uint64_t kheap_limit = 0;
static spinlock_t kheap_lock = {0};

extern uint64_t memory_size;

extern uint64_t end[];

#define HEADER_SIZE 8

typedef struct free_header {
    uint32_t size;
    struct free_header *next;
} free_header_t;

static free_header_t *free_list = NULL;

void kheap_set_start(uint64_t start) {
  if (start % 4096 != 0)
    start += 4096 - (start % 4096);
  uint64_t arena_start = 0, arena_size = 0;
  /* Web pages and LVGL keep large, independent arenas alive together. Ask for
   * enough kernel heap headroom on RAM-rich machines; PMM still returns the
   * largest available region down to the existing minimum on smaller systems. */
  uint64_t desired = 192ULL * 1024 * 1024;
  uint64_t minimum = 8ULL * 1024 * 1024;
  if (pmm_claim_contiguous_region(start, desired, minimum,
                                  &arena_start, &arena_size) != 0) {
    serial_print("KERNEL PANIC: no contiguous RAM for KHeap\n");
    for (;;) asm volatile("hlt");
  }
  kheap_start = (uint64_t)phys_to_virt(arena_start);
  kheap_current = kheap_start;
  kheap_limit = (uint64_t)phys_to_virt(arena_start + arena_size);
}

char *itoa(int value, char *str, int base);

uint64_t push_interrupts(void) {
    uint64_t eflags;
    asm volatile("pushfq; pop %0; cli" : "=r"(eflags));
    return eflags;
}

void pop_interrupts(uint64_t eflags) {
    asm volatile("push %0; popfq" :: "r"(eflags));
}

void *kmalloc(size_t size) {
  uint64_t eflags = push_interrupts();
  spinlock_acquire(&kheap_lock);
  if (kheap_start == 0)
    kheap_set_start(virt_to_phys(end) + 0x1000);

  size = (size + 3) & ~3;
  uint32_t total = size + HEADER_SIZE;
  if (total < 16) total = 16;

  free_header_t **prev = &free_list;
  free_header_t *curr = free_list;
  while (curr) {
    if (curr->size >= total) {
      *prev = curr->next;
      curr->size = total;
      spinlock_release(&kheap_lock);
      pop_interrupts(eflags);
      return (void*)((uint64_t)curr + HEADER_SIZE);
    }
    prev = &curr->next;
    curr = curr->next;
  }

  if (kheap_current + total < kheap_current || kheap_current + total > kheap_limit) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap!\n");
    asm volatile("hlt");
  }

  free_header_t *header = (free_header_t *)(uint64_t)kheap_current;
  header->size = total;
  /* The bump heap uses the direct-map alias. Keep every physical frame it
   * consumes out of the allocator so DMA/user pages cannot overwrite it. */
  pmm_reserve_region(virt_to_phys((void *)kheap_current), total);
  kheap_current += total;
  spinlock_release(&kheap_lock);
  pop_interrupts(eflags);
  return (void*)((uint64_t)header + HEADER_SIZE);
}

void *kmalloc_a(size_t size) {
  uint64_t eflags = push_interrupts();
  spinlock_acquire(&kheap_lock);
  if (kheap_start == 0)
    kheap_set_start(virt_to_phys(end) + 0x1000);

  // We want the returned address `aligned` to be page-aligned (multiple of 4096),
  // and we need to place the block header right before it (at `aligned - HEADER_SIZE`).
  // So the header position must be >= kheap_current.
  uint64_t raw = kheap_current + HEADER_SIZE;
  uint64_t aligned = raw;
  if (aligned % 4096 != 0) {
    aligned += 4096 - (aligned % 4096);
  }
  uint64_t header_pos = aligned - HEADER_SIZE;
  uint64_t total = (aligned - kheap_current) + size;

  if (kheap_current + total < kheap_current || kheap_current + total > kheap_limit) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap (aligned)!\n");
    asm volatile("hlt");
  }

  free_header_t *header = (free_header_t *)header_pos;
  header->size = size + HEADER_SIZE;

  pmm_reserve_region(virt_to_phys((void *)kheap_current), total);
  kheap_current += total;

  spinlock_release(&kheap_lock);
  pop_interrupts(eflags);
  return (void *)aligned;
}

void *kmalloc_ap(size_t size, uint64_t *phys) {
  uint64_t eflags = push_interrupts();
  spinlock_acquire(&kheap_lock);
  if (kheap_start == 0)
    kheap_set_start(virt_to_phys(end) + 0x1000);

  if (kheap_current % 4096 != 0)
    kheap_current += 4096 - (kheap_current % 4096);

  uint32_t total = size + HEADER_SIZE;
  if (total < 16) total = 16;

  free_header_t **prev = &free_list;
  free_header_t *curr = free_list;
  while (curr) {
    if (curr->size >= total && ((uint64_t)curr % 4096) == 0) {
      *prev = curr->next;
      curr->size = total;
      if (phys) *phys = virt_to_phys(curr);
      spinlock_release(&kheap_lock);
      pop_interrupts(eflags);
      return (void*)((uint64_t)curr + HEADER_SIZE);
    }
    prev = &curr->next;
    curr = curr->next;
  }

  if (kheap_current + total < kheap_current || kheap_current + total > kheap_limit) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap (aligned)!\n");
    asm volatile("hlt");
  }

  free_header_t *header = (free_header_t *)(uint64_t)kheap_current;
  header->size = total;
  pmm_reserve_region(virt_to_phys((void *)kheap_current), total);
  kheap_current += total;
  if (phys) *phys = virt_to_phys(header);
  spinlock_release(&kheap_lock);
  pop_interrupts(eflags);
  return (void*)((uint64_t)header + HEADER_SIZE);
}

void kfree(void *ptr) {
  if (!ptr) return;
  uint64_t eflags = push_interrupts();
  spinlock_acquire(&kheap_lock);

  free_header_t *header = (free_header_t *)((uint64_t)ptr - HEADER_SIZE);
  uint64_t blk_start = (uint64_t)header;

  if (blk_start < kheap_start) {
    spinlock_release(&kheap_lock);
    pop_interrupts(eflags);
    return;
  }

  if (blk_start >= kheap_current || header->size < HEADER_SIZE ||
      blk_start + header->size < blk_start ||
      blk_start + header->size > kheap_current) {
    spinlock_release(&kheap_lock);
    pop_interrupts(eflags);
    return;
  }

  /* Keep free blocks address-sorted so adjacent releases can be reclaimed as
   * one larger allocation. Re-rendering real pages repeatedly creates many
   * short-lived HTML/CSS allocations; without coalescing, the bump heap can
   * report OOM even though those neighboring blocks have all been freed. */
  free_header_t **link = &free_list;
  free_header_t *prev = NULL;
  while (*link && (uint64_t)*link < blk_start) {
    prev = *link;
    link = &(*link)->next;
  }
  free_header_t *next = *link;
  uint64_t block_end = blk_start + header->size;
  if ((prev && (uint64_t)prev + prev->size > blk_start) ||
      (next && block_end > (uint64_t)next)) {
    spinlock_release(&kheap_lock);
    pop_interrupts(eflags);
    return;
  }

  header->next = next;
  *link = header;
  if (prev && (uint64_t)prev + prev->size == blk_start) {
    prev->size += header->size;
    prev->next = header->next;
    header = prev;
  }
  next = header->next;
  if (next && (uint64_t)header + header->size == (uint64_t)next) {
    header->size += next->size;
    header->next = next->next;
  }

  /* Heap frames remain reserved when an allocation returns to the free list. */
  spinlock_release(&kheap_lock);
  pop_interrupts(eflags);
}

void *krealloc(void *ptr, size_t new_size) {
  if (!ptr)
    return kmalloc(new_size);
  if (new_size == 0) {
    kfree(ptr);
    return NULL;
  }

  if (new_size > UINT32_MAX - HEADER_SIZE - 3U)
    return NULL;
  size_t aligned_size = (new_size + 3U) & ~(size_t)3U;
  uint32_t new_total = (uint32_t)aligned_size + HEADER_SIZE;
  if (new_total < 16U) new_total = 16U;

  uint64_t eflags = push_interrupts();
  spinlock_acquire(&kheap_lock);

  free_header_t *header = (free_header_t *)((uint64_t)ptr - HEADER_SIZE);
  if ((uint64_t)header < kheap_start) {
    spinlock_release(&kheap_lock);
    pop_interrupts(eflags);
    return NULL;
  }

  uint32_t old_total = header->size;
  size_t old_size = old_total > HEADER_SIZE ? old_total - HEADER_SIZE : 0;
  if (new_size <= old_size) {
    spinlock_release(&kheap_lock);
    pop_interrupts(eflags);
    return ptr;
  }

  /* Preserve the bump heap's tail allocation in place when it can grow. This
   * avoids copying a large live arena only to need another full-size block at
   * the end of a nearly full heap. */
  uintptr_t header_address = (uintptr_t)header;
  uintptr_t old_end = header_address + old_total;
  uintptr_t new_end = header_address + new_total;
  if (old_end == kheap_current && new_end >= old_end &&
      new_end <= kheap_limit) {
    size_t growth = (size_t)(new_end - old_end);
    if (growth)
      pmm_reserve_region(virt_to_phys((void *)old_end), growth);
    header->size = new_total;
    kheap_current = new_end;
    spinlock_release(&kheap_lock);
    pop_interrupts(eflags);
    return ptr;
  }
  spinlock_release(&kheap_lock);
  pop_interrupts(eflags);

  void *np = kmalloc(aligned_size);
  if (!np)
    return NULL;
  memcpy(np, ptr, old_size);
  kfree(ptr);
  return np;
}
