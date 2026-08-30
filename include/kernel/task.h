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

#define PIPE_BUF_SIZE 4096

typedef struct {
  uint8_t buffer[PIPE_BUF_SIZE];
  uint32_t read_pos;
  uint32_t write_pos;
  uint32_t bytes_avail;
  int refcount;
} pipe_t;

typedef struct {
  int type;
  uint32_t base_addr;
  uint32_t size;
  uint32_t offset;
  int owned_buffer;
  void *socket_ptr;
} kfile_t;

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
  void *fpu_ctx;
  uint64_t heap_start;
  uint64_t heap_end;
  // Topo (endereço mais alto) da região de mmap anônimo. Começa logo
  // abaixo da stack do usuário (0xC0000000) e cresce para baixo.
  uint64_t mmap_top;
  uint64_t cpu_ticks;
  uint64_t switch_count;
  bool user_mode;
  char name[32];
  char cwd[256];
  kfile_t file_descriptors[32];
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
void task_set_fpu(void *fpu_area);
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

// Per-CPU variables and TLS support
typedef struct {
    int cpu_id;
    int padding;
    task_t *current_task_ptr;
    uint64_t kernel_stack;
} __attribute__((packed)) cpu_local_t;

static inline uint64_t read_gs_qword(uint32_t offset) {
    uint64_t val;
    asm volatile("movq %%gs:%c1, %0" : "=r"(val) : "i"(offset));
    return val;
}

static inline void write_gs_qword(uint32_t offset, uint64_t val) {
    asm volatile("movq %0, %%gs:%c1" : : "r"(val), "i"(offset));
}

#define current_task ((task_t *)read_gs_qword(8))

static inline int get_cpu_id(void) {
    return (int)read_gs_qword(0);
}

static inline void set_current_task(task_t *task) {
    write_gs_qword(8, (uint64_t)task);
}

void init_cpu_local(int cpu_id);

#endif
