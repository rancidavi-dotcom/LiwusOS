#ifdef KERNEL_TEST
#include "framework.h"
#include "kernel_layout.h"
#include "pmm.h"
#include "vmm.h"

#define ONE_GIB (1ULL << 30)

int test_pmm_high_memory(void) {
  if (pmm_get_total_memory() <= ONE_GIB)
    FAIL("pmm_high_memory", "test requires QEMU RAM above 1 GiB");
  uint64_t free_before = pmm_get_free_memory();
  volatile uint64_t *page = (volatile uint64_t *)pmm_alloc_block_above(ONE_GIB);
  if (!page) FAIL("pmm_high_memory", "PMM did not allocate a frame above 1 GiB");
  uint64_t phys = virt_to_phys((void *)page);
  if (phys < ONE_GIB || phys >= pmm_get_total_memory()) {
    pmm_free_block((void *)page);
    FAIL("pmm_high_memory", "allocated frame is outside the requested range");
  }
  page[0] = 0x4c495755534d454dULL;
  page[PAGE_SIZE / sizeof(*page) - 1] = ~0x4c495755534d454dULL;
  if (page[0] != 0x4c495755534d454dULL ||
      page[PAGE_SIZE / sizeof(*page) - 1] != ~0x4c495755534d454dULL) {
    pmm_free_block((void *)page);
    FAIL("pmm_high_memory", "direct-map read/write verification failed");
  }
  pmm_free_block((void *)page);
  if (pmm_get_free_memory() != free_before)
    FAIL("pmm_high_memory", "high frame was not released cleanly");
  PASS("allocated, accessed, and freed a physical frame above 1 GiB");
}
#endif
