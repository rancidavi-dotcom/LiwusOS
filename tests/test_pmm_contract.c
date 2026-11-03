#ifdef KERNEL_TEST
#include "framework.h"
#include "kernel_layout.h"
#include "pmm.h"
#include "vmm.h"

#define PMM_TEST_MIN_PHYS (1ULL << 30)

int test_pmm_contract(void) {
  uint64_t free_before = pmm_get_free_memory();
  void *frames[3] = {0};

  for (unsigned i = 0; i < 3; i++) {
    frames[i] = pmm_alloc_block_above(PMM_TEST_MIN_PHYS);
    if (!frames[i]) {
      for (unsigned j = 0; j < i; j++) pmm_free_block(frames[j]);
      FAIL("pmm_contract", "failed to allocate a frame above 1 GiB");
    }
    uint64_t phys = virt_to_phys(frames[i]);
    if (phys < PMM_TEST_MIN_PHYS || (phys & (PAGE_SIZE - 1))) {
      for (unsigned j = 0; j <= i; j++) pmm_free_block(frames[j]);
      FAIL("pmm_contract", "allocated frame is below range or unaligned");
    }
    for (unsigned j = 0; j < i; j++) {
      if (frames[i] == frames[j]) {
        for (unsigned k = 0; k <= i; k++) pmm_free_block(frames[k]);
        FAIL("pmm_contract", "allocator returned a frame twice");
      }
    }
  }

  void *low_frame = pmm_alloc_block_below(2ULL * 1024 * 1024 * 1024);
  if (!low_frame ||
      virt_to_phys(low_frame) >= 2ULL * 1024 * 1024 * 1024) {
    if (low_frame) pmm_free_block(low_frame);
    for (unsigned i = 0; i < 3; i++) pmm_free_block(frames[i]);
    FAIL("pmm_contract", "below-limit allocation crossed the physical limit");
  }
  pmm_free_block(low_frame);

  volatile uint64_t *words = (volatile uint64_t *)frames[1];
  words[0] = 0x4c495755504d4d31ULL;
  words[PAGE_SIZE / sizeof(*words) - 1] = 0x4c495755504d4d32ULL;
  if (words[0] != 0x4c495755504d4d31ULL ||
      words[PAGE_SIZE / sizeof(*words) - 1] != 0x4c495755504d4d32ULL) {
    for (unsigned i = 0; i < 3; i++) pmm_free_block(frames[i]);
    FAIL("pmm_contract", "direct-map frame read/write failed");
  }

  for (unsigned i = 0; i < 3; i++) pmm_free_block(frames[i]);
  uint64_t free_after_release = pmm_get_free_memory();
  if (free_after_release != free_before)
    FAIL("pmm_contract", "free-frame count did not return after release");

  pmm_free_block(frames[0]); /* A second free must not underflow the count. */
  pmm_free_block((void *)(uintptr_t)0x12345000ULL);
  pmm_free_block((void *)(uintptr_t)(DIRECT_MAP_BASE + 0x123ULL));
  if (pmm_get_free_memory() != free_before)
    FAIL("pmm_contract", "double-free or invalid pointer changed the count");

  uint64_t claimed_base = UINT64_MAX;
  uint64_t claimed_size = UINT64_MAX;
  if (pmm_claim_contiguous_region(PMM_TEST_MIN_PHYS, 2 * PAGE_SIZE,
                                  PAGE_SIZE, &claimed_base, &claimed_size) != 0)
    FAIL("pmm_contract", "could not claim a two-frame contiguous range");
  if (claimed_base < PMM_TEST_MIN_PHYS ||
      (claimed_base & (PAGE_SIZE - 1)) || claimed_size < 2 * PAGE_SIZE ||
      (claimed_size & (PAGE_SIZE - 1))) {
    pmm_init_region(claimed_base, claimed_size);
    FAIL("pmm_contract", "contiguous claim returned an invalid range");
  }
  if (pmm_get_free_memory() != free_before - claimed_size) {
    pmm_init_region(claimed_base, claimed_size);
    FAIL("pmm_contract", "contiguous claim did not reserve exactly its range");
  }
  pmm_init_region(claimed_base, claimed_size);
  if (pmm_get_free_memory() != free_before)
    FAIL("pmm_contract", "contiguous range release did not restore free count");

  uint64_t untouched_base = 0x1111111111111111ULL;
  uint64_t untouched_size = 0x2222222222222222ULL;
  if (pmm_claim_contiguous_region(PMM_TEST_MIN_PHYS, 0, 0,
                                  &untouched_base, &untouched_size) == 0 ||
      untouched_base != 0x1111111111111111ULL ||
      untouched_size != 0x2222222222222222ULL)
    FAIL("pmm_contract", "invalid claim arguments were accepted or modified outputs");
  if (pmm_claim_contiguous_region(PMM_TEST_MIN_PHYS, PAGE_SIZE, PAGE_SIZE,
                                  NULL, &untouched_size) == 0)
    FAIL("pmm_contract", "claim accepted a null output pointer");

  PASS("PMM allocation, invalid frees, direct map, contiguous claim and cleanup");
}
#endif
