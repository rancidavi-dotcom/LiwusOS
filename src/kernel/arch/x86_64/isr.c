#include "isr.h"
#include "io.h"
#include "serial.h"
#include "syscall.h" // Para syscall_handler
#include "task.h"
#include "string.h"
extern void keyboard_handler();
extern void timer_handler();
extern void mouse_handler();
extern uint64_t schedule(uint64_t current_rsp);


/* Handler para exceções de CPU (ISR 0-31) e Syscalls (128) */
/* O Assembly chama 'call isr_handler', passando a struct registers_t na stack
 * (por valor) */
void isr_handler(registers_t *regs) {
  if (regs->int_no == 128) {
    syscall_handler(regs);
  } else {
    serial_print("CPU exception: ");
    serial_print_hex(regs->int_no);
    serial_print(" err=");
    serial_print_hex(regs->err_code);
    if (regs->int_no == 14) {
      uint64_t cr2;
      asm volatile("mov %%cr2, %0" : "=r"(cr2));
      serial_print(" cr2=");
      serial_print_hex(cr2);
    }
serial_print(" rip=");
      serial_print_hex(regs->rip);
      serial_print(" rsp=");
      serial_print_hex(regs->rsp);
      serial_print(" rax=");
      serial_print_hex(regs->rax);
      serial_print(" rbx=");
      serial_print_hex(regs->rbx);
      serial_print(" rcx=");
      serial_print_hex(regs->rcx);
      serial_print(" rdx=");
      serial_print_hex(regs->rdx);
      serial_print(" rdi=");
      serial_print_hex(regs->rdi);
      serial_print(" rsi=");
      serial_print_hex(regs->rsi);
      serial_print(" rbp=");
      serial_print_hex(regs->rbp);
      serial_print("\n");

      if ((regs->cs & 0x3) == 0x3 && current_task && current_task->user_mode) {
        serial_print("user task killed after exception\n");
        sys_exit_process(128 + (int)regs->int_no);
      } else {
      extern void kernel_panic(const char *msg);
      char buf[64];
      strcpy(buf, "Unhandled CPU exception (ISR ");
      char num[16];
      itoa(regs->int_no, num, 10);
      strcat(buf, num);
      strcat(buf, ")");
      kernel_panic(buf);
    }
  }
}

/* Handler para interrupções de hardware (IRQ 0-15) */
/* O Assembly 'irq_common_stub' faz 'push %esp' e 'call irq_handler', então
 * recebe um ponteiro */
uint64_t irq_handler(uint64_t rsp) {
  registers_t *regs = (registers_t *)rsp;

  extern void lapic_eoi(void);
  lapic_eoi();

  if (regs->int_no == 32) {
    timer_handler();
    return schedule(rsp);
  } else if (regs->int_no == 33) {
    keyboard_handler();
  } else if (regs->int_no == 43) { // IRQ 11 (Network)

  } else if (regs->int_no == 44) {
    mouse_handler();
  }

  return rsp;
}
