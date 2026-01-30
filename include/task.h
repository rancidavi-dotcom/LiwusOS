#ifndef TASK_H
#define TASK_H

#include "vmm.h" // page_directory_t
#include <stdint.h>

typedef enum {
  TASK_RUNNING,
  TASK_READY,
  TASK_SLEEPING,
  TASK_ZOMBIE
} task_state_t;

typedef struct task {
  int id;
  task_state_t state;
  int exit_code;
  uint32_t stack_top;    // saved ESP
  uint32_t kernel_stack; // kernel stack base
  struct task *parent;
  struct task *next;
  page_directory_t *page_directory;
  uint32_t heap_start;
  uint32_t heap_end;
} task_t;

void init_tasking();
void create_task(void (*entry_point)());
void switch_task();
uint32_t schedule(uint32_t current_esp);
void move_to_user_mode();

/* Syscall Helpers */
// Registers struct do isr.h
#include "isr.h"
int fork_process(registers_t *regs);
int sys_waitpid(int pid, int *status, int options);
void sys_exit_process(int status);

#endif
