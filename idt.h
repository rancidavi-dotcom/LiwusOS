#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* Estrutura de uma entrada na IDT */
struct idt_entry_struct {
    uint16_t base_lo;             /* Os 16 bits inferiores do endereço do handler */
    uint16_t sel;                 /* Seletor de segmento (Kernel Code) */
    uint8_t  always0;             /* Sempre zero */
    uint8_t  flags;               /* Flags (Tipo de porta, Ring, etc) */
    uint16_t base_hi;             /* Os 16 bits superiores do endereço do handler */
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

/* Estrutura do ponteiro IDT */
struct idt_ptr_struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

/* Inicializa a IDT */
void init_idt();

#endif
