#include "gdt.h"
#include "string.h"

extern void gdt_flush(gdt_ptr_t *);
extern void tss_flush();

gdt_entry_t cpus_gdt[16][GDT_ENTRIES];
gdt_ptr_t cpus_gdt_ptr[16];
tss_entry_t cpus_tss[16];

gdt_ptr_t gdt_ptr;

#define gdt_entries (cpus_gdt[0])
#define tss_entry (cpus_tss[0])

void gdt_set_gate(int32_t num, uint64_t base, uint32_t limit, uint8_t access,
                  uint8_t gran) {
  gdt_entries[num].base_low = (base & 0xFFFF);
  gdt_entries[num].base_middle = (base >> 16) & 0xFF;
  gdt_entries[num].base_high = (base >> 24) & 0xFF;

  gdt_entries[num].limit_low = (limit & 0xFFFF);
  gdt_entries[num].granularity = (limit >> 16) & 0x0F;

  gdt_entries[num].granularity |= gran & 0xF0;
  gdt_entries[num].access = access;
}

void write_tss(int32_t num, uint16_t ss0, uint64_t rsp0) {
  (void)ss0;
  uint64_t base = (uint64_t)&tss_entry;
  uint32_t limit = sizeof(tss_entry) - 1; /* limit = size - 1 */

  memset(&tss_entry, 0, sizeof(tss_entry));

  tss_entry.rsp0 = rsp0;
  tss_entry.iomap_base = sizeof(tss_entry);

  /* Build the TSS descriptor (16 bytes for x86_64 long mode) */
  /* First 8 bytes: standard descriptor */
  gdt_entries[num].limit_low = limit & 0xFFFF;
  gdt_entries[num].base_low = base & 0xFFFF;
  gdt_entries[num].base_middle = (base >> 16) & 0xFF;
  gdt_entries[num].access = 0x89;  /* P=1, DPL=0, S=0, type=9 */
  gdt_entries[num].granularity = ((limit >> 16) & 0x0F) | 0x00;
  gdt_entries[num].base_high = (base >> 24) & 0xFF;

  /* Second 8 bytes: upper base bits, reserved */
  uint64_t *high = (uint64_t *)&gdt_entries[num + 1];
  *high = (base >> 32);  /* bits 63:32 of base, reserved must be 0 */
}

static void write_gdt_gate(gdt_entry_t *gdt, int32_t num, uint64_t base, uint32_t limit, uint8_t access, uint8_t gran) {
  gdt[num].base_low = (base & 0xFFFF);
  gdt[num].base_middle = (base >> 16) & 0xFF;
  gdt[num].base_high = (base >> 24) & 0xFF;

  gdt[num].limit_low = (limit & 0xFFFF);
  gdt[num].granularity = (limit >> 16) & 0x0F;
  gdt[num].granularity |= gran & 0xF0;
  gdt[num].access = access;
}

static void write_gdt_tss(gdt_entry_t *gdt, tss_entry_t *tss, int32_t num, uint64_t rsp0) {
  uint64_t base = (uint64_t)tss;
  uint32_t limit = sizeof(tss_entry_t) - 1;

  memset(tss, 0, sizeof(tss_entry_t));
  tss->rsp0 = rsp0;
  tss->iomap_base = sizeof(tss_entry_t);

  gdt[num].limit_low = limit & 0xFFFF;
  gdt[num].base_low = base & 0xFFFF;
  gdt[num].base_middle = (base >> 16) & 0xFF;
  gdt[num].access = 0x89; // P=1, DPL=0, S=0, type=9
  gdt[num].granularity = ((limit >> 16) & 0x0F) | 0x00;
  gdt[num].base_high = (base >> 24) & 0xFF;

  uint64_t *high = (uint64_t *)&gdt[num + 1];
  *high = (base >> 32);
}

void init_gdt_cpu(int cpu_id) {
  if (cpu_id < 0 || cpu_id >= 16) return;
  gdt_entry_t *gdt = cpus_gdt[cpu_id];
  gdt_ptr_t *my_gdt_ptr = &cpus_gdt_ptr[cpu_id];
  tss_entry_t *my_tss = &cpus_tss[cpu_id];

  my_gdt_ptr->limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
  my_gdt_ptr->base = (uint64_t)gdt;

  write_gdt_gate(gdt, 0, 0, 0, 0, 0);
  write_gdt_gate(gdt, 1, 0, 0xFFFFFFFF, 0x9A, 0xAF);
  write_gdt_gate(gdt, 2, 0, 0xFFFFFFFF, 0x92, 0x8F);
  write_gdt_gate(gdt, 3, 0, 0xFFFFFFFF, 0xFA, 0xCF);
  write_gdt_gate(gdt, 4, 0, 0xFFFFFFFF, 0xF2, 0xCF);
  write_gdt_gate(gdt, 5, 0, 0xFFFFFFFF, 0xFA, 0xAF);
  write_gdt_gate(gdt, 6, 0, 0xFFFFFFFF, 0xF2, 0x8F);
  write_gdt_tss(gdt, my_tss, 7, 0);

  if (cpu_id == 0) {
    gdt_ptr = *my_gdt_ptr;
  }

  gdt_flush(my_gdt_ptr);
}

void init_gdt() {
  init_gdt_cpu(0);
}
