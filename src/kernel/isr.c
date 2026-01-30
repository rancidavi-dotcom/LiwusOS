#include "isr.h"
#include "io.h"
#include "syscall.h" // Para syscall_handler

extern void keyboard_handler();
extern void timer_handler();
extern void mouse_handler();
extern uint32_t schedule(uint32_t current_esp);
extern void rtl8139_handler();

/* Handler para exceções de CPU (ISR 0-31) e Syscalls (128) */
/* O Assembly chama 'call isr_handler', passando a struct registers_t na stack
 * (por valor) */
void isr_handler(registers_t regs) {
  if (regs.int_no == 128) {
    syscall_handler(&regs);
  } else {
    // Exceção (GPF, Page Fault, etc) - Ignorar ou printar
    // Se for interrupt 14 (Page Fault), seria critico.
  }
}

/* Handler para interrupções de hardware (IRQ 0-15) */
/* O Assembly 'irq_common_stub' faz 'push %esp' e 'call irq_handler', então
 * recebe um ponteiro */
uint32_t irq_handler(uint32_t esp) {
  registers_t *regs = (registers_t *)esp;

  if (regs->int_no >= 40)
    outb(0xA0, 0x20);
  outb(0x20, 0x20);

  if (regs->int_no == 32) {
    timer_handler();
    return schedule(esp);
  } else if (regs->int_no == 33) {
    keyboard_handler();
  } else if (regs->int_no == 43) { // IRQ 11 (Network)
    rtl8139_handler();
  } else if (regs->int_no == 44) {
    mouse_handler();
  }

  return esp;
}