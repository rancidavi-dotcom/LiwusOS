#ifndef TASK_H
#define TASK_H

#include "vmm.h" // page_directory_t
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  TASK_RUNNING,
  TASK_READY,
  TASK_SLEEPING,
  TASK_ZOMBIE,
  TASK_BLOCKED
} task_state_t;

typedef enum {
  TASK_ABI_LIWUS = 0,
  TASK_ABI_LINUX_X86_64 = 1
} task_abi_t;

#include "vfs.h"

struct thread;
typedef struct wait_queue {
  struct thread *head;
} wait_queue_t;
typedef void (*wait_cancel_fn)(void *context);

#define PIPE_BUF_SIZE 4096

typedef struct {
  uint8_t buffer[PIPE_BUF_SIZE];
  uint32_t read_pos;
  uint32_t write_pos;
  uint32_t bytes_avail;
  int references;
  int readers;
  int writers;
  wait_queue_t read_waiters;
  wait_queue_t write_waiters;
} pipe_t;

typedef struct {
  fs_node_t *node;
  uint32_t offset;
  uint32_t size;
  int references;
  uint8_t access;
  uint8_t opened;
} file_description_t;

typedef struct {
  int type;
  uint32_t base_addr;
  uint32_t size;
  uint32_t offset;
  int owned_buffer;
  void *socket_ptr;
  file_description_t *description;
} kfile_t;

typedef struct process {
  int pid; /* process identity (TGID), distinct from thread TIDs */
  struct process *parent;
  struct process *next;
  struct thread *threads;
  struct thread *leader;
  wait_queue_t child_waiters;
  uint32_t thread_count;
  uint32_t references; /* process table plus attached thread references */
  bool zombie;
  int exit_status;
  page_directory_t *mm;
  uint64_t heap_start;
  uint64_t heap_end;
  uint64_t mmap_top;
  task_abi_t abi;
  int job_id;
  int spawn_job_id;
  char cwd[256];
  kfile_t file_descriptors[32];
  uint32_t uid, euid, gid, egid;
  uint32_t umask;
} process_t;

typedef struct thread {
  int id;
  task_state_t state;
  int exit_status;
  uint32_t kernel_mutex_depth;
  int pending_kill;
  int pending_kill_status;
  uint64_t stack_top;
  uint64_t kernel_stack;
  uint64_t kernel_stack_base;
  uint32_t kernel_stack_size;
  uint64_t stack_canary;
  uint64_t stack_canary_addr;
  uint64_t fs_base;
  uint64_t user_gs_base;
  uint64_t clear_child_tid;
  wait_queue_t *waiting_on;
  struct thread *wait_next;
  uintptr_t wait_space;
  uintptr_t wait_address;
  bool wait_keyed;
  wait_cancel_fn wait_cancel;
  void *wait_cancel_context;
  struct thread *next;
  struct thread *process_next;
  process_t *process;
  void *fpu_ctx;
  uint64_t cpu_ticks;
  uint64_t switch_count;
  /* Escalonamento: quantos ticks consecutivos a tarefa roda por vez
   * (weighted round-robin). 1 = comportamento classico (troca a cada tick). */
  int priority;
  int time_slice;     // ticks restantes no quantum atual
  uint64_t wake_tick; // 0 = sem wake agendado; senao tick absoluto do timer
  bool user_mode;
  char name[32];
} thread_t;

/* Scheduler APIs still use the historical task name for a schedulable
 * thread. Process resources are stored exclusively in process_t. */
typedef thread_t task_t;

typedef struct {
  int id;
  int parent_id;
  task_state_t state;
  uint64_t heap_start;
  uint64_t heap_end;
  uint64_t cpu_ticks;
  uint64_t switch_count;
  int priority;
  bool user_mode;
  char name[32];
} task_info_t;

void init_tasking();
bool tasking_is_initialized(void);
int create_task(void (*entry_point)());
int create_task_named(void (*entry_point)(), const char *name);
/* Cria a tarefa ja com a prioridade desejada, sem janela em que ela possa
 * rodar com a prioridade default (a tarefa pode ser escalada imediatamente
 * apos a criacao). */
int create_task_named_prio(void (*entry_point)(), const char *name,
                           int priority);
int create_task_named_stack(void (*entry_point)(), const char *name,
                            int priority, uint64_t stack_size);
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

/* Escalonamento por prioridade + sleep real.
 * priority e um multiplicador de quantum (1..10): com 1 a tarefa cede a CPU
 * a cada tick (round-robin puro); com N ela roda N ticks consecutivos. */
#define TASK_PRIO_LOW 1
#define TASK_PRIO_DEFAULT 1
#define TASK_PRIO_NORMAL 2
#define TASK_PRIO_HIGH 4
#define TASK_PRIO_MAX 10

/* Bloqueia a tarefa atual por aproximadamente ms milissegundos. A tarefa vira
 * TASK_SLEEPING e e acordada automaticamente pelo scheduler no tick alvo. */
void task_sleep_ms(uint32_t ms);
/* Wait queues are thread-context only, never for interrupt handlers.
 * block_locked requires the scheduler lock; unlock before switch_task(). */
uint64_t task_scheduler_lock(void);
void task_scheduler_unlock(uint64_t flags);
void wait_queue_init(wait_queue_t *queue);
int wait_queue_block_locked(wait_queue_t *queue, uintptr_t space,
                            uintptr_t address, bool keyed,
                            wait_cancel_fn cancel, void *cancel_context);
uint32_t wait_queue_wake_locked(wait_queue_t *queue, uintptr_t space,
                                uintptr_t address, bool keyed,
                                uint32_t maximum);
uint32_t wait_queue_wake(wait_queue_t *queue, uint32_t maximum);
uint32_t wait_queue_count(wait_queue_t *queue);
int task_futex_wait_private(uint32_t *address, uint32_t expected);
int task_futex_wake_private(uint32_t *address, uint32_t maximum);

/* Ajusta/consulta a prioridade de uma tarefa pelo pid. */
void task_set_priority(int pid, int priority);
int task_get_priority(int pid);

/* Syscall Helpers */
#include "isr.h"
int fork_process(registers_t *regs);
int task_clone_thread(registers_t *regs, uint64_t user_stack,
                      uint64_t tls_base, int set_tls);
int sys_waitpid(int pid, int *status, int options);
void sys_exit_process(int status);
void sys_exit_thread(int status);
void sys_kill_by_pid(int pid);
extern process_t *process_list;
process_t *task_process_create(int pid, process_t *parent,
                               page_directory_t *mm);
void task_process_discard(process_t *process);
void task_process_attach(process_t *process, thread_t *thread);
void task_process_detach(process_t *process, thread_t *thread);
void task_process_set_mm(process_t *process, page_directory_t *mm);
void task_process_release_io(process_t *process);
void task_release_io(task_t *task);
void sys_fd_get(kfile_t *file);
void sys_fd_put(kfile_t *file);
uint64_t sys_io_live_file_descriptions(void);
uint64_t sys_io_live_pipe_objects(void);

extern int last_foreground_pid;
int task_set_foreground(int pid);
extern int foreground_job_id;
int task_create_job(void);
int task_set_foreground_job(int job_id);
void task_kill_job(int job_id);

// Per-CPU variables and TLS support
typedef struct {
    int cpu_id;
    int padding;
    task_t *current_task_ptr;
    uint64_t kernel_stack;
  uint64_t syscall_user_rsp;
    uint64_t irq_stack_top;
    uint64_t irq_stack_base;
} __attribute__((packed)) cpu_local_t;

static inline uint64_t read_gs_qword(uint32_t offset) {
    uint64_t val;
    asm volatile("movq %%gs:%c1, %0" : "=r"(val) : "i"(offset));
    return val;
}

static inline void write_gs_qword(uint32_t offset, uint64_t val) {
    asm volatile("movq %0, %%gs:%c1" : : "r"(val), "i"(offset));
}

static inline void write_msr_qword(uint32_t msr, uint64_t val) {
  uint32_t low = (uint32_t)val;
  uint32_t high = (uint32_t)(val >> 32);
  asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(msr));
}

#define current_task ((task_t *)read_gs_qword(8))

static inline int get_cpu_id(void) {
    return (int)read_gs_qword(0);
}

static inline void set_current_task(task_t *task) {
    write_gs_qword(8, (uint64_t)task);
  if (task) {
    write_gs_qword(16, task->kernel_stack);
    write_msr_qword(0xC0000100U, task->fs_base);       /* IA32_FS_BASE */
    write_msr_qword(0xC0000102U, task->user_gs_base);  /* KERNEL_GS_BASE */
  }
}

void init_cpu_local(int cpu_id);

#endif
