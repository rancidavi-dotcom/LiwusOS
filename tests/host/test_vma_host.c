#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kheap.h"
#include "usercopy.h"
#include "vma.h"
#include "vmm.h"

#define MODEL_PAGES 256
#define MODEL_BASE USER_ADDR_MIN
#define DEFAULT_RANDOM_STEPS 5000
#define STRESS_RANDOM_STEPS 50000

static int fail_after = -1;
static size_t alloc_count;
static size_t free_count;
static uint32_t rng = 0x4c495755U;

void *kmalloc(size_t size) {
  if (fail_after == 0) return NULL;
  if (fail_after > 0) fail_after--;
  void *ptr = malloc(size);
  if (ptr) alloc_count++;
  return ptr;
}

void kfree(void *ptr) {
  if (ptr) {
    free_count++;
    free(ptr);
  }
}

static void check(int condition, const char *message, int step) {
  if (!condition) {
    fprintf(stderr, "VMA host test FAIL at step %d (seed=0x%08x): %s\n",
            step, rng, message);
    exit(1);
  }
}

static uint32_t random_u32(void) {
  rng ^= rng << 13;
  rng ^= rng >> 17;
  rng ^= rng << 5;
  return rng;
}

static size_t vma_count(const page_directory_t *mm) {
  size_t count = 0;
  for (const vma_t *area = mm->vmas; area; area = area->next) count++;
  return count;
}

static void check_model(const page_directory_t *mm,
                        const uint8_t expected[MODEL_PAGES], int step) {
  const vma_t *previous = NULL;
  for (const vma_t *area = mm->vmas; area; area = area->next) {
    check(area->start >= MODEL_BASE && area->start < area->end &&
              area->end <= MODEL_BASE + MODEL_PAGES * PAGE_SIZE,
          "VMA bounds are invalid", step);
    check(((area->start | area->end) & (PAGE_SIZE - 1)) == 0,
          "VMA boundaries lost page alignment", step);
    if (previous)
      check(previous->start < area->start && previous->end <= area->start,
            "VMA list is unsorted or overlapping", step);
    previous = area;
  }
  for (size_t page = 0; page < MODEL_PAGES; page++) {
    uint64_t address = MODEL_BASE + page * PAGE_SIZE;
    check((vma_find((page_directory_t *)mm, address) != NULL) ==
              (expected[page] != 0),
          "vma_find disagrees with the reference page model", step);
  }
}

int main(int argc, char **argv) {
  int random_steps = DEFAULT_RANDOM_STEPS;
  if (argc == 2 && strcmp(argv[1], "--stress") == 0)
    random_steps = STRESS_RANDOM_STEPS;
  else if (argc > 1) {
    fprintf(stderr, "usage: %s [--stress]\n", argv[0]);
    return 2;
  }
  page_directory_t mm = {0};
  uint8_t expected[MODEL_PAGES] = {0};

  check(vma_insert(&mm, MODEL_BASE + 1, MODEL_BASE + PAGE_SIZE,
                   VMA_PROT_READ, VMA_PRIVATE | VMA_ANON, 0, NULL) < 0,
        "unaligned insertion accepted", -1);
  check(vma_insert(&mm, 0, PAGE_SIZE, VMA_PROT_READ,
                   VMA_PRIVATE | VMA_ANON, 0, NULL) < 0,
        "null page insertion accepted", -1);
  check(vma_insert(&mm, USER_ADDR_MAX - PAGE_SIZE,
                   USER_ADDR_MAX + PAGE_SIZE, VMA_PROT_READ,
                   VMA_PRIVATE | VMA_ANON, 0, NULL) < 0,
        "overflowing/non-user interval accepted", -1);

  check(vma_insert(&mm, 0x1000, 0x3000, VMA_PROT_READ | VMA_PROT_WRITE,
                   VMA_PRIVATE | VMA_ANON, 0, NULL) == 0,
        "initial interval insertion failed", -1);
  check(vma_insert(&mm, 0x3000, 0x5000, VMA_PROT_READ | VMA_PROT_WRITE,
                   VMA_PRIVATE | VMA_ANON, 0x2000, NULL) == 0,
        "adjacent interval insertion failed", -1);
  check(vma_count(&mm) == 1 && mm.vmas->start == 0x1000 &&
            mm.vmas->end == 0x5000,
        "compatible adjacent intervals did not coalesce", -1);
  check(vma_find(&mm, 0x4fff) != NULL && vma_find(&mm, 0x5000) == NULL,
        "half-open lookup boundary is wrong", -1);
  check(vma_insert(&mm, 0x2000, 0x4000, VMA_PROT_READ,
                   VMA_PRIVATE | VMA_ANON, 0, NULL) < 0 &&
            vma_count(&mm) == 1,
        "overlap accepted or changed existing metadata", -1);
  vma_destroy(&mm);

  check(vma_insert(&mm, 0x1000, 0x9000, VMA_PROT_READ,
                   VMA_PRIVATE | VMA_FILE, 0x2000, (void *)1) == 0,
        "file interval insertion failed", -1);
  fail_after = 0;
  check(vma_remove(&mm, 0x3000, 0x5000) < 0,
        "middle split succeeded despite injected allocation failure", -1);
  fail_after = -1;
  check(vma_count(&mm) == 1 && mm.vmas->start == 0x1000 &&
            mm.vmas->end == 0x9000 && mm.vmas->file_offset == 0x2000,
        "failed split mutated the source interval", -1);
  check(vma_remove(&mm, 0x3000, 0x5000) == 0 && vma_count(&mm) == 2,
        "middle removal did not split the file interval", -1);
  check(mm.vmas->end == 0x3000 && mm.vmas->next->start == 0x5000 &&
            mm.vmas->next->file_offset == 0x6000,
        "split did not preserve right-side file offset", -1);
  vma_destroy(&mm);

  check(vma_insert(&mm, 0x1000, 0x3000, VMA_PROT_READ | VMA_PROT_WRITE,
                   VMA_PRIVATE | VMA_ANON | VMA_HEAP, 0, NULL) == 0,
        "heap interval insertion failed", -1);
  check(vma_insert(&mm, 0x5000, 0x6000, VMA_PROT_READ,
                   VMA_PRIVATE | VMA_ANON, 0, NULL) == 0,
        "collision fixture insertion failed", -1);
  check(vma_resize_heap(&mm, 0x1000, 0x7000) < 0 &&
            vma_find(&mm, 0x2000) != NULL && vma_find(&mm, 0x4000) == NULL,
        "colliding heap resize was not rejected atomically", -1);
  check(vma_resize_heap(&mm, 0x1000, 0x5000) == 0 &&
            vma_find(&mm, 0x4000) != NULL && vma_find(&mm, 0x5000) != NULL,
        "heap growth adjacent to another VMA was rejected", -1);
  check(vma_resize_heap(&mm, 0x1000, 0x4000) == 0 &&
            vma_find(&mm, 0x3000) != NULL && vma_find(&mm, 0x4000) == NULL,
        "valid heap growth failed", -1);
  uint64_t gap = 0;
  check(vma_find_gap_topdown(&mm, 0xA000, 0x1000, 0x1000, &gap) == 0 &&
            gap == 0x9000,
        "top-down gap search returned an unexpected address", -1);
  vma_destroy(&mm);

  /* Reproducible 5,000-operation model-based insert/remove stress. */
  for (int step = 0; step < random_steps; step++) {
    uint32_t first = random_u32() % MODEL_PAGES;
    uint32_t length = 1 + random_u32() % 12;
    uint32_t last = first + length;
    if (last > MODEL_PAGES) last = MODEL_PAGES;
    uint64_t start = MODEL_BASE + (uint64_t)first * PAGE_SIZE;
    uint64_t end = MODEL_BASE + (uint64_t)last * PAGE_SIZE;
    if (random_u32() & 1U) {
      int overlaps = 0;
      for (uint32_t page = first; page < last; page++)
        overlaps |= expected[page] != 0;
      int result = vma_insert(&mm, start, end,
                              VMA_PROT_READ | VMA_PROT_WRITE,
                              VMA_PRIVATE | VMA_ANON, 0, NULL);
      check((result == 0) == !overlaps,
            "insert result disagrees with reference overlap model", step);
      if (!overlaps)
        for (uint32_t page = first; page < last; page++) expected[page] = 1;
    } else {
      check(vma_remove(&mm, start, end) == 0,
            "valid randomized removal failed", step);
      for (uint32_t page = first; page < last; page++) expected[page] = 0;
    }
    check_model(&mm, expected, step);

    if ((step + 1) % 250 == 0) {
      page_directory_t clone = {0};
      check(vma_clone(&mm, &clone) == 0,
            "clone failed during randomized sequence", step);
      check_model(&clone, expected, step);
      vma_destroy(&clone);
    }
  }

  vma_destroy(&mm);
  check(mm.vmas == NULL, "destroy left VMA metadata linked", random_steps);
  check(alloc_count == free_count,
        "VMA metadata allocations leaked", random_steps);
  printf("VMA host PASS: boundary/error/fault-injection plus %d model steps (seed=0x4c495755), allocations=%zu frees=%zu\n",
         random_steps, alloc_count, free_count);
  return 0;
}
