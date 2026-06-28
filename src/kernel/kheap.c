#include "kheap.h"
#include "pmm.h"
#include "serial.h"

static uint32_t kheap_start = 0;
static uint32_t kheap_current = 0;

extern uint32_t end;

#define HEADER_SIZE 8

typedef struct free_header {
    uint32_t size;
    struct free_header *next;
} free_header_t;

static free_header_t *free_list = NULL;

void kheap_set_start(uint32_t start) {
  if (start % 4096 != 0)
    start += 4096 - (start % 4096);
  kheap_start = start;
  kheap_current = start;
}

char *itoa(int value, char *str, int base);

uint32_t push_interrupts(void) {
    uint32_t eflags;
    asm volatile("pushfl; pop %0; cli" : "=r"(eflags));
    return eflags;
}

void pop_interrupts(uint32_t eflags) {
    asm volatile("push %0; popfl" :: "r"(eflags));
}

extern uint32_t memory_size;

void *kmalloc(size_t size) {
  uint32_t eflags = push_interrupts();
  if (kheap_start == 0)
    kheap_set_start((uint32_t)&end + 0x1000);

  size = (size + 3) & ~3;
  uint32_t total = size + HEADER_SIZE;
  if (total < 16) total = 16;

  free_header_t **prev = &free_list;
  free_header_t *curr = free_list;
  while (curr) {
    if (curr->size >= total) {
      *prev = curr->next;
      curr->size = total;
      pop_interrupts(eflags);
      return (void*)((uint32_t)curr + HEADER_SIZE);
    }
    prev = &curr->next;
    curr = curr->next;
  }

  if (kheap_current + total >= memory_size) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap!\n");
    asm volatile("hlt");
  }

  free_header_t *header = (free_header_t *)kheap_current;
  header->size = total;
  kheap_current += total;
  pop_interrupts(eflags);
  return (void*)((uint32_t)header + HEADER_SIZE);
}

void *kmalloc_a(size_t size) {
  uint32_t eflags = push_interrupts();
  if (kheap_start == 0)
    kheap_set_start((uint32_t)&end + 0x1000);

  if (kheap_current % 4096 != 0)
    kheap_current += 4096 - (kheap_current % 4096);

  /* Simple bump allocator without header — returned pointer IS the
     page-aligned base. kfree cannot free these, but page tables
     live forever anyway. */
  if (kheap_current + size >= memory_size) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap (aligned)!\n");
    asm volatile("hlt");
  }
  uint32_t ret = kheap_current;
  kheap_current += size;
  pop_interrupts(eflags);
  return (void *)ret;
}

void *kmalloc_ap(size_t size, uint32_t *phys) {
  uint32_t eflags = push_interrupts();
  if (kheap_start == 0)
    kheap_set_start((uint32_t)&end + 0x1000);

  if (kheap_current % 4096 != 0)
    kheap_current += 4096 - (kheap_current % 4096);

  uint32_t total = size + HEADER_SIZE;
  if (total < 16) total = 16;

  free_header_t **prev = &free_list;
  free_header_t *curr = free_list;
  while (curr) {
    if (curr->size >= total && ((uint32_t)curr % 4096) == 0) {
      *prev = curr->next;
      curr->size = total;
      if (phys) *phys = (uint32_t)curr;
      pop_interrupts(eflags);
      return (void*)((uint32_t)curr + HEADER_SIZE);
    }
    prev = &curr->next;
    curr = curr->next;
  }

  if (kheap_current + total >= memory_size) {
    serial_print("KERNEL PANIC: Out of Memory in KHeap (aligned)!\n");
    asm volatile("hlt");
  }

  free_header_t *header = (free_header_t *)kheap_current;
  header->size = total;
  kheap_current += total;
  pop_interrupts(eflags);
  if (phys) *phys = (uint32_t)header;
  return (void*)((uint32_t)header + HEADER_SIZE);
}

void kfree(void *ptr) {
  if (!ptr) return;
  uint32_t eflags = push_interrupts();

  /* kmalloc_a allocations are headerless — cannot free them */
  if ((uint32_t)ptr % 4096 == 0) {
    pop_interrupts(eflags);
    return;
  }

  free_header_t *header = (free_header_t *)((uint32_t)ptr - HEADER_SIZE);
  uint32_t blk_start = (uint32_t)header;

  if (blk_start < kheap_start) {
    pop_interrupts(eflags);
    return;
  }

  if (blk_start % 4096 == 0 && header->size >= 4096) {
    uint32_t pages = header->size / 4096;
    for (uint32_t i = 0; i < pages; i++)
      pmm_free_block((void*)(blk_start + i * 4096));
  }

  header->next = free_list;
  free_list = header;
  pop_interrupts(eflags);
}
