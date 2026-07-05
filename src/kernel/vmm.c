#include "vmm.h"
#include "kheap.h"
#include "pmm.h"
#include "string.h"
#include "serial.h"

page_directory_t *kernel_directory = 0;
page_directory_t *current_directory = 0;

static int walk_pt_modified_pml4 = 0;

static pte_t *walk_pt(page_directory_t *dir, uint64_t vaddr, uint64_t flags) {
  pml4_t *pml4 = dir->pml4_virt;
  uint64_t pml4_i = vaddr_pml4i(vaddr);
  uint64_t alloc_flags = PTE_P | PTE_W;
  if (flags & PTE_U) alloc_flags |= PTE_U;

  if (!(pml4->entries[pml4_i] & PTE_P)) {
    if (!flags) return 0;
    pdp_t *new_pdp = (pdp_t *)kmalloc_a(4096);
    memset(new_pdp, 0, 4096);
    pml4->entries[pml4_i] = ((uint64_t)new_pdp) | alloc_flags;
    walk_pt_modified_pml4 = 1;
  } else if ((flags & PTE_U) && !(pml4->entries[pml4_i] & PTE_U)) {
    pml4->entries[pml4_i] |= PTE_U;
    walk_pt_modified_pml4 = 1;
  }

  pdp_t *pdp = (pdp_t *)(uint64_t)(pml4->entries[pml4_i] & ~0xFFFULL);
  uint64_t pdp_i = vaddr_pdpi(vaddr);

  if (!(pdp->entries[pdp_i] & PTE_P)) {
    if (!flags) return 0;
    pd_t *new_pd = (pd_t *)kmalloc_a(4096);
    memset(new_pd, 0, 4096);
    pdp->entries[pdp_i] = ((uint64_t)new_pd) | alloc_flags;
  } else if ((flags & PTE_U) && !(pdp->entries[pdp_i] & PTE_U)) {
    pdp->entries[pdp_i] |= PTE_U;
  }

  pd_t *pd = (pd_t *)(uint64_t)(pdp->entries[pdp_i] & ~0xFFFULL);
  uint64_t pd_i = vaddr_pdi(vaddr);

  if (pd->entries[pd_i] & PTE_PS) {
    if (!flags) return 0;
    uint64_t large_base = pd->entries[pd_i] & ~0x1FFFFFULL;
    uint64_t large_flags = pd->entries[pd_i] & 0x1FFULL;
    if (flags & PTE_U) large_flags |= PTE_U;
    if (flags & PTE_W) large_flags |= PTE_W;
    pt_t *new_pt = (pt_t *)kmalloc_a(4096);
    memset(new_pt, 0, 4096);
    for (int j = 0; j < 512; j++) {
      new_pt->entries[j] = (large_base + j * 4096) | (large_flags & ~PTE_PS);
    }
    pd->entries[pd_i] = ((uint64_t)new_pt) | alloc_flags;
  }

  if (!(pd->entries[pd_i] & PTE_P)) {
    if (!flags) return 0;
    pt_t *new_pt = (pt_t *)kmalloc_a(4096);
    memset(new_pt, 0, 4096);
    pd->entries[pd_i] = ((uint64_t)new_pt) | alloc_flags;
  }

  pt_t *pt = (pt_t *)(uint64_t)(pd->entries[pd_i] & ~0xFFFULL);
  uint64_t pt_i = vaddr_pti(vaddr);
  return &pt->entries[pt_i];
}

void vmm_map_page(void *phys, void *virt, uint64_t flags) {
  uint64_t p = (uint64_t)phys;
  uint64_t v = (uint64_t)virt;
  uint64_t f = flags;

  walk_pt_modified_pml4 = 0;
  pte_t *entry = walk_pt(current_directory, v, f);
  if (entry) {
    *entry = (p & ~0xFFFULL) | (f & 0xFFF) | PTE_P;
    if (walk_pt_modified_pml4) {
      uint64_t cr3;
      asm volatile("mov %%cr3, %0" : "=r"(cr3));
      asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    } else {
      asm volatile("invlpg (%0)" : : "r"(v) : "memory");
    }
  }
}

void vmm_map_framebuffer(uint64_t phys_addr, uint64_t size) {
  for (uint64_t i = 0; i < size; i += 4096) {
    vmm_map_page((void*)(phys_addr + i), (void*)(phys_addr + i), PTE_P | PTE_W);
  }
}

void switch_page_directory(page_directory_t *dir) {
  current_directory = dir;
  asm volatile("mov %0, %%cr3" : : "r"(dir->pml4_phys) : "memory");
}

void init_vmm(uint64_t memory_size) {
  (void)memory_size;
  kernel_directory = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  memset(kernel_directory, 0, sizeof(page_directory_t));
  uint64_t cr3;
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  kernel_directory->pml4_phys = cr3;
  kernel_directory->pml4_virt = (pml4_t *)(uint64_t)cr3;
  current_directory = kernel_directory;
}

page_directory_t *vmm_create_directory() {
  page_directory_t *dir = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  memset(dir, 0, sizeof(page_directory_t));
  pml4_t *new_pml4 = (pml4_t *)kmalloc_a(4096);
  memset(new_pml4, 0, 4096);
  pml4_t *kernel_pml4 = kernel_directory->pml4_virt;
  for (int i = 0; i < 512; i++)
    new_pml4->entries[i] = kernel_pml4->entries[i];
  dir->pml4_virt = new_pml4;
  dir->pml4_phys = (uint64_t)new_pml4;
  return dir;
}

page_directory_t *vmm_copy_directory(page_directory_t *src) {
  (void)src;
  return vmm_create_directory();
}

#include "task.h"
extern task_t *current_task;
uint64_t sys_brk(uint64_t addr) {
  if (!current_task) return 0;
  if (addr == 0 || addr < current_task->heap_start) return current_task->heap_end;
  if (addr > current_task->heap_end) {
    uint64_t start = (current_task->heap_end + 0xFFF) & ~0xFFFULL;
    uint64_t end = (addr + 0xFFF) & ~0xFFFULL;
    for (uint64_t p = start; p < end; p += 4096) {
      void *phys = kmalloc_a(4096);
      vmm_map_page(phys, (void *)p, 0x7);
      memset((void *)p, 0, 4096);
    }
    current_task->heap_end = end;
  }
  return current_task->heap_end;
}
