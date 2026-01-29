#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* Estrutura de uma entrada na GDT */
struct gdt_entry_struct {
    uint16_t limit_low;     /* Os 16 bits inferiores do limite */
    uint16_t base_low;      /* Os 16 bits inferiores da base */
    uint8_t  base_middle;   /* Os próximos 8 bits da base */
    uint8_t  access;        /* Flags de acesso */
    uint8_t  granularity;
    uint8_t  base_high;     /* Os últimos 8 bits da base */
} __attribute__((packed));

typedef struct gdt_entry_struct gdt_entry_t;

/* Estrutura do ponteiro GDT (que passamos para lgdt) */
struct gdt_ptr_struct {
    uint16_t limit;         /* O limite da tabela (tamanho - 1) */
    uint32_t base;          /* O endereço da primeira entrada */
} __attribute__((packed));

typedef struct gdt_ptr_struct gdt_ptr_t;

/* Inicializa a GDT */
void init_gdt();

#endif
