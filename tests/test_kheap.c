#ifdef KERNEL_TEST
#include "framework.h"
#include "kheap.h"

int test_kheap_contract(void) {
  uint8_t *original = (uint8_t *)kmalloc(64);
  if (!original) FAIL("kheap_contract", "kmalloc returned null");
  for (unsigned i = 0; i < 64; i++) original[i] = (uint8_t)(i ^ 0x5a);

  uint8_t *shrunk = (uint8_t *)krealloc(original, 32);
  if (shrunk != original) {
    if (shrunk) kfree(shrunk);
    kfree(original);
    FAIL("kheap_contract", "shrinking krealloc did not retain the allocation");
  }
  for (unsigned i = 0; i < 32; i++) {
    if (shrunk[i] != (uint8_t)(i ^ 0x5a)) {
      kfree(shrunk);
      FAIL("kheap_contract", "shrinking krealloc changed existing bytes");
    }
  }

  uint8_t *grown = (uint8_t *)krealloc(shrunk, 128);
  if (!grown) {
    kfree(shrunk);
    FAIL("kheap_contract", "growing krealloc returned null");
  }
  for (unsigned i = 0; i < 32; i++) {
    if (grown[i] != (uint8_t)(i ^ 0x5a)) {
      kfree(grown);
      FAIL("kheap_contract", "growing krealloc did not preserve old bytes");
    }
  }

  kfree(grown);
  void *reused = kmalloc(16);
  if (reused != grown) {
    if (reused) kfree(reused);
    FAIL("kheap_contract", "freed block was not reused for a smaller request");
  }
  kfree(reused);

  void *aligned = kmalloc_a(96);
  if (!aligned) FAIL("kheap_contract", "kmalloc_a returned null");
  if (((uintptr_t)aligned & 4095U) != 0) {
    kfree(aligned);
    FAIL("kheap_contract", "kmalloc_a result is not page-aligned");
  }
  kfree(aligned);

  void *from_null = krealloc(NULL, 24);
  if (!from_null) FAIL("kheap_contract", "krealloc(NULL, size) returned null");
  if (krealloc(from_null, 0) != NULL)
    FAIL("kheap_contract", "krealloc(ptr, 0) did not return null");
  kfree(NULL);

  PASS("kmalloc reuse, krealloc content/size semantics, aligned allocation and cleanup");
}
#endif
