#ifndef ISR_H
#define ISR_H

#include <stdint.h>

/* Estrutura salva pelo dispatcher assembly (common stub) */
typedef struct {
  uint32_t ds;
  uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
  uint32_t int_no, err_code;
  uint32_t eip, cs, eflags, useresp, ss;
} registers_t;

/* Handlers */
void isr_handler(registers_t regs);
uint32_t irq_handler(uint32_t esp);

#endif
