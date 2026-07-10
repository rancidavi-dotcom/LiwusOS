#include "gdt.h"
#include "string.h"

extern void gdt_flush(gdt_ptr_t *);
extern void tss_flush();

gdt_entry_t gdt_entries[GDT_ENTRIES];
gdt_ptr_t gdt_ptr;
tss_entry_t tss_entry;

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

void init_gdt() {
  gdt_ptr.limit = (sizeof(gdt_entry_t) * GDT_ENTRIES) - 1;
  gdt_ptr.base = (uint64_t)&gdt_entries;

  /* 0x00: Null */
  gdt_set_gate(0, 0, 0, 0, 0);

  /* 0x08: Kernel code 64-bit (L=1, D=0) */
  gdt_set_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xAF);

  /* 0x10: Kernel data 64-bit */
  gdt_set_gate(2, 0, 0xFFFFFFFF, 0x92, 0x8F);

  /* 0x18: User code 32-bit compat (D=1, L=0) */
  gdt_set_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF);

  /* 0x20: User data 32-bit */
  gdt_set_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF);

  /* 0x28: User code 64-bit (L=1, D=0) */
  gdt_set_gate(5, 0, 0xFFFFFFFF, 0xFA, 0xAF);

  /* 0x30: User data 64-bit */
  gdt_set_gate(6, 0, 0xFFFFFFFF, 0xF2, 0x8F);

  /* 0x38: TSS (entry 7), initially with RSP0 = 0. Updated per task switch */
  write_tss(7, 0x10, 0);

  gdt_flush(&gdt_ptr);
  /* tss_flush() called later from kernel_main (needs IDT active) */
}
