#ifndef ISR_H
#define ISR_H

#include <stdint.h>

typedef struct {
  uint64_t rax, rcx, rdx, rbx;
  uint64_t rbp, rsi, rdi;
  uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
  uint64_t int_no, err_code;
  uint64_t rip, cs, rflags, rsp, ss;
} registers_t;

/* Handlers */
void isr_handler(registers_t *regs);
uint64_t irq_handler(uint64_t rsp);

#endif
