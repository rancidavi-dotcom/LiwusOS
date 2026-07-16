#include "kheap.h"
#include "pmm.h"
#include "serial.h"
#include "spinlock.h"

static uint64_t kheap_start = 0;
static uint64_t kheap_current = 0;
static spinlock_t kheap_lock = {0};

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
  kheap_start = start;
  kheap_current = start;
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

extern uint64_t memory_size;

void *kmalloc(size_t size) {
  uint64_t eflags = push_interrupts();
  spinlock_acquire(&kheap_lock);
  if (kheap_start == 0)
    kheap_set_start((uint64_t)end + 0x1000);

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

  if (kheap_current + total >= memory_size) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap!\n");
    asm volatile("hlt");
  }

  free_header_t *header = (free_header_t *)(uint64_t)kheap_current;
  header->size = total;
  kheap_current += total;
  spinlock_release(&kheap_lock);
  pop_interrupts(eflags);
  return (void*)((uint64_t)header + HEADER_SIZE);
}

void *kmalloc_a(size_t size) {
  uint64_t eflags = push_interrupts();
  spinlock_acquire(&kheap_lock);
  if (kheap_start == 0)
    kheap_set_start((uint64_t)end + 0x1000);

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

  if (kheap_current + total >= memory_size) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap (aligned)!\n");
    asm volatile("hlt");
  }

  free_header_t *header = (free_header_t *)header_pos;
  header->size = size + HEADER_SIZE;

  kheap_current += total;

  spinlock_release(&kheap_lock);
  pop_interrupts(eflags);
  return (void *)aligned;
}

void *kmalloc_ap(size_t size, uint64_t *phys) {
  uint64_t eflags = push_interrupts();
  spinlock_acquire(&kheap_lock);
  if (kheap_start == 0)
    kheap_set_start((uint64_t)end + 0x1000);

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
      if (phys) *phys = (uint64_t)curr;
      spinlock_release(&kheap_lock);
      pop_interrupts(eflags);
      return (void*)((uint64_t)curr + HEADER_SIZE);
    }
    prev = &curr->next;
    curr = curr->next;
  }

  if (kheap_current + total >= memory_size) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap (aligned)!\n");
    asm volatile("hlt");
  }

  free_header_t *header = (free_header_t *)(uint64_t)kheap_current;
  header->size = total;
  kheap_current += total;
  if (phys) *phys = (uint64_t)header;
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

  if (blk_start % 4096 == 0 && header->size >= 4096) {
    uint32_t pages = header->size / 4096;
    for (uint32_t i = 0; i < pages; i++)
      pmm_free_block((void*)(uint64_t)(blk_start + i * 4096));
  }

  header->next = free_list;
  free_list = header;
  spinlock_release(&kheap_lock);
  pop_interrupts(eflags);
}
