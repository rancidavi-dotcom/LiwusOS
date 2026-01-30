#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include <stdint.h>

void init_syscalls();
void syscall_handler(registers_t *regs);

#endif
