#ifndef TASK_H
#define TASK_H

#include "vmm.h" // page_directory_t
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  TASK_RUNNING,
  TASK_READY,
  TASK_SLEEPING,
  TASK_ZOMBIE
} task_state_t;

#include "vfs.h"

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
  uint32_t cpu_ticks;
  uint32_t switch_count;
  bool user_mode;
  char name[32];
  fs_node_t *file_descriptors[16]; // Tabela de arquivos abertos
} task_t;

typedef struct {
  int id;
  int parent_id;
  task_state_t state;
  uint32_t heap_start;
  uint32_t heap_end;
  uint32_t cpu_ticks;
  uint32_t switch_count;
  bool user_mode;
  char name[32];
} task_info_t;

void init_tasking();
int create_task(void (*entry_point)());
int create_task_named(void (*entry_point)(), const char *name);
int create_user_task(uint32_t entry_point, uint32_t user_stack);
int create_user_task_named(uint32_t entry_point, uint32_t user_stack,
                            const char *name);
void switch_task();
uint32_t schedule(uint32_t current_esp);
void move_to_user_mode();
int task_snapshot(task_info_t *out, int max_entries);
const char *task_state_name(task_state_t state);
uint32_t task_total_switches(void);

/* Syscall Helpers */
// Registers struct do isr.h
#include "isr.h"
int fork_process(registers_t *regs);
int sys_waitpid(int pid, int *status, int options);
void sys_exit_process(int status);

#endif
