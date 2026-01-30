#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* Estrutura de uma entrada na GDT */
struct gdt_entry_struct {
  uint16_t limit_low;  /* Os 16 bits inferiores do limite */
  uint16_t base_low;   /* Os 16 bits inferiores da base */
  uint8_t base_middle; /* Os próximos 8 bits da base */
  uint8_t access;      /* Flags de acesso */
  uint8_t granularity;
  uint8_t base_high; /* Os últimos 8 bits da base */
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

/* Estrutura do ponteiro GDT (que passamos para lgdt) */
struct gdt_ptr_struct {
  uint16_t limit; /* O limite da tabela (tamanho - 1) */
  uint32_t base;  /* O endereço da primeira entrada */
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

/* TSS Structure */
struct tss_entry_struct {
  uint32_t prev_tss; // The previous TSS - if we used hardware task switching
                     // this would link to the previous task
  uint32_t esp0;     // The stack pointer to load when we change to kernel mode
  uint32_t ss0;      // The stack segment to load when we change to kernel mode
  uint32_t esp1;     // everything below here is unusued now..
  uint32_t ss1;
  uint32_t esp2;
  uint32_t ss2;
  uint32_t cr3;
  uint32_t eip;
  uint32_t eflags;
  uint32_t eax;
  uint32_t ecx;
  uint32_t edx;
  uint32_t ebx;
  uint32_t esp;
  uint32_t ebp;
  uint32_t esi;
  uint32_t edi;
  uint32_t es;
  uint32_t cs;
  uint32_t ss;
  uint32_t ds;
  uint32_t fs;
  uint32_t gs;
  uint32_t ldt;
  uint16_t trap;
  uint16_t iomap_base;
} __attribute__((packed));

typedef struct tss_entry_struct tss_entry_t;

/* Inicializa a GDT */
void init_gdt();
void gdt_set_gate(int32_t num, uint32_t base, uint32_t limit, uint8_t access,
                  uint8_t gran);
void write_tss(int32_t num, uint16_t ss0, uint32_t esp0);

#endif
