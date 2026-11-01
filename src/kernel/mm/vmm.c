#include "vmm.h"
#include "kheap.h"
#include "pmm.h"
#include "kernel_layout.h"
#include "string.h"
#include "serial.h"
#include "usercopy.h"
#include "vma.h"
#include "spinlock.h"

page_directory_t *kernel_directory = 0;
page_directory_t *current_directory = 0;
static page_directory_t *directory_registry = 0;
static spinlock_t directory_registry_lock = {0};
static int vmm_unmap_page_keep_frame_in(page_directory_t *dir,
                                        uint64_t virt);

static uint64_t directory_lock_irqsave(void) {
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
  spinlock_acquire(&directory_registry_lock);
  return flags;
}

static void directory_unlock_irqrestore(uint64_t flags) {
  spinlock_release(&directory_registry_lock);
  if (flags & (1ULL << 9)) asm volatile("sti" ::: "memory");
}

static void directory_register(page_directory_t *dir) {
  if (!dir || dir == kernel_directory) return;
  uint64_t irq_flags = directory_lock_irqsave();
  dir->registry_next = directory_registry;
  directory_registry = dir;
  directory_unlock_irqrestore(irq_flags);
}

static void directory_unregister(page_directory_t *dir) {
  if (!dir || dir == kernel_directory) return;
  uint64_t irq_flags = directory_lock_irqsave();
  page_directory_t **link = &directory_registry;
  while (*link && *link != dir) link = &(*link)->registry_next;
  if (*link == dir) *link = dir->registry_next;
  dir->registry_next = 0;
  directory_unlock_irqrestore(irq_flags);
}

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
static int vmm_nx_enabled = 0;
static int vmm_smap_enabled = 0;

void vmm_init_cpu_protections(void) {
  uint32_t eax, ebx, ecx, edx;
  eax = 0x80000000U;
  asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  if (eax >= 0x80000001U) {
    eax = 0x80000001U;
    asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    if (edx & (1U << 20)) {
      uint32_t low, high;
      asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080U));
      low |= 1U << 11; /* EFER.NXE */
      asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000080U));
      vmm_nx_enabled = 1;
      serial_print("VMM: EFER.NXE enabled\n");
    }
  }

  eax = 0;
  asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  uint32_t max_basic_leaf = eax;
  if (max_basic_leaf < 7) return;
  eax = 7;
  ecx = 0;
  asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
  uint64_t cr4;
  asm volatile("mov %%cr4, %0" : "=r"(cr4));
  if (ebx & (1U << 7)) { /* CPUID.7.0:EBX.SMEP */
    cr4 |= 1ULL << 20;
    serial_print("VMM: CR4.SMEP enabled\n");
  }
  if (ebx & (1U << 20)) { /* CPUID.7.0:EBX.SMAP */
    cr4 |= 1ULL << 21;
    vmm_smap_enabled = 1;
    serial_print("VMM: CR4.SMAP enabled\n");
  }
  if (ebx & ((1U << 7) | (1U << 20))) {
    asm volatile("mov %0, %%cr4" : : "r"(cr4) : "memory");
  }
}

int vmm_smap_available(void) { return vmm_smap_enabled; }

static void *alloc_pt_page(void) {
  void *page = pmm_alloc_block();
  if (page) memset(page, 0, PAGE_SIZE);
  return page;
}

static pte_t *walk_pt(page_directory_t *dir, uint64_t vaddr, uint64_t flags) {
  pml4_t *pml4 = dir->pml4_virt;
  uint64_t pml4_i = vaddr_pml4i(vaddr);
  uint64_t alloc_flags = PTE_P | PTE_W;
  if (flags & PTE_U) alloc_flags |= PTE_U;

  if (!(pml4->entries[pml4_i] & PTE_P)) {
    if (!flags) return 0;
    pdp_t *new_pdp = (pdp_t *)alloc_pt_page();
    if (!new_pdp) return 0;
    pml4->entries[pml4_i] = virt_to_phys(new_pdp) | alloc_flags;
    walk_pt_modified_pml4 = 1;
  } else if ((flags & PTE_U) && !(pml4->entries[pml4_i] & PTE_U)) {
    pml4->entries[pml4_i] |= PTE_U;
    walk_pt_modified_pml4 = 1;
  }

  pdp_t *pdp = (pdp_t *)phys_to_virt(pml4->entries[pml4_i] & PTE_ADDR_MASK);
  uint64_t pdp_i = vaddr_pdpi(vaddr);

  if (!(pdp->entries[pdp_i] & PTE_P)) {
    if (!flags) return 0;
    pd_t *new_pd = (pd_t *)alloc_pt_page();
    if (!new_pd) return 0;
    pdp->entries[pdp_i] = virt_to_phys(new_pd) | alloc_flags;
  } else if ((flags & PTE_U) && !(pdp->entries[pdp_i] & PTE_U)) {
    pdp->entries[pdp_i] |= PTE_U;
  }

  pd_t *pd = (pd_t *)phys_to_virt(pdp->entries[pdp_i] & PTE_ADDR_MASK);
  uint64_t pd_i = vaddr_pdi(vaddr);

  if (pd->entries[pd_i] & PTE_PS) {
    if (!flags) return 0;
    uint64_t large_base = pd->entries[pd_i] & PTE_LARGE_ADDR_MASK;
    uint64_t large_flags = pd->entries[pd_i] & 0x1FFULL;
    /* Preserve permissions on neighboring leaves after splitting. */
    pt_t *new_pt = (pt_t *)alloc_pt_page();
    if (!new_pt) return 0;
    for (int j = 0; j < 512; j++) {
      new_pt->entries[j] = (large_base + j * 4096) |
                           (large_flags & ~PTE_PS) |
                           (pd->entries[pd_i] & PTE_NX);
    }
    pd->entries[pd_i] = virt_to_phys(new_pt) | alloc_flags;
  }

  if (!(pd->entries[pd_i] & PTE_P)) {
    if (!flags) return 0;
    pt_t *new_pt = (pt_t *)alloc_pt_page();
    if (!new_pt) return 0;
    pd->entries[pd_i] = virt_to_phys(new_pt) | alloc_flags;
  } else if ((flags & PTE_U) && !(pd->entries[pd_i] & PTE_U)) {
    pd->entries[pd_i] |= PTE_U;
  }

  pt_t *pt = (pt_t *)phys_to_virt(pd->entries[pd_i] & PTE_ADDR_MASK);
  uint64_t pt_i = vaddr_pti(vaddr);
  return &pt->entries[pt_i];
}

int vmm_page_present_in(page_directory_t *dir, uint64_t virt) {
  if (!dir || !dir->pml4_virt) return 0;
  pte_t *entry = walk_pt(dir, virt, 0);
  return entry && (*entry & PTE_P);
}

int vmm_get_mapping_in(page_directory_t *dir, uint64_t virt,
                       uint64_t *phys_out, uint64_t *flags_out) {
  if (!dir || !dir->pml4_virt) return -1;
  pte_t *entry = walk_pt(dir, virt, 0);
  if (!entry || !(*entry & PTE_P)) return -1;
  if (phys_out) *phys_out = *entry & PTE_ADDR_MASK;
  if (flags_out) *flags_out = *entry & (0xFFFULL | PTE_NX);
  return 0;
}

int vmm_map_page(void *phys, void *virt, uint64_t flags) {
  return vmm_map_page_in(current_directory, phys, virt, flags);
}

int vmm_map_page_in(page_directory_t *dir, void *phys, void *virt,
                    uint64_t flags) {
  if (!dir || !dir->pml4_virt || !phys || !virt) return -1;
  uint64_t p = virt_to_phys(phys);
  uint64_t v = (uint64_t)virt;
  uint64_t f = flags;
  if (!vmm_nx_enabled) f &= ~PTE_NX;
  if ((f & PTE_U) && !is_user_addr(virt)) return -1;

  walk_pt_modified_pml4 = 0;
  pte_t *entry = walk_pt(dir, v, f);
  if (!entry) return -1;
  if ((*entry & PTE_P) && ((*entry & PTE_ADDR_MASK) != (p & PTE_ADDR_MASK)))
    return -1;
  *entry = (p & PTE_ADDR_MASK) | (f & (0xFFFULL | PTE_NX)) | PTE_P;
  if (dir == current_directory && walk_pt_modified_pml4) {
    uint64_t cr3;
    asm volatile("mov %%cr3, %0" : "=r"(cr3));
    asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
  } else if (dir == current_directory) {
    asm volatile("invlpg (%0)" : : "r"(v) : "memory");
  }
  return 0;
}

int vmm_map_kernel_page(uint64_t phys, uint64_t virt, uint64_t flags) {
  if (!kernel_directory || (flags & PTE_U) ||
      (virt & (PAGE_SIZE - 1)) || (phys & (PAGE_SIZE - 1))) return -1;
  int result = 0;
  uint64_t irq_flags = directory_lock_irqsave();
  if (vmm_map_page_in(kernel_directory, (void *)phys, (void *)virt,
                      flags) < 0) result = -1;
  for (page_directory_t *dir = directory_registry; !result && dir;
       dir = dir->registry_next) {
    if (vmm_map_page_in(dir, (void *)phys, (void *)virt, flags) < 0)
      result = -1;
  }
  if (result < 0) {
    /* A failed broadcast must not leave a partially installed alias. */
    (void)vmm_unmap_page_keep_frame_in(kernel_directory, virt);
    for (page_directory_t *dir = directory_registry; dir;
         dir = dir->registry_next)
      (void)vmm_unmap_page_keep_frame_in(dir, virt);
  }
  directory_unlock_irqrestore(irq_flags);
  return result;
}

int vmm_map_large_page(uint64_t phys, uint64_t virt, uint64_t flags) {
  return vmm_map_large_page_in(current_directory, phys, virt, flags);
}

int vmm_map_large_page_in(page_directory_t *dir, uint64_t phys,
                          uint64_t virt, uint64_t flags) {
  if (!dir || !dir->pml4_virt) return -1;
  if (!vmm_nx_enabled) flags &= ~PTE_NX;
  if ((phys & 0x1FFFFFULL) || (virt & 0x1FFFFFULL) ||
      vaddr_pml4i(virt) != vaddr_pml4i(virt + 0x1FFFFFULL) ||
      (flags & PTE_U)) return -1;

  pml4_t *pml4 = dir->pml4_virt;
  uint64_t pml4_i = vaddr_pml4i(virt);
  uint64_t pdp_i = vaddr_pdpi(virt);
  uint64_t pd_i = vaddr_pdi(virt);
  uint64_t table_flags = PTE_P | PTE_W;

  if (!(pml4->entries[pml4_i] & PTE_P)) {
    pdp_t *new_pdp = (pdp_t *)alloc_pt_page();
    if (!new_pdp) return -1;
    pml4->entries[pml4_i] = virt_to_phys(new_pdp) | table_flags;
  }
  pdp_t *pdp = (pdp_t *)phys_to_virt(pml4->entries[pml4_i] & PTE_ADDR_MASK);
  if (pdp->entries[pdp_i] & PTE_PS) return -1;
  if (!(pdp->entries[pdp_i] & PTE_P)) {
    pd_t *new_pd = (pd_t *)alloc_pt_page();
    if (!new_pd) return -1;
    pdp->entries[pdp_i] = virt_to_phys(new_pd) | table_flags;
  }
  pd_t *pd = (pd_t *)phys_to_virt(pdp->entries[pdp_i] & PTE_ADDR_MASK);
  if (pd->entries[pd_i] & PTE_P) return -1;
  pd->entries[pd_i] = (phys & ~0x1FFFFFULL) |
                      (flags & (0xFFFULL | PTE_NX)) | PTE_P | PTE_W | PTE_PS;
  if (dir == current_directory)
    asm volatile("invlpg (%0)" : : "r"(virt) : "memory");
  return 0;
}

static int map_direct_page(uint64_t phys) {
  uint64_t virt = DIRECT_MAP_BASE + phys;
  vmm_map_page((void *)phys, (void *)virt, PTE_P | PTE_W | PTE_NX);
  pte_t *entry = walk_pt(current_directory, virt, 0);
  return entry && (*entry & PTE_P) ? 0 : -1;
}

int vmm_map_direct_ram_region(uint64_t phys, uint64_t size) {
  if (!size) return 0;
  uint64_t end = phys + size;
  if (end < phys || end > DIRECT_MAP_SIZE) return -1;
  if (phys < DIRECT_MAP_BOOT_SIZE) phys = DIRECT_MAP_BOOT_SIZE;
  phys = (phys + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
  end &= ~(uint64_t)(PAGE_SIZE - 1);
  if (phys >= end) return 0;

  uint64_t large_start = (phys + 0x1FFFFFULL) & ~0x1FFFFFULL;
  if (large_start > end) large_start = end;
  for (uint64_t page = phys; page < large_start; page += PAGE_SIZE) {
    if (map_direct_page(page) < 0) return -1;
  }

  uint64_t large_end = end & ~0x1FFFFFULL;
  for (uint64_t page = large_start; page < large_end; page += 0x200000ULL) {
    if (vmm_map_large_page(page, DIRECT_MAP_BASE + page,
                           PTE_P | PTE_W | PTE_NX) < 0)
      return -1;
  }

  uint64_t tail_start = large_end;
  if (tail_start < large_start) tail_start = large_start;
  for (uint64_t page = tail_start; page < end; page += PAGE_SIZE) {
    if (map_direct_page(page) < 0) return -1;
  }
  return 0;
}

int vmm_nx_available(void) { return vmm_nx_enabled; }

static void vmm_mark_direct_map_nx(void) {
  if (!vmm_nx_enabled) return;
  pml4_t *pml4 = kernel_directory->pml4_virt;
  uint64_t pml4e = pml4->entries[vaddr_pml4i(DIRECT_MAP_BASE)];
  if (!(pml4e & PTE_P)) return;
  pdp_t *pdp = (pdp_t *)phys_to_virt(pml4e & PTE_ADDR_MASK);
  for (uint64_t i = 0; i < PAGE_TABLE_ENTRIES; i++) {
    if (!(pdp->entries[i] & PTE_P)) continue;
    if (pdp->entries[i] & PTE_PS) {
      pdp->entries[i] |= PTE_NX;
      continue;
    }
    pd_t *pd = (pd_t *)phys_to_virt(pdp->entries[i] & PTE_ADDR_MASK);
    for (uint64_t j = 0; j < PAGE_TABLE_ENTRIES; j++) {
      if (!(pd->entries[j] & PTE_P)) continue;
      if (pd->entries[j] & PTE_PS) {
        pd->entries[j] |= PTE_NX;
        continue;
      }
      pt_t *pt = (pt_t *)phys_to_virt(pd->entries[j] & PTE_ADDR_MASK);
      for (uint64_t k = 0; k < PAGE_TABLE_ENTRIES; k++)
        if (pt->entries[k] & PTE_P) pt->entries[k] |= PTE_NX;
    }
  }
}

/* Change leaf permissions without replacing or freeing the mapped frame. */
int vmm_set_page_flags(void *virt, uint64_t flags) {
  return vmm_set_page_flags_in(current_directory, virt, flags);
}

int vmm_set_page_flags_in(page_directory_t *dir, void *virt,
                          uint64_t flags) {
  if (!dir || !dir->pml4_virt) return -1;
  if (!vmm_nx_enabled) flags &= ~PTE_NX;
  uint64_t v = (uint64_t)virt;
  pte_t *entry = walk_pt(dir, v, 0);
  if (!entry || !(*entry & PTE_P)) return -1;
  *entry = (*entry & ~(0xFFFULL | PTE_NX)) |
           (flags & (0xFFFULL | PTE_NX)) | PTE_P;
  if (dir == current_directory)
    asm volatile("invlpg (%0)" : : "r"(v) : "memory");
  return 0;
}

// Desmapeia uma única página (4 KB) do diretório de páginas ativo e
// libera o frame físico correspondente de volta ao PMM. Útil para munmap.
int vmm_unmap_page(void *virt) {
  return vmm_unmap_page_in(current_directory, virt);
}

static int vmm_unmap_leaf_in(page_directory_t *dir, uint64_t v,
                             int require_user, int free_frame,
                             uint64_t *phys_out) {
  if (!dir || !dir->pml4_virt) return -1;
  pml4_t *pml4 = dir->pml4_virt;
  uint64_t pml4_i = vaddr_pml4i(v);
  uint64_t pdp_i = vaddr_pdpi(v);
  uint64_t pd_i = vaddr_pdi(v);
  uint64_t pt_i = vaddr_pti(v);

  if (!(pml4->entries[pml4_i] & PTE_P) ||
      (require_user && !(pml4->entries[pml4_i] & PTE_U))) return -1;
  pdp_t *pdp = (pdp_t *)phys_to_virt(pml4->entries[pml4_i] & PTE_ADDR_MASK);
  if (!(pdp->entries[pdp_i] & PTE_P) ||
      (require_user && !(pdp->entries[pdp_i] & PTE_U))) return -1;
  pd_t *pd = (pd_t *)phys_to_virt(pdp->entries[pdp_i] & PTE_ADDR_MASK);
  if (!(pd->entries[pd_i] & PTE_P) ||
      (require_user && !(pd->entries[pd_i] & PTE_U))) return -1;

  // Não desmapear dentro de um mapping 2 MB (large page)
  if (pd->entries[pd_i] & PTE_PS) return -1;

  pt_t *pt = (pt_t *)phys_to_virt(pd->entries[pd_i] & PTE_ADDR_MASK);
  if (!(pt->entries[pt_i] & PTE_P) ||
      (require_user && !(pt->entries[pt_i] & PTE_U))) return -1;

  if (dir == current_directory)
    asm volatile("invlpg (%0)" : : "r"(v) : "memory");
  uint64_t phys = pt->entries[pt_i] & PTE_ADDR_MASK;
  pt->entries[pt_i] = 0;
  if (phys_out) *phys_out = phys;
  if (free_frame) pmm_free_block(phys_to_virt(phys));
  return 0;
}

int vmm_unmap_page_in(page_directory_t *dir, void *virt) {
  return vmm_unmap_leaf_in(dir, (uint64_t)virt, 1, 1, 0);
}

static int vmm_unmap_page_keep_frame_in(page_directory_t *dir,
                                        uint64_t virt) {
  return vmm_unmap_leaf_in(dir, virt, 0, 0, 0);
}

int vmm_unmap_kernel_page(uint64_t virt, uint64_t *phys_out) {
  if (!kernel_directory || (virt & (PAGE_SIZE - 1))) return -1;
  uint64_t phys = 0;
  int result = -1;
  uint64_t irq_flags = directory_lock_irqsave();
  if (vmm_unmap_leaf_in(kernel_directory, virt, 0, 0, &phys) == 0) {
    result = 0;
    for (page_directory_t *dir = directory_registry; dir;
         dir = dir->registry_next) {
      if (vmm_unmap_leaf_in(dir, virt, 0, 0, 0) < 0) result = -1;
    }
  }
  directory_unlock_irqrestore(irq_flags);
  if (result == 0 && phys_out) *phys_out = phys;
  return result;
}

void vmm_map_framebuffer(uint64_t phys_addr, uint64_t size) {
  if (!size) return;
  uint64_t offset = phys_addr & 0xFFFULL;
  uint64_t map_start = phys_addr & ~0xFFFULL;
  uint64_t map_size = (offset + size + 0xFFFULL) & ~0xFFFULL;
  for (uint64_t i = 0; i < map_size; i += 4096) {
    /* Map with PTE_PWT to select PAT1 (WC - Write Combining) */
    vmm_map_page((void *)(map_start + i),
                 (void *)(DIRECT_MAP_BASE + map_start + i),
                 PTE_P | PTE_W | PTE_PWT | PTE_NX);
  }
}

void vmm_remove_identity_map(void) {
  if (!kernel_directory || !kernel_directory->pml4_virt) return;
  kernel_directory->pml4_virt->entries[0] = 0;
  uint64_t cr3;
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  asm volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
}

void switch_page_directory(page_directory_t *dir) {
  current_directory = dir;
  asm volatile("mov %0, %%cr3" : : "r"(dir->pml4_phys) : "memory");
}

void init_vmm(uint64_t memory_size) {
  (void)memory_size;
  vmm_init_cpu_protections();
  vmm_init_pat();
  kernel_directory = (page_directory_t *)kmalloc_a(sizeof(page_directory_t));
  memset(kernel_directory, 0, sizeof(page_directory_t));
  uint64_t cr3;
  asm volatile("mov %%cr3, %0" : "=r"(cr3));
  kernel_directory->pml4_phys = cr3;
  kernel_directory->pml4_virt = (pml4_t *)phys_to_virt(cr3);
  kernel_directory->registry_next = 0;
  current_directory = kernel_directory;
}

static void vmm_protect_kernel_range(uint64_t start, uint64_t end,
                                     uint64_t flags) {
  start &= ~(uint64_t)(PAGE_SIZE - 1);
  end = (end + PAGE_SIZE - 1) & ~(uint64_t)(PAGE_SIZE - 1);
  for (uint64_t page = start; page < end; page += PAGE_SIZE) {
    uint64_t phys = virt_to_phys((void *)page);
    vmm_map_page((void *)phys, (void *)page, flags);
  }
}

void vmm_protect_kernel_image(void) {
  extern uint8_t __kernel_text_start[], __kernel_text_end[];
  extern uint8_t __kernel_rodata_start[], __kernel_rodata_end[];
  extern uint8_t __kernel_data_start[], end[];
  if (!vmm_nx_enabled) return;
  vmm_protect_kernel_range((uint64_t)__kernel_text_start,
                           (uint64_t)__kernel_text_end, PTE_P);
  vmm_protect_kernel_range((uint64_t)__kernel_rodata_start,
                           (uint64_t)__kernel_rodata_end,
                           PTE_P | PTE_NX);
  vmm_protect_kernel_range((uint64_t)__kernel_data_start,
                           (uint64_t)end, PTE_P | PTE_W | PTE_NX);
  vmm_mark_direct_map_nx();
}

page_directory_t *vmm_create_directory() {
  page_directory_t *dir = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  if (!dir) return 0;
  memset(dir, 0, sizeof(page_directory_t));
  pml4_t *new_pml4 = (pml4_t *)alloc_pt_page();
  if (!new_pml4) { kfree(dir); return 0; }
  dir->pml4_virt = new_pml4;
  dir->pml4_phys = virt_to_phys(new_pml4);
  pml4_t *kernel_pml4 = kernel_directory->pml4_virt;
  /* Give each address space its own page-table pages. Leaf mappings for
   * supervisor memory still point at the same physical frames. This lets
   * user mappings split large pages without changing the kernel's tables. */
  for (int i = 0; i < 512; i++) {
    if (!(kernel_pml4->entries[i] & PTE_P)) continue;
    pdp_t *new_pdp = (pdp_t *)alloc_pt_page();
    if (!new_pdp) { vmm_free_directory(dir); return 0; }
    new_pml4->entries[i] = virt_to_phys(new_pdp) |
                           (kernel_pml4->entries[i] & 0xFFF & ~PTE_U);
    pdp_t *old_pdp = (pdp_t *)phys_to_virt(kernel_pml4->entries[i] & PTE_ADDR_MASK);
    for (int j = 0; j < 512; j++) {
      if (!(old_pdp->entries[j] & PTE_P)) continue;
      if (old_pdp->entries[j] & PTE_PS) {
        new_pdp->entries[j] = old_pdp->entries[j] & ~PTE_U;
        continue;
      }
      pd_t *new_pd = (pd_t *)alloc_pt_page();
      if (!new_pd) { vmm_free_directory(dir); return 0; }
      new_pdp->entries[j] = virt_to_phys(new_pd) |
                            (old_pdp->entries[j] & 0xFFF & ~PTE_U);
      pd_t *old_pd = (pd_t *)phys_to_virt(old_pdp->entries[j] & PTE_ADDR_MASK);
      for (int k = 0; k < 512; k++) {
        if (!(old_pd->entries[k] & PTE_P)) continue;
        if (old_pd->entries[k] & PTE_PS) {
          new_pd->entries[k] = old_pd->entries[k] & ~PTE_U;
          continue;
        }
        pt_t *new_pt = (pt_t *)alloc_pt_page();
        if (!new_pt) { vmm_free_directory(dir); return 0; }
        new_pd->entries[k] = virt_to_phys(new_pt) |
                             (old_pd->entries[k] & 0xFFF & ~PTE_U);
        pt_t *old_pt = (pt_t *)phys_to_virt(old_pd->entries[k] & PTE_ADDR_MASK);
        memcpy(new_pt, old_pt, PAGE_SIZE);
        for (int l = 0; l < 512; l++) new_pt->entries[l] &= ~PTE_U;
      }
    }
  }
  directory_register(dir);
  return dir;
}

static int copy_user_leaves(page_directory_t *src, page_directory_t *dest) {
  pml4_t *sp = src->pml4_virt;
  for (int i = 0; i < 512; i++) {
    if (!(sp->entries[i] & PTE_P)) continue;
    pdp_t *sdp = (pdp_t *)phys_to_virt(sp->entries[i] & PTE_ADDR_MASK);
    for (int j = 0; j < 512; j++) {
      if (!(sdp->entries[j] & PTE_P) || (sdp->entries[j] & PTE_PS)) continue;
      pd_t *sd = (pd_t *)phys_to_virt(sdp->entries[j] & PTE_ADDR_MASK);
      for (int k = 0; k < 512; k++) {
        if (!(sd->entries[k] & PTE_P)) continue;
        uint64_t va = ((uint64_t)i << 39) | ((uint64_t)j << 30) |
                      ((uint64_t)k << 21);
        if (sd->entries[k] & PTE_PS) {
          if (!(sd->entries[k] & PTE_U)) continue;
          uint64_t phys = sd->entries[k] & PTE_LARGE_ADDR_MASK;
          uint64_t flags = sd->entries[k] & 0xFFFULL & ~PTE_PS;
          for (int l = 0; l < 512; l++) {
            void *page = pmm_alloc_block();
            if (!page) return -1;
            memcpy(page, phys_to_virt(phys + (uint64_t)l * PAGE_SIZE), PAGE_SIZE);
            pte_t *pte = walk_pt(dest, va + (uint64_t)l * PAGE_SIZE,
                                 PTE_P | PTE_U | PTE_W);
            if (!pte) { pmm_free_block(page); return -1; }
            *pte = virt_to_phys(page) | flags |
                   (sd->entries[k] & PTE_NX);
          }
          continue;
        }
        pt_t *st = (pt_t *)phys_to_virt(sd->entries[k] & PTE_ADDR_MASK);
        for (int l = 0; l < 512; l++) {
          if (!(st->entries[l] & PTE_P) || !(st->entries[l] & PTE_U)) continue;
          void *page = pmm_alloc_block();
          if (!page) return -1;
          memcpy(page, phys_to_virt(st->entries[l] & PTE_ADDR_MASK), PAGE_SIZE);
          pte_t *pte = walk_pt(dest, va + (uint64_t)l * PAGE_SIZE,
                               PTE_P | PTE_U | PTE_W);
          if (!pte) { pmm_free_block(page); return -1; }
          *pte = virt_to_phys(page) |
                 (st->entries[l] & (0xFFFULL | PTE_NX));
        }
      }
    }
  }
  return 0;
}

page_directory_t *vmm_copy_directory(page_directory_t *src) {
  if (!src) return NULL;
  page_directory_t *dest = vmm_create_directory();
  if (!dest) return NULL;
  if (copy_user_leaves(src, dest) < 0) {
    vmm_free_directory(dest);
    return NULL;
  }
  if (vma_clone(src, dest) < 0) {
    vmm_free_directory(dest);
    return NULL;
  }
  return dest;
}

void vmm_free_directory(page_directory_t *dir) {
  if (!dir) return;
  directory_unregister(dir);
  vma_destroy(dir);
  pml4_t *pml4 = dir->pml4_virt;
  if (!pml4) {
    kfree(dir);
    return;
  }
  for (int i = 0; i < 512; i++) {
    if (pml4->entries[i] & PTE_P) {
      pdp_t *pdp = (pdp_t *)phys_to_virt(pml4->entries[i] & PTE_ADDR_MASK);
      for (int j = 0; j < 512; j++) {
        if (pdp->entries[j] & PTE_P) {
          if (pdp->entries[j] & PTE_PS) continue;
          pd_t *pd = (pd_t *)phys_to_virt(pdp->entries[j] & PTE_ADDR_MASK);
          for (int k = 0; k < 512; k++) {
            if (pd->entries[k] & PTE_P) {
              if (pd->entries[k] & PTE_PS) {
                /* Large leaves are shared kernel identity mappings. */
              } else {
                pt_t *pt = (pt_t *)phys_to_virt(pd->entries[k] & PTE_ADDR_MASK);
                for (int l = 0; l < 512; l++) {
                  if ((pt->entries[l] & (PTE_P | PTE_U)) == (PTE_P | PTE_U)) {
                    pmm_free_block(phys_to_virt(pt->entries[l] & PTE_ADDR_MASK));
                  }
                }
                pmm_free_block(pt);
              }
            }
          }
          pmm_free_block(pd);
        }
      }
      pmm_free_block(pdp);
    }
  }
  pmm_free_block(pml4);
  kfree(dir);
}

#include "task.h"
uint64_t sys_brk(uint64_t addr) {
  if (!current_task) return 0;
  page_directory_t *mm = current_task->page_directory;
  if (!mm) return 0;
  if (addr == 0) return current_task->heap_end;
  uint64_t brk_top = current_task->heap_end;
  if (brk_top < current_task->heap_start) brk_top = current_task->heap_start;
  if (addr <= brk_top) return brk_top;
  uint64_t start = (brk_top + 0xFFF) & ~0xFFFULL;
  if (addr > USER_ADDR_MAX - PAGE_SIZE) return brk_top;
  uint64_t end = (addr + 0xFFF) & ~0xFFFULL;
  for (uint64_t p = start; p < end; p += 4096) {
    vma_t *existing = vma_find(mm, p);
    if (existing && !(existing->flags & VMA_HEAP)) return brk_top;
    void *phys = pmm_alloc_block();
    if (!phys) {
      for (uint64_t rollback = start; rollback < p; rollback += PAGE_SIZE)
        vmm_unmap_page_in(mm, (void *)rollback);
      return brk_top;
    }
    memset(phys, 0, PAGE_SIZE);
    vmm_map_page_in(mm, phys, (void *)p, PTE_P | PTE_W | PTE_U | PTE_NX);
    if (validate_user_pointer((void *)p, PAGE_SIZE) < 0) {
      pmm_free_block(phys);
      for (uint64_t rollback = start; rollback < p; rollback += PAGE_SIZE)
        vmm_unmap_page_in(mm, (void *)rollback);
      return brk_top;
    }
  }
  uint64_t heap_vma_start = current_task->heap_start & ~(uint64_t)(PAGE_SIZE - 1);
  if (end > heap_vma_start &&
      vma_resize_heap(mm, heap_vma_start, end) < 0) {
    for (uint64_t p = start; p < end; p += PAGE_SIZE)
      vmm_unmap_page_in(mm, (void *)p);
    return brk_top;
  }
  current_task->heap_end = end;
  return current_task->heap_end;
}
