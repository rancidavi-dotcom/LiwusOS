#ifndef GDT_H
#define GDT_H

#include <stdint.h>

#define GDT_ENTRIES 9

struct gdt_entry_struct {
  uint16_t limit_low;
  uint16_t base_low;
  uint8_t base_middle;
  uint8_t access;
  uint8_t granularity;
  uint8_t base_high;
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

struct gdt_ptr_struct {
  uint16_t limit;
  uint64_t base;
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

struct tss_entry_struct {
  uint32_t reserved0;
  uint64_t rsp0;
  uint64_t rsp1;
  uint64_t rsp2;
  uint64_t reserved1;
  uint64_t ist1;
  uint64_t ist2;
  uint64_t ist3;
  uint64_t ist4;
  uint64_t ist5;
  uint64_t ist6;
  uint64_t ist7;
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iomap_base;
} __attribute__((packed));

typedef struct tss_entry_struct tss_entry_t;

void init_gdt();
void gdt_set_gate(int32_t num, uint64_t base, uint32_t limit, uint8_t access,
                  uint8_t gran);
void write_tss(int32_t num, uint16_t ss0, uint64_t rsp0);

void init_gdt_cpu(int cpu_id);
extern tss_entry_t cpus_tss[16];

#endif
