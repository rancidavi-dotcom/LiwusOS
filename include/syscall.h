#ifndef SYSCALL_H
#define SYSCALL_H

#include "isr.h"
#include <stdint.h>

void init_syscalls();
void syscall_handler(registers_t *regs);
int launch_initrd_program(const char *filename);
int launch_initrd_program_argv(const char *filename, char *const argv[]);
const char *get_launch_last_error(void);
int graphics_exclusive_active(void);
void graphics_exclusive_release(int owner_pid);

#endif
