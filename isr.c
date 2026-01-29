#include <stdint.h>
#include "io.h"

struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} __attribute__((packed));

typedef struct registers registers_t;

extern void keyboard_handler();
extern void timer_handler();
extern void mouse_handler();
extern uint32_t schedule(uint32_t current_esp);

/* Handler para exceções de CPU (ISR 0-31) */
void isr_handler(registers_t regs) {
    /* Por enquanto apenas para evitar erro de linkagem */
}

extern void rtl8139_handler();

/* Handler para interrupções de hardware (IRQ 0-15) */
uint32_t irq_handler(uint32_t esp) {
    registers_t* regs = (registers_t*)esp;

    if (regs->int_no >= 40) outb(0xA0, 0x20);
    outb(0x20, 0x20);

    if (regs->int_no == 32) {
        timer_handler();
        return schedule(esp);
    }
    else if (regs->int_no == 33) {
        keyboard_handler();
    }
    else if (regs->int_no == 43) { // IRQ 11 (Network)
        rtl8139_handler();
    }
    else if (regs->int_no == 44) {
        mouse_handler();
    }

    return esp;
}