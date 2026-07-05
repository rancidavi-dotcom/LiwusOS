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
  uint64_t stack_top;
  uint64_t kernel_stack;
  uint32_t kernel_stack_size;
  struct task *parent;
  struct task *next;
  page_directory_t *page_directory;
  uint64_t heap_start;
  uint64_t heap_end;
  uint64_t cpu_ticks;
  uint64_t switch_count;
  bool user_mode;
  char name[32];
  fs_node_t *file_descriptors[16];
} task_t;

typedef struct {
  int id;
  int parent_id;
  task_state_t state;
  uint64_t heap_start;
  uint64_t heap_end;
  uint64_t cpu_ticks;
  uint64_t switch_count;
  bool user_mode;
  char name[32];
} task_info_t;

void init_tasking();
int create_task(void (*entry_point)());
int create_task_named(void (*entry_point)(), const char *name);
int create_user_task(uint64_t entry_point, uint64_t user_stack);
int create_user_task_named(uint64_t entry_point, uint64_t user_stack,
                            const char *name);
int create_user_task_64_named(uint64_t entry_point, uint64_t user_stack,
                               const char *name);
void switch_task();
uint64_t schedule(uint64_t current_rsp);
void move_to_user_mode();
int task_snapshot(task_info_t *out, int max_entries);
const char *task_state_name(task_state_t state);
uint32_t task_total_switches(void);

/* Syscall Helpers */
#include "isr.h"
int fork_process(registers_t *regs);
int sys_waitpid(int pid, int *status, int options);
void sys_exit_process(int status);
void sys_kill_by_pid(int pid);

extern int last_foreground_pid;

#endif
