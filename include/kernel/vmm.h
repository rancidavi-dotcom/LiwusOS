#ifndef VMM_H
#define VMM_H

#include <stdint.h>
#include <stdbool.h>

#define PAGE_SIZE 4096
#define PAGE_TABLE_ENTRIES 512

#define PTE_P        (1ULL << 0)
#define PTE_W        (1ULL << 1)
#define PTE_U        (1ULL << 2)
#define PTE_PWT      (1ULL << 3)
#define PTE_PCD      (1ULL << 4)
#define PTE_A        (1ULL << 5)
#define PTE_D        (1ULL << 6)
#define PTE_PS       (1ULL << 7)
#define PTE_G        (1ULL << 8)
#define PTE_NX       (1ULL << 63)

typedef uint64_t pte_t;

typedef struct {
  pte_t entries[512];
} __attribute__((aligned(4096))) pml4_t;

typedef struct {
  pte_t entries[512];
} __attribute__((aligned(4096))) pdp_t;

typedef struct {
  pte_t entries[512];
} __attribute__((aligned(4096))) pd_t;

typedef struct {
  pte_t entries[512];
} __attribute__((aligned(4096))) pt_t;

typedef struct {
  pml4_t *pml4_virt;
  uint64_t pml4_phys;
} page_directory_t;

static inline uint64_t vaddr_pml4i(uint64_t vaddr) { return (vaddr >> 39) & 0x1FF; }
static inline uint64_t vaddr_pdpi(uint64_t vaddr)  { return (vaddr >> 30) & 0x1FF; }
static inline uint64_t vaddr_pdi(uint64_t vaddr)   { return (vaddr >> 21) & 0x1FF; }
static inline uint64_t vaddr_pti(uint64_t vaddr)   { return (vaddr >> 12) & 0x1FF; }

void init_vmm(uint64_t memory_size);
void vmm_map_page(void *phys, void *virt, uint64_t flags);
void vmm_map_framebuffer(uint64_t phys_addr, uint64_t size);
void switch_page_directory(page_directory_t *dir);
page_directory_t *vmm_create_directory();
page_directory_t *vmm_copy_directory(page_directory_t *src);
void vmm_free_directory(page_directory_t *dir);
uint64_t sys_brk(uint64_t addr);

#endif
