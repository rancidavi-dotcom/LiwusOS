#include "vma.h"
#include "vmm.h"
#include "kheap.h"
#include "usercopy.h"

static int contiguous_offset(uint64_t offset, uint64_t length,
                             uint64_t next_offset) {
  return length <= UINT64_MAX - offset && offset + length == next_offset;
}

static int valid_interval(uint64_t start, uint64_t end) {
  return start >= USER_ADDR_MIN && start < end && end <= USER_ADDR_MAX &&
         !(start & (PAGE_SIZE - 1)) && !(end & (PAGE_SIZE - 1));
}

static vma_t *vma_allocate(uint64_t start, uint64_t end, uint32_t prot,
                           uint32_t flags, uint64_t file_offset,
                           void *backing) {
  vma_t *area = (vma_t *)kmalloc(sizeof(*area));
  if (!area) return 0;
  area->start = start;
  area->end = end;
  area->prot = prot;
  area->flags = flags;
  area->file_offset = file_offset;
  area->backing = backing;
  area->next = 0;
  return area;
}

vma_t *vma_find(struct page_directory *mm, uint64_t address) {
  if (!mm) return 0;
  for (vma_t *area = mm->vmas; area; area = area->next) {
    if (address < area->start) return 0;
    if (address < area->end) return area;
  }
  return 0;
}

int vma_insert(struct page_directory *mm, uint64_t start, uint64_t end,
               uint32_t prot, uint32_t flags, uint64_t file_offset,
               void *backing) {
  if (!mm || !valid_interval(start, end)) return -1;
  vma_t **link = &mm->vmas;
  vma_t *prev = 0;
  while (*link && (*link)->start < start) {
    prev = *link;
    link = &(*link)->next;
  }
  if ((prev && prev->end > start) || (*link && end > (*link)->start))
    return -1;

  if (prev && prev->end == start && prev->prot == prot &&
      prev->flags == flags && prev->backing == backing &&
      contiguous_offset(prev->file_offset, prev->end - prev->start,
                        file_offset)) {
    prev->end = end;
    if (prev->next && prev->next->start == end &&
        prev->next->prot == prot && prev->next->flags == flags &&
        prev->next->backing == backing &&
        contiguous_offset(file_offset, end - start,
                          prev->next->file_offset)) {
      vma_t *merged = prev->next;
      prev->end = merged->end;
      prev->next = merged->next;
      kfree(merged);
    }
    return 0;
  }
  if (*link && end == (*link)->start && (*link)->prot == prot &&
      (*link)->flags == flags && (*link)->backing == backing &&
      contiguous_offset(file_offset, end - start, (*link)->file_offset)) {
    (*link)->start = start;
    (*link)->file_offset = file_offset;
    return 0;
  }

  vma_t *area = vma_allocate(start, end, prot, flags, file_offset, backing);
  if (!area) return -1;
  area->next = *link;
  *link = area;
  return 0;
}

int vma_remove(struct page_directory *mm, uint64_t start, uint64_t end) {
  if (!mm || !valid_interval(start, end)) return -1;
  vma_t *split_area = 0;
  vma_t *split_right = 0;
  for (vma_t *area = mm->vmas; area; area = area->next) {
    if (area->start < start && end < area->end) {
      split_area = area;
      split_right = vma_allocate(end, area->end, area->prot, area->flags,
                                 area->file_offset + (end - area->start),
                                 area->backing);
      if (!split_right) return -1;
      split_right->next = area->next;
      break;
    }
  }

  vma_t **link = &mm->vmas;
  while (*link) {
    vma_t *area = *link;
    if (area->end <= start || area->start >= end) {
      link = &area->next;
      continue;
    }
    if (start <= area->start && end >= area->end) {
      *link = area->next;
      kfree(area);
      continue;
    }
    if (start <= area->start) {
      uint64_t delta = end - area->start;
      area->start = end;
      area->file_offset += delta;
      break;
    }
    if (end < area->end) {
      if (area != split_area || !split_right) return -1;
      area->next = split_right;
      area->end = start;
      split_right = 0;
      break;
    }
    area->end = start;
    link = &area->next;
  }
  if (split_right) kfree(split_right);
  return 0;
}

/* Replace protection metadata for a fully covered interval. Build a new VMA
 * list first so allocation failures leave the old map intact. The page-table
 * permission changes are applied only after all mappings and metadata have
 * been validated/allocated. */
int vma_protect(struct page_directory *mm, uint64_t start, uint64_t end,
                uint32_t prot) {
  if (!mm || !valid_interval(start, end)) return -1;

  uint64_t covered = start;
  for (vma_t *area = mm->vmas; area && covered < end; area = area->next) {
    if (area->end <= covered) continue;
    if (area->start > covered) return -1;
    covered = area->end < end ? area->end : end;
  }
  if (covered != end) return -1;

  for (uint64_t page = start; page < end; page += PAGE_SIZE) {
    if (!vmm_page_present_in(mm, page)) return -1;
  }

  struct page_directory replacement = {0};
  for (vma_t *area = mm->vmas; area; area = area->next) {
    if (area->end <= start || area->start >= end) {
      if (vma_insert(&replacement, area->start, area->end, area->prot,
                     area->flags, area->file_offset, area->backing) < 0)
        goto allocation_failure;
      continue;
    }

    if (area->start < start &&
        vma_insert(&replacement, area->start, start, area->prot, area->flags,
                   area->file_offset, area->backing) < 0)
      goto allocation_failure;

    uint64_t middle_start = area->start > start ? area->start : start;
    uint64_t middle_end = area->end < end ? area->end : end;
    uint64_t middle_offset = area->file_offset + (middle_start - area->start);
    if (vma_insert(&replacement, middle_start, middle_end, prot, area->flags,
                   middle_offset, area->backing) < 0)
      goto allocation_failure;

    if (area->end > end &&
        vma_insert(&replacement, end, area->end, area->prot, area->flags,
                   area->file_offset + (end - area->start),
                   area->backing) < 0)
      goto allocation_failure;
  }

  uint64_t pte_flags = PTE_P | PTE_U | PTE_NX;
  if (prot & VMA_PROT_WRITE) pte_flags |= PTE_W;
  if (prot & VMA_PROT_EXEC) pte_flags &= ~((uint64_t)PTE_NX);
  for (uint64_t page = start; page < end; page += PAGE_SIZE) {
    if (vmm_set_page_flags_in(mm, (void *)page, pte_flags) < 0) {
      vma_destroy(&replacement);
      return -1;
    }
  }

  vma_t *old = mm->vmas;
  mm->vmas = replacement.vmas;
  replacement.vmas = 0;
  while (old) {
    vma_t *next = old->next;
    kfree(old);
    old = next;
  }
  return 0;

allocation_failure:
  vma_destroy(&replacement);
  return -1;
}

int vma_resize_heap(struct page_directory *mm, uint64_t start, uint64_t end) {
  if (!mm || start < USER_ADDR_MIN || end < start || end > USER_ADDR_MAX ||
      (start & (PAGE_SIZE - 1)) || (end & (PAGE_SIZE - 1))) return -1;
  vma_t **heap_link = 0;
  vma_t *heap = 0;
  for (vma_t **link = &mm->vmas; *link; link = &(*link)->next) {
    if ((*link)->flags & VMA_HEAP) {
      if (heap) return -1;
      heap = *link;
      heap_link = link;
    }
  }
  if (start == end) {
    if (heap) {
      *heap_link = heap->next;
      kfree(heap);
    }
    return 0;
  }
  if (!valid_interval(start, end)) return -1;
  for (vma_t *area = mm->vmas; area; area = area->next) {
    if (area == heap) continue;
    if (start < area->end && end > area->start) return -1;
  }
  if (!heap)
    return vma_insert(mm, start, end,
                      VMA_PROT_READ | VMA_PROT_WRITE,
                      VMA_PRIVATE | VMA_ANON | VMA_HEAP, 0, 0);

  *heap_link = heap->next;
  heap->start = start;
  heap->end = end;
  vma_t **insert = &mm->vmas;
  while (*insert && (*insert)->start < start) insert = &(*insert)->next;
  heap->next = *insert;
  *insert = heap;
  return 0;
}

int vma_clone(struct page_directory *src, struct page_directory *dst) {
  if (!src || !dst) return -1;
  for (vma_t *area = src->vmas; area; area = area->next) {
    if (vma_insert(dst, area->start, area->end, area->prot, area->flags,
                   area->file_offset, area->backing) < 0) {
      vma_destroy(dst);
      return -1;
    }
  }
  return 0;
}

void vma_destroy(struct page_directory *mm) {
  if (!mm) return;
  vma_t *area = mm->vmas;
  mm->vmas = 0;
  while (area) {
    vma_t *next = area->next;
    kfree(area);
    area = next;
  }
}

int vma_find_gap_topdown(struct page_directory *mm, uint64_t top,
                         uint64_t bottom, uint64_t length,
                         uint64_t *base_out) {
  if (!mm || !base_out || !length || bottom < USER_ADDR_MIN ||
      top > USER_ADDR_MAX || bottom >= top || length > top - bottom ||
      (bottom & (PAGE_SIZE - 1)) || (top & (PAGE_SIZE - 1)) ||
      (length & (PAGE_SIZE - 1)))
    return -1;
  uint64_t candidate = (top - length) & ~(uint64_t)(PAGE_SIZE - 1);
  while (candidate >= bottom) {
    int collision = 0;
    uint64_t next_candidate = candidate;
    for (vma_t *area = mm->vmas; area; area = area->next) {
      if (candidate < area->end && candidate + length > area->start) {
        collision = 1;
        if (area->start < length || area->start - length < bottom) break;
        next_candidate = (area->start - length) & ~(uint64_t)(PAGE_SIZE - 1);
        break;
      }
    }
    if (!collision) {
      *base_out = candidate;
      return 0;
    }
    if (next_candidate >= candidate) return -1;
    candidate = next_candidate;
  }
  return -1;
}
