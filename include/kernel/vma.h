#ifndef KERNEL_VMA_H
#define KERNEL_VMA_H

#include <stdint.h>

struct page_directory;

#define VMA_PROT_READ  0x1U
#define VMA_PROT_WRITE 0x2U
#define VMA_PROT_EXEC  0x4U

#define VMA_PRIVATE 0x0001U
#define VMA_SHARED  0x0002U
#define VMA_ANON    0x0004U
#define VMA_FILE    0x0008U
#define VMA_ELF     0x0010U
#define VMA_STACK   0x0020U
#define VMA_HEAP    0x0040U

typedef struct vma {
  uint64_t start;
  uint64_t end; /* half-open */
  uint64_t file_offset;
  uint32_t prot;
  uint32_t flags;
  void *backing;
  struct vma *next;
} vma_t;

int vma_insert(struct page_directory *mm, uint64_t start, uint64_t end,
               uint32_t prot, uint32_t flags, uint64_t file_offset,
               void *backing);
int vma_remove(struct page_directory *mm, uint64_t start, uint64_t end);
int vma_protect(struct page_directory *mm, uint64_t start, uint64_t end,
                uint32_t prot);
int vma_resize_heap(struct page_directory *mm, uint64_t start, uint64_t end);
int vma_clone(struct page_directory *src, struct page_directory *dst);
void vma_destroy(struct page_directory *mm);
vma_t *vma_find(struct page_directory *mm, uint64_t address);
int vma_find_gap_topdown(struct page_directory *mm, uint64_t top,
                         uint64_t bottom, uint64_t length,
                         uint64_t *base_out);

#endif
