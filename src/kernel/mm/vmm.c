#include "vmm.h"
#include "kheap.h"
#include "pmm.h"
#include "string.h"
#include "serial.h"

page_directory_t *kernel_directory = 0;
page_directory_t *current_directory = 0;

static inline void vmm_wrmsr(uint32_t msr, uint64_t val) {
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

static inline uint64_t vmm_rdmsr(uint32_t msr) {
    uint32_t low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return ((uint64_t)high << 32) | low;
}

void vmm_init_pat() {
    uint64_t pat = vmm_rdmsr(0x277); /* IA32_CR_PAT */
    /* PAT0 is bits 0-7 (default 0x06 = WB)
     * PAT1 is bits 8-15 (default 0x04 = WT)
     * We change PAT1 to 0x01 (WC - Write Combining) */
    pat &= ~(0xFFULL << 8);
    pat |= (0x01ULL << 8);
    vmm_wrmsr(0x277, pat);
    serial_print("VMM: PAT initialized (PAT1 = WC)\n");
}

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
  } else if ((flags & PTE_U) && !(pd->entries[pd_i] & PTE_U)) {
    pd->entries[pd_i] |= PTE_U;
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

// Desmapeia uma única página (4 KB) do diretório de páginas ativo e
// libera o frame físico correspondente de volta ao PMM. Útil para munmap.
int vmm_unmap_page(void *virt) {
  uint64_t v = (uint64_t)virt;
  pml4_t *pml4 = current_directory->pml4_virt;
  uint64_t pml4_i = vaddr_pml4i(v);
  uint64_t pdp_i = vaddr_pdpi(v);
  uint64_t pd_i = vaddr_pdi(v);
  uint64_t pt_i = vaddr_pti(v);

  if (!(pml4->entries[pml4_i] & PTE_P)) return -1;
  pdp_t *pdp = (pdp_t *)(uint64_t)(pml4->entries[pml4_i] & ~0xFFFULL);
  if (!(pdp->entries[pdp_i] & PTE_P)) return -1;
  pd_t *pd = (pd_t *)(uint64_t)(pdp->entries[pdp_i] & ~0xFFFULL);
  if (!(pd->entries[pd_i] & PTE_P)) return -1;

  // Não desmapear dentro de um mapping 2 MB (large page)
  if (pd->entries[pd_i] & PTE_PS) return -1;

  pt_t *pt = (pt_t *)(uint64_t)(pd->entries[pd_i] & ~0xFFFULL);
  if (!(pt->entries[pt_i] & PTE_P)) return -1;

  pmm_free_block((void *)(pt->entries[pt_i] & ~0xFFFULL));
  pt->entries[pt_i] = 0;
  asm volatile("invlpg (%0)" : : "r"(v) : "memory");
  return 0;
}

void vmm_map_framebuffer(uint64_t phys_addr, uint64_t size) {
  for (uint64_t i = 0; i < size; i += 4096) {
    /* Map with PTE_PWT to select PAT1 (WC - Write Combining) */
    vmm_map_page((void*)(phys_addr + i), (void*)(phys_addr + i), PTE_P | PTE_W | PTE_PWT);
  }
}

void switch_page_directory(page_directory_t *dir) {
  current_directory = dir;
  asm volatile("mov %0, %%cr3" : : "r"(dir->pml4_phys) : "memory");
}

void init_vmm(uint64_t memory_size) {
  vmm_init_pat();
  kernel_directory = (page_directory_t *)kmalloc_a(sizeof(page_directory_t));
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
  if (!src) return NULL;
  page_directory_t *dest = vmm_create_directory();
  pml4_t *src_pml4 = src->pml4_virt;
  pml4_t *dest_pml4 = dest->pml4_virt;

  for (int i = 0; i < 512; i++) {
    if (src_pml4->entries[i] & PTE_P) {
      pdp_t *new_pdp = (pdp_t *)kmalloc_a(4096);
      memset(new_pdp, 0, 4096);
      dest_pml4->entries[i] = ((uint64_t)new_pdp) | (src_pml4->entries[i] & 0xFFF);

      pdp_t *src_pdp = (pdp_t *)(uint64_t)(src_pml4->entries[i] & ~0xFFFULL);
      for (int j = 0; j < 512; j++) {
        if (src_pdp->entries[j] & PTE_P) {
          pd_t *new_pd = (pd_t *)kmalloc_a(4096);
          memset(new_pd, 0, 4096);
          new_pdp->entries[j] = ((uint64_t)new_pd) | (src_pdp->entries[j] & 0xFFF);

          pd_t *src_pd = (pd_t *)(uint64_t)(src_pdp->entries[j] & ~0xFFFULL);
          for (int k = 0; k < 512; k++) {
            if (src_pd->entries[k] & PTE_P) {
              if (!(src_pd->entries[k] & PTE_PS)) {
                pt_t *new_pt = (pt_t *)kmalloc_a(4096);
                memset(new_pt, 0, 4096);
                new_pd->entries[k] = ((uint64_t)new_pt) | (src_pd->entries[k] & 0xFFF);

                pt_t *src_pt = (pt_t *)(uint64_t)(src_pd->entries[k] & ~0xFFFULL);
                for (int l = 0; l < 512; l++) {
                  if (src_pt->entries[l] & PTE_P) {
                    void *dest_phys = pmm_alloc_block();
                    new_pt->entries[l] = ((uint64_t)dest_phys) | (src_pt->entries[l] & 0xFFF);
                    void *src_virt = (void *)(src_pt->entries[l] & ~0xFFFULL);
                    memcpy(dest_phys, src_virt, 4096);
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  return dest;
}

void vmm_free_directory(page_directory_t *dir) {
  if (!dir) return;
  pml4_t *pml4 = dir->pml4_virt;
  if (!pml4) {
    kfree(dir);
    return;
  }
  for (int i = 0; i < 512; i++) {
    if (pml4->entries[i] & PTE_P) {
      pdp_t *pdp = (pdp_t *)(uint64_t)(pml4->entries[i] & ~0xFFFULL);
      for (int j = 0; j < 512; j++) {
        if (pdp->entries[j] & PTE_P) {
          pd_t *pd = (pd_t *)(uint64_t)(pdp->entries[j] & ~0xFFFULL);
          for (int k = 0; k < 512; k++) {
            if (pd->entries[k] & PTE_P) {
              if (pd->entries[k] & PTE_PS) {
                pmm_free_block((void *)(pd->entries[k] & ~0x1FFFFFULL));
              } else {
                pt_t *pt = (pt_t *)(uint64_t)(pd->entries[k] & ~0xFFFULL);
                for (int l = 0; l < 512; l++) {
                  if (pt->entries[l] & PTE_P) {
                    pmm_free_block((void *)(pt->entries[l] & ~0xFFFULL));
                  }
                }
                kfree(pt);
              }
            }
          }
          kfree(pd);
        }
      }
      kfree(pdp);
    }
  }
  kfree(pml4);
  kfree(dir);
}

#include "task.h"
uint64_t sys_brk(uint64_t addr) {
  if (!current_task) return 0;
  if (addr == 0) return current_task->heap_end;
  uint64_t brk_top = current_task->heap_end;
  if (brk_top < current_task->heap_start) brk_top = current_task->heap_start;
  if (addr <= brk_top) return brk_top;
  uint64_t start = (brk_top + 0xFFF) & ~0xFFFULL;
  uint64_t end = (addr + 0xFFF) & ~0xFFFULL;
  for (uint64_t p = start; p < end; p += 4096) {
    void *phys = pmm_alloc_block();
    vmm_map_page(phys, (void *)p, 0x7);
    memset((void *)p, 0, 4096);
  }
  current_task->heap_end = end;
  return current_task->heap_end;
}
