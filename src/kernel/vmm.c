#include "vmm.h"
#include "kheap.h"
#include "pmm.h"
#include "string.h"
#include "serial.h"

page_directory_t *kernel_directory = 0;
page_directory_t *current_directory = 0;

extern void load_page_directory(uint32_t *);
extern void enable_paging();

/* Helpers */
void switch_page_directory(page_directory_t *dir) {
  current_directory = dir;
  load_page_directory((uint32_t *)dir->physicalAddr);
}

void vmm_map_page(void *phys, void *virt, uint32_t flags) {
  page_directory_t *dir = current_directory;
  uint32_t pd_index = (uint32_t)virt >> 22;
  uint32_t pt_index = ((uint32_t)virt >> 12) & 0x03FF;

  if (!dir->tablesVirtual[pd_index]) {
    uint32_t *new_table = (uint32_t *)kmalloc_a(4096);
    uint32_t phys_table = (uint32_t)new_table;
    memset(new_table, 0, 4096);

    dir->tablesVirtual[pd_index] = new_table;
    dir->tablesPhysical[pd_index] = phys_table | 0x7; // PRESENT, RW, US
    ((uint32_t*)dir->physicalAddr)[pd_index] = phys_table | 0x7;
  }

  uint32_t *table = dir->tablesVirtual[pd_index];
  table[pt_index] = ((uint32_t)phys) | (flags & 0xFFF) | 0x1; // Present
  
  asm volatile("invlpg (%0)" ::"r" (virt) : "memory");
}

void init_vmm() {
  kernel_directory = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  memset(kernel_directory, 0, sizeof(page_directory_t));

  uint32_t phys_addr;
  kernel_directory->physicalAddr = (uint32_t)kmalloc_ap(4096, &phys_addr);
  memset((void *)kernel_directory->physicalAddr, 0, 4096);

  uint32_t *pd = (uint32_t *)kernel_directory->physicalAddr;

  for (int i = 0; i < 64 * 1024 * 1024; i += 4096) {
    uint32_t pd_index = i >> 22;
    uint32_t pt_index = (i >> 12) & 0x03FF;

    if (!kernel_directory->tablesVirtual[pd_index]) {
      uint32_t *table = (uint32_t *)kmalloc_a(4096);
      kernel_directory->tablesVirtual[pd_index] = table;
      pd[pd_index] = ((uint32_t)table) | 0x3;
      memset(table, 0, 4096);
    }
    kernel_directory->tablesVirtual[pd_index][pt_index] = i | 0x3;
  }

  uint32_t fb_phys = 0xFD000000;
  for (int i = 0; i < 16 * 1024 * 1024; i += 4096) {
    uint32_t addr = fb_phys + i;
    uint32_t pd_index = addr >> 22;
    uint32_t pt_index = (addr >> 12) & 0x03FF;

    if (!kernel_directory->tablesVirtual[pd_index]) {
      uint32_t *table = (uint32_t *)kmalloc_a(4096);
      kernel_directory->tablesVirtual[pd_index] = table;
      pd[pd_index] = ((uint32_t)table) | 0x3;
      memset(table, 0, 4096);
    }
    kernel_directory->tablesVirtual[pd_index][pt_index] = addr | 0x3;
  }

  current_directory = kernel_directory;
  load_page_directory((uint32_t *)kernel_directory->physicalAddr);
  enable_paging();
}

page_directory_t *vmm_create_directory() {
  page_directory_t *dir = (page_directory_t *)kmalloc(sizeof(page_directory_t));
  memset(dir, 0, sizeof(page_directory_t));
  uint32_t phys_addr;
  dir->physicalAddr = (uint32_t)kmalloc_ap(4096, &phys_addr);
  memset((void *)dir->physicalAddr, 0, 4096);
  for (int i = 0; i < 1024; i++) {
    if (kernel_directory->tablesVirtual[i]) {
      dir->tablesVirtual[i] = kernel_directory->tablesVirtual[i];
      uint32_t *pd = (uint32_t *)dir->physicalAddr;
      uint32_t *k_pd = (uint32_t *)kernel_directory->physicalAddr;
      pd[i] = k_pd[i];
    }
  }
  return dir;
}

page_directory_t *vmm_copy_directory(page_directory_t *src) {
  page_directory_t *dir = vmm_create_directory();
  for (int i = 0; i < 1024; i++) {
    if (src->tablesVirtual[i] && src->tablesVirtual[i] != kernel_directory->tablesVirtual[i]) {
      uint32_t *old_table = src->tablesVirtual[i];
      uint32_t *new_table = (uint32_t *)kmalloc_a(4096);
      dir->tablesVirtual[i] = new_table;
      dir->tablesPhysical[i] = (uint32_t)new_table | 0x7;
      memset(new_table, 0, 4096);
      for (int j = 0; j < 1024; j++) {
        if (old_table[j] & 0x1) {
          uint32_t flags = old_table[j] & 0xFFF;
          uint32_t phys = old_table[j] & 0xFFFFF000;
          void *new_phys = kmalloc_a(4096);
          memcpy(new_phys, (void *)(phys), 4096);
          new_table[j] = (uint32_t)new_phys | flags;
        }
      }
    }
  }
  return dir;
}

#include "task.h"
extern task_t *current_task;
uint32_t sys_brk(uint32_t addr) {
  if (!current_task) return 0;
  if (addr == 0 || addr < current_task->heap_start) return current_task->heap_end;
  if (addr > current_task->heap_end) {
    uint32_t start = (current_task->heap_end + 0xFFF) & 0xFFFFF000;
    uint32_t end = (addr + 0xFFF) & 0xFFFFF000;
    for (uint32_t p = start; p < end; p += 4096) {
      void *phys = kmalloc_a(4096);
      vmm_map_page(phys, (void *)p, 0x7);
      memset((void *)p, 0, 4096);
    }
    current_task->heap_end = addr;
  }
  return current_task->heap_end;
}