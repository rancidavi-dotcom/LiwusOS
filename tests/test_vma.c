#ifdef KERNEL_TEST
#include "framework.h"
#include "vma.h"
#include "vmm.h"

int test_vma_intervals(void) {
  page_directory_t source = {0};
  page_directory_t copy = {0};

  if (vma_insert(&source, 0x1000, 0x5000,
                 VMA_PROT_READ | VMA_PROT_WRITE,
                 VMA_PRIVATE | VMA_ANON, 0, 0) < 0)
    FAIL("vma_intervals", "initial interval insertion failed");
  if (vma_insert(&source, 0x8000, 0x9000,
                 VMA_PROT_READ, VMA_PRIVATE | VMA_ANON, 0, 0) < 0)
    FAIL("vma_intervals", "ordered interval insertion failed");
  if (vma_insert(&source, 0x4000, 0xA000,
                 VMA_PROT_READ, VMA_PRIVATE | VMA_ANON, 0, 0) == 0)
    FAIL("vma_intervals", "overlapping interval was accepted");
  if (!vma_find(&source, 0x4000) || vma_find(&source, 0x5000) ||
      !vma_find(&source, 0x8000))
    FAIL("vma_intervals", "interval lookup boundary is incorrect");

  if (vma_remove(&source, 0x2000, 0x3000) < 0 ||
      !vma_find(&source, 0x1000) || vma_find(&source, 0x2000) ||
      !vma_find(&source, 0x3000))
    FAIL("vma_intervals", "middle unmap did not split the VMA");
  uint64_t gap = 0;
  if (vma_find_gap_topdown(&source, 0xA000, 0x1000, 0x1000, &gap) < 0 ||
      gap != 0x9000)
    FAIL("vma_intervals", "top-down gap search returned wrong address");
  if (vma_clone(&source, &copy) < 0 || !vma_find(&copy, 0x3000) ||
      vma_find(&copy, 0x2000))
    FAIL("vma_intervals", "VMA clone did not preserve intervals");

  vma_destroy(&source);
  vma_destroy(&copy);
  PASS("ordered, split, cloned, and released VMA intervals");
}
#endif
