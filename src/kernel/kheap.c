#include "kheap.h"
#include "pmm.h"

// Implementação simples de Bump Allocator
// KHeap Start address
static uint32_t kheap_start = 0;
static uint32_t kheap_current = 0;

extern uint32_t end; // Definido no linker.ld

void kheap_set_start(uint32_t start) {
  if (start % 4096 != 0) {
    start += 4096 - (start % 4096);
  }
  kheap_start = start;
  kheap_current = start;
}

// Helper function to convert int to string
char *itoa(int value, char *str, int base);

uint32_t push_interrupts() {
    uint32_t eflags;
    asm volatile("pushfl; pop %0; cli" : "=r"(eflags));
    return eflags;
}

void pop_interrupts(uint32_t eflags) {
    asm volatile("push %0; popfl" :: "r"(eflags));
}

extern uint32_t memory_size; // From kernel.c/pmm.c

void *kmalloc(size_t size) {
  uint32_t eflags = push_interrupts();
  if (kheap_start == 0) {
    kheap_set_start((uint32_t)&end + 0x1000);
  }

  // Alinhamento simples (4 bytes)
  if (kheap_current % 4 != 0) {
    kheap_current += 4 - (kheap_current % 4);
  }

  if (kheap_current + size >= memory_size) {
      serial_print("KERNEL PANIC: Out of Memory in KHeap bump allocator!\n");
      asm volatile("hlt");
  }

  void *ptr = (void *)kheap_current;
  kheap_current += size;
  pop_interrupts(eflags);
  return ptr;
}

void *kmalloc_a(size_t size) {
  uint32_t eflags = push_interrupts();
  if (kheap_start == 0) {
    kheap_set_start((uint32_t)&end + 0x1000);
  }

  // Alinhamento de página (4096 bytes)
  if (kheap_current % 4096 != 0) {
    kheap_current += 4096 - (kheap_current % 4096);
  }

  if (kheap_current + size >= memory_size) {
      serial_print("KERNEL PANIC: Out of Memory in KHeap bump allocator (aligned)!\n");
      asm volatile("hlt");
  }

  void *ptr = (void *)kheap_current;
  kheap_current += size;
  pop_interrupts(eflags);
  return ptr;
}

void *kmalloc_ap(size_t size, uint32_t *phys) {
  void *ptr = kmalloc_a(size);
  if (phys)
    *phys = (uint32_t)ptr; // No paging identity mapped for now kernel space
  return ptr;
}

// Stub para kfree (Bump allocator não libera memória individualmente)
void kfree(void *ptr) { (void)ptr; }
