#include "task.h"
#include "gdt.h"
#include "isr.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "syscall.h"
#include "vmm.h"
#include "keyboard.h"
#include <stddef.h>
#include "spinlock.h"
#include "elf.h"
#include "timer.h"
#include "usercopy.h"
#include "kstack.h"

cpu_local_t cpus_local[16];
spinlock_t scheduler_lock = {0};
task_t *task_list = NULL;
process_t *process_list = NULL;
static inline int task_quantum(const task_t *t);

/* The scheduler also runs from the timer interrupt. On this single-CPU
 * kernel, any code taking this lock must prevent a timer interrupt from
 * preempting it and trying to take the same lock recursively. */
static inline uint64_t scheduler_lock_irqsave(void) {
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
  spinlock_acquire(&scheduler_lock);
  return flags;
}

static inline void scheduler_unlock_irqrestore(uint64_t flags) {
  spinlock_release(&scheduler_lock);
  if (flags & (1ULL << 9))
    asm volatile("sti" ::: "memory");
}

uint64_t task_scheduler_lock(void) {
  return scheduler_lock_irqsave();
}

void task_scheduler_unlock(uint64_t flags) {
  scheduler_unlock_irqrestore(flags);
}

void wait_queue_init(wait_queue_t *queue) {
  if (queue) queue->head = NULL;
}

int wait_queue_block_locked(wait_queue_t *queue, uintptr_t space,
                            uintptr_t address, bool keyed,
                            wait_cancel_fn cancel, void *cancel_context) {
  thread_t *thread = current_task;
  if (!queue || !thread || thread->waiting_on ||
      thread->state == TASK_ZOMBIE)
    return -1;
  thread->wait_space = space;
  thread->wait_address = address;
  thread->wait_keyed = keyed;
  thread->wait_cancel = cancel;
  thread->wait_cancel_context = cancel_context;
  thread->waiting_on = queue;
  thread->wait_next = queue->head;
  queue->head = thread;
  thread->state = TASK_BLOCKED;
  return 0;
}

static void wait_queue_unlink_locked(wait_queue_t *queue, thread_t *thread) {
  if (!queue || !thread || thread->waiting_on != queue) return;
  thread_t **link = &queue->head;
  while (*link && *link != thread) link = &(*link)->wait_next;
  if (*link == thread) *link = thread->wait_next;
  thread->wait_next = NULL;
  thread->waiting_on = NULL;
  thread->wait_space = 0;
  thread->wait_address = 0;
  thread->wait_keyed = false;
}

uint32_t wait_queue_wake_locked(wait_queue_t *queue, uintptr_t space,
                                uintptr_t address, bool keyed,
                                uint32_t maximum) {
  if (!queue || !maximum) return 0;
  uint32_t woken = 0;
  thread_t **link = &queue->head;
  while (*link && woken < maximum) {
    thread_t *thread = *link;
    if (keyed && (!thread->wait_keyed || thread->wait_space != space ||
                  thread->wait_address != address)) {
      link = &thread->wait_next;
      continue;
    }
    *link = thread->wait_next;
    thread->wait_next = NULL;
    thread->waiting_on = NULL;
    thread->wait_space = 0;
    thread->wait_address = 0;
    thread->wait_keyed = false;
    if (thread->state == TASK_BLOCKED) {
      thread->state = TASK_READY;
      thread->time_slice = task_quantum(thread);
      woken++;
    }
  }
  return woken;
}

uint32_t wait_queue_wake(wait_queue_t *queue, uint32_t maximum) {
  uint64_t flags = scheduler_lock_irqsave();
  uint32_t woken = wait_queue_wake_locked(queue, 0, 0, false, maximum);
  scheduler_unlock_irqrestore(flags);
  return woken;
}

uint32_t wait_queue_count(wait_queue_t *queue) {
  uint64_t flags = scheduler_lock_irqsave();
  uint32_t count = 0;
  if (queue)
    for (thread_t *thread = queue->head; thread; thread = thread->wait_next)
      count++;
  scheduler_unlock_irqrestore(flags);
  return count;
}

#define FUTEX_QUEUE_COUNT 64
#define EAGAIN 11
static wait_queue_t futex_private_queues[FUTEX_QUEUE_COUNT];

static wait_queue_t *futex_private_queue(page_directory_t *mm,
                                         uint32_t *address) {
  uintptr_t key = ((uintptr_t)mm >> 12) ^ ((uintptr_t)address >> 2);
  return &futex_private_queues[key & (FUTEX_QUEUE_COUNT - 1)];
}

int task_futex_wait_private(uint32_t *address, uint32_t expected) {
  thread_t *thread = current_task;
  if (!thread || !thread->process || !address) return -EFAULT;
  if (((uintptr_t)address & (sizeof(uint32_t) - 1)) != 0) return -EINVAL;
  if (validate_user_pointer(address, sizeof(*address)) < 0) return -EFAULT;

  wait_queue_t *queue = futex_private_queue(thread->process->mm, address);
  uint64_t flags = scheduler_lock_irqsave();
  uint32_t observed = 0;
  /* The range was validated before taking the scheduler lock. Page tables are
   * eager and user tasks currently run only on the BSP, so this short aligned
   * access cannot fault or race with another running user CPU. */
  if (copy_from_user_u32_atomic(&observed, address) < 0) {
    scheduler_unlock_irqrestore(flags);
    return -EFAULT;
  }
  if (observed != expected) {
    scheduler_unlock_irqrestore(flags);
    return -EAGAIN;
  }
  if (wait_queue_block_locked(queue, (uintptr_t)thread->process->mm,
                              (uintptr_t)address, true, NULL, NULL) < 0) {
    scheduler_unlock_irqrestore(flags);
    return -EINVAL;
  }
  scheduler_unlock_irqrestore(flags);
  switch_task();
  return 0;
}

int task_futex_wake_private(uint32_t *address, uint32_t maximum) {
  thread_t *thread = current_task;
  if (!thread || !thread->process || !address) return -EFAULT;
  if (((uintptr_t)address & (sizeof(uint32_t) - 1)) != 0) return -EINVAL;
  if (validate_user_pointer(address, sizeof(*address)) < 0) return -EFAULT;
  wait_queue_t *queue = futex_private_queue(thread->process->mm, address);
  uint64_t flags = scheduler_lock_irqsave();
  uint32_t woken = wait_queue_wake_locked(
      queue, (uintptr_t)thread->process->mm, (uintptr_t)address, true,
      maximum);
  scheduler_unlock_irqrestore(flags);
  return (int)woken;
}

/* Setado por switch_task() para forcar um reschedule mesmo quando a tarefa
 * ainda tem quantum: um "cede a CPU" voluntario (yield). */
static volatile int g_resched_force = 0;

static inline int task_quantum(const task_t *t) {
  int p = t->priority;
  if (p < 1) p = 1;
  if (p > TASK_PRIO_MAX) p = TASK_PRIO_MAX;
  return p;
}

static void panic_stack_msg(task_t *curr, const char *type, char *buf, size_t bufsz) {
  (void)bufsz;
  char num[16];
  buf[0] = '\0';
  strcat(buf, "STACK OVERFLOW (");
  strcat(buf, type);
  strcat(buf, ") task=");
  strcat(buf, curr->name[0] ? curr->name : "?");
  strcat(buf, " pid=");
  itoa(curr->id, num, 10);
  strcat(buf, num);
}

static inline void task_sched_fields_init(task_t *t) {
  t->priority = TASK_PRIO_DEFAULT;
  t->time_slice = TASK_PRIO_DEFAULT;
  t->wake_tick = 0;
}
extern page_directory_t *current_directory;

extern uint64_t last_elf_heap_start;

static int next_pid = 1;
static uint32_t global_switch_count = 0;

int last_foreground_pid = -1;
int foreground_job_id = -1;
static int next_job_id = 1;
static void task_kill_job_locked(int job_id);

process_t *task_process_create(int pid, process_t *parent,
                               page_directory_t *mm) {
  process_t *process = (process_t *)kmalloc(sizeof(*process));
  if (!process) return NULL;
  memset(process, 0, sizeof(*process));
  process->pid = pid;
  process->parent = parent;
  process->mm = mm;
  process->mmap_top = USER_MMAP_TOP;
  process->umask = 022;
  strcpy(process->cwd, "/");
  process->references = 1; /* process table owns it until wait/reap */

  uint64_t flags = scheduler_lock_irqsave();
  process->next = process_list;
  process_list = process;
  scheduler_unlock_irqrestore(flags);
  return process;
}

void task_process_attach(process_t *process, thread_t *thread) {
  if (!process || !thread) return;
  uint64_t flags = scheduler_lock_irqsave();
  thread->process = process;
  thread->process_next = process->threads;
  process->threads = thread;
  process->thread_count++;
  process->references++;
  if (!process->leader) process->leader = thread;
  scheduler_unlock_irqrestore(flags);
}

void task_process_detach(process_t *process, thread_t *thread) {
  if (!process || !thread) return;
  uint64_t flags = scheduler_lock_irqsave();
  thread_t **link = &process->threads;
  while (*link && *link != thread) link = &(*link)->process_next;
  if (*link == thread) {
    *link = thread->process_next;
    if (process->thread_count) process->thread_count--;
    if (process->references) process->references--;
    if (process->leader == thread) process->leader = process->threads;
  }
  thread->process_next = NULL;
  thread->process = NULL;
  scheduler_unlock_irqrestore(flags);
}

void task_process_set_mm(process_t *process, page_directory_t *mm) {
  if (process) process->mm = mm;
}

void task_process_discard(process_t *process) {
  if (!process) return;
  uint64_t flags = scheduler_lock_irqsave();
  if (process->thread_count != 0 || process->references != 1) {
    scheduler_unlock_irqrestore(flags);
    return;
  }
  process_t **link = &process_list;
  while (*link && *link != process) link = &(*link)->next;
  if (*link == process) *link = process->next;
  process->next = NULL;
  scheduler_unlock_irqrestore(flags);
  task_process_release_io(process);
  if (process->mm && process->mm != kernel_directory)
    vmm_free_directory(process->mm);
  kfree(process);
}

static void process_wake_parent(process_t *process) {
  if (!process || !process->parent) return;
  uint64_t flags = scheduler_lock_irqsave();
  wait_queue_wake_locked(&process->parent->child_waiters, 0, 0, false,
                         UINT32_MAX);
  scheduler_unlock_irqrestore(flags);
}

static void task_mark_zombie_locked(thread_t *thread, int status) {
  if (!thread) return;
  if (thread->waiting_on)
    wait_queue_unlink_locked(thread->waiting_on, thread);
  thread->exit_status = status;
  thread->state = TASK_ZOMBIE;
}

void init_cpu_local(int cpu_id) {
    if (cpu_id < 0 || cpu_id >= 16) return;
    cpu_local_t *local = &cpus_local[cpu_id];
    memset(local, 0, sizeof(cpu_local_t));
    local->cpu_id = cpu_id;

    // Write structure address to IA32_GS_BASE MSR (0xC0000101)
    uint64_t val = (uint64_t)local;
    uint32_t low = val & 0xFFFFFFFF;
    uint32_t high = val >> 32;
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000101));
    /* User GS starts at zero; SWAPGS moves the per-CPU base across the ring. */
    asm volatile("wrmsr" : : "a"(0), "d"(0), "c"(0xC0000102));
    size_t irq_size = 0;
    void *irq_base = kernel_stack_alloc(KERNEL_STACK_MIN_SIZE, &irq_size);
    if (irq_base) {
      local->irq_stack_base = (uint64_t)irq_base;
      local->irq_stack_top = (uint64_t)irq_base + irq_size;
    }
}

// Helper function to convert int to string if not in string.h
extern char *itoa(int value, char *str, int base);

static inline void fpu_context_save(void *area) {
  asm volatile("fxsave (%0)" :: "r"(area) : "memory");
}

static inline void fpu_context_restore(void *area) {
  asm volatile("fxrstor (%0)" :: "r"(area) : "memory");
}

static void *fpu_context_create(void) {
  uint8_t *area = (uint8_t *)kmalloc_a(512);
  if (!area) return NULL;
  memset(area, 0, 512);
  *(uint16_t *)(void *)(area + 0) = 0x037f; /* x87 default control word */
  *(uint32_t *)(void *)(area + 24) = 0x1f80; /* MXCSR default */
  return area;
}

void task_set_fpu(void *fpu_area) {
  if (!current_task) return;
  current_task->fpu_ctx = fpu_area;
  if (fpu_area) {
    uint32_t m = 0x1F80; /* default MXCSR: all exceptions masked */
    asm volatile("fninit");
    asm volatile("ldmxcsr (%0)" :: "r"(&m));
    fpu_context_save(fpu_area);
  }
}

static void task_assign_name(task_t *task, const char *name, bool user_mode) {
  if (!task) {
    return;
  }

  memset(task->name, 0, sizeof(task->name));
  if (name && name[0]) {
    strncpy(task->name, name, sizeof(task->name) - 1);
    return;
  }

  strcpy(task->name, user_mode ? "user" : "kthread");
}

void init_tasking() {
  init_cpu_local(0); // Initialize BSP (CPU 0) local storage

  task_t *init_task = (task_t *)kmalloc(sizeof(task_t));
  if (!init_task) return;
  memset(init_task, 0, sizeof(task_t));
  init_task->id = 0;
  init_task->state = TASK_RUNNING;
  process_t *init_process = task_process_create(0, NULL, current_directory);
  if (!init_process) { kfree(init_task); return; }
  task_process_attach(init_process, init_task);
  init_task->kernel_stack_base = (uint64_t)kernel_stack_boot_base();
  init_task->kernel_stack_size = (uint32_t)kernel_stack_boot_size();
  init_task->kernel_stack = init_task->kernel_stack_base +
                            init_task->kernel_stack_size;
  init_task->stack_canary = 0xDEADBEEFCAFEBABE;
  *(uint64_t *)init_task->kernel_stack_base = init_task->stack_canary;
  init_task->user_mode = false;
  strcpy(init_task->process->cwd, "/");
  task_assign_name(init_task, "kernel", false);
  task_sched_fields_init(init_task);

  init_task->next = init_task;
  task_list = init_task;

  set_current_task(init_task);
}

int create_task(void (*entry_point)()) {
  return create_task_named(entry_point, NULL);
}

int create_task_named(void (*entry_point)(), const char *name) {
  return create_task_named_prio(entry_point, name, TASK_PRIO_DEFAULT);
}

int create_task_named_prio(void (*entry_point)(), const char *name,
                            int priority) {
  return create_task_named_stack(entry_point, name, priority,
                                 KERNEL_STACK_MAX_SIZE);
}

int create_task_named_stack(void (*entry_point)(), const char *name,
                             int priority, uint64_t stack_size) {
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  if (!new_task) return -1;
  memset(new_task, 0, sizeof(task_t));
  new_task->id = next_pid++;
  int pid = new_task->id;
  new_task->state = TASK_READY;
  process_t *process = task_process_create(
      pid, current_task ? current_task->process : NULL, current_directory);
  if (!process) { kfree(new_task); return -1; }
  strcpy(process->cwd, current_task ? current_task->process->cwd : "/");
  new_task->user_mode = false;
  task_assign_name(new_task, name, false);
  task_sched_fields_init(new_task);
  if (priority >= 1 && priority <= TASK_PRIO_MAX) {
    new_task->priority = priority;
    new_task->time_slice = priority;
  }

  size_t actual_stack_size = 0;
  uint64_t stack_base = (uint64_t)kernel_stack_alloc((size_t)stack_size,
                                                      &actual_stack_size);
  if (!stack_base) { task_process_discard(process); kfree(new_task); return -1; }
  uint64_t stack = stack_base + actual_stack_size;
  new_task->kernel_stack_base = stack_base;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)actual_stack_size;

  /* Prepara a pilha para o iret */
  registers_t *  regs = (registers_t *)(uint64_t)(stack - sizeof(registers_t));
  for (size_t i = 0; i < sizeof(registers_t); i++) {
    ((uint8_t *)regs)[i] = 0;
  }

  regs->rflags = 0x202;
  regs->cs = 0x08;
  regs->rip = (uint64_t)entry_point;
  /* x86-64 SysV: a C function is entered with rsp == 8 (mod 16) (return
   * address already pushed). kmalloc only guarantees 4-byte alignment, so
   * align the fresh task stack or 16-byte-aligned ops (movaps, used by the
   * -msse MP3 decoder) fault with #GP on every other allocation. */
  regs->rsp = (uint64_t)((stack & ~(uint64_t)0xF) - 8);
  regs->ss = 0x10;

  /* A pilha de kernel nao usa canary no topo: o endereco inicial de RSP
   * coincide com o campo rsp/ss do frame de iret. Marcar o canary ali
   * corromperia o frame (RSP saltaria para 0xDEAD...). O canary de base
   * continua detectando estouro de pilha. */
  new_task->stack_canary = 0xDEADBEEFCAFEBABE;
  new_task->stack_canary_addr = 0;

  /* Also canary at bottom for underflow detection */
  *(uint64_t *)stack_base = 0xDEADBEEFCAFEBABE;

  new_task->stack_top = (uint64_t)regs;
  new_task->fpu_ctx = fpu_context_create();
  if (!new_task->fpu_ctx) {
    kernel_stack_free((void *)stack_base, actual_stack_size);
    task_process_discard(process);
    kfree(new_task);
    return -1;
  }
  task_process_attach(process, new_task);

  uint64_t sched_flags = scheduler_lock_irqsave();
  new_task->next = task_list->next;
  task_list->next = new_task;
  scheduler_unlock_irqrestore(sched_flags);
  return pid;
}

int create_user_task(uint64_t entry_point, uint64_t user_stack) {
  return create_user_task_named(entry_point, user_stack, NULL);
}

int create_user_task_named(uint64_t entry_point, uint64_t user_stack,
                            const char *name) {
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  if (!new_task) return -1;
  uint64_t stack;
  registers_t *regs;

  memset(new_task, 0, sizeof(task_t));
  new_task->id = next_pid++;
  int pid = new_task->id;
  new_task->state = TASK_READY;
  process_t *process = task_process_create(
      new_task->id, current_task ? current_task->process : NULL,
      current_directory);
  if (!process) { kfree(new_task); return -1; }
  new_task->user_mode = true;
  process->uid = process->euid = 1000;
  process->gid = process->egid = 1000;
  process->job_id = pid;
  strcpy(process->cwd, current_task ? current_task->process->cwd : "/");
  task_assign_name(new_task, name, true);
  task_sched_fields_init(new_task);

  size_t stack_size = 0;
  uint64_t stack_base = (uint64_t)kernel_stack_alloc(KERNEL_STACK_MIN_SIZE,
                                                      &stack_size);
  if (!stack_base) { task_process_discard(process); kfree(new_task); return -1; }
  stack = stack_base + stack_size;
  /* Alinhar kernel stack para 16 bytes (ABI x86_64) */
  stack = stack & ~0xF;
  new_task->kernel_stack_base = stack_base;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)stack_size;
  new_task->stack_canary = 0xDEADBEEFCAFEBABE;
  new_task->stack_canary_addr = 0;
  *(uint64_t *)stack_base = new_task->stack_canary;

  regs = (registers_t *)(stack - sizeof(registers_t));
  for (size_t i = 0; i < sizeof(registers_t); i++) {
    ((uint8_t *)regs)[i] = 0;
  }

  regs->rflags = 0x202;
  regs->cs = 0x1B;   /* 32-bit compat user code */
  regs->ss = 0x23;   /* 32-bit user data (DS/SS) */
  regs->rip = entry_point;
  regs->rsp = user_stack;

  new_task->stack_top = (uint64_t)regs;
  process->heap_start = 0x40000000;
  process->heap_end = 0x40000000;
  new_task->fpu_ctx = fpu_context_create();
  if (!new_task->fpu_ctx) {
    kernel_stack_free((void *)stack_base, stack_size);
    task_process_discard(process);
    kfree(new_task);
    return -1;
  }
  task_process_attach(process, new_task);

  serial_print("DBG_TASK: pid=");
  char nbuf[12];
  itoa(pid, nbuf, 10); serial_print(nbuf);
  serial_print(" name="); serial_print(name);
  serial_print(" entry="); serial_print_hex(entry_point);
  serial_print(" rsp="); serial_print_hex(user_stack);
  serial_print(" heap_start="); serial_print_hex(0x40000000);
  serial_print(" page_dir="); serial_print_hex((uint64_t)current_directory);
  serial_print("\n");

  uint64_t sched_flags = scheduler_lock_irqsave();
  new_task->next = task_list->next;
  task_list->next = new_task;
  scheduler_unlock_irqrestore(sched_flags);
  return pid;
}

int create_user_task_64_named(uint64_t entry_point, uint64_t user_stack,
                               const char *name) {
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  if (!new_task) return -1;
  uint64_t stack;
  registers_t *regs;

  memset(new_task, 0, sizeof(task_t));
  new_task->id = next_pid++;
  int pid = new_task->id;
  new_task->state = TASK_READY;
  process_t *process = task_process_create(
      new_task->id, current_task ? current_task->process : NULL,
      current_directory);
  if (!process) { kfree(new_task); return -1; }
  new_task->user_mode = true;
  process->uid = process->euid = 1000;
  process->gid = process->egid = 1000;
  process->job_id = pid;
  strcpy(process->cwd, current_task ? current_task->process->cwd : "/");
  task_assign_name(new_task, name, true);
  task_sched_fields_init(new_task);

  size_t stack_size = 0;
  uint64_t stack_base = (uint64_t)kernel_stack_alloc(KERNEL_STACK_MIN_SIZE,
                                                      &stack_size);
  if (!stack_base) { task_process_discard(process); kfree(new_task); return -1; }
  stack = stack_base + stack_size;
  /* Alinhar kernel stack para 16 bytes (ABI x86_64) */
  stack = stack & ~0xF;
  new_task->kernel_stack_base = stack_base;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)stack_size;
  new_task->stack_canary = 0xDEADBEEFCAFEBABE;
  new_task->stack_canary_addr = 0;
  *(uint64_t *)stack_base = new_task->stack_canary;

  regs = (registers_t *)(uint64_t)(stack - sizeof(registers_t));
  /* Explicitly zero with known pattern to detect corruption */
  for (size_t i = 0; i < sizeof(registers_t); i++) {
    ((uint8_t *)regs)[i] = 0;
  }

  regs->rflags = 0x202;
  regs->cs = 0x2B;   /* 64-bit user code */
  regs->ss = 0x33;   /* 64-bit user data */
  regs->rip = entry_point;
  regs->rsp = user_stack;

  new_task->stack_top = (uint64_t)regs;
  uint64_t hstart = last_elf_heap_start ? last_elf_heap_start : 0x40000000;
  process->heap_start = hstart;
  process->heap_end = hstart;
  new_task->fpu_ctx = fpu_context_create();
  if (!new_task->fpu_ctx) {
    kernel_stack_free((void *)stack_base, stack_size);
    task_process_discard(process);
    kfree(new_task);
    return -1;
  }
  task_process_attach(process, new_task);

  serial_print("DBG_TASK64: pid=");
  char nbuf[12];
  itoa(pid, nbuf, 10); serial_print(nbuf);
  serial_print(" name="); serial_print(name);
  serial_print(" entry="); serial_print_hex(entry_point);
  serial_print(" rsp="); serial_print_hex(user_stack);
  serial_print(" heap_start="); serial_print_hex(hstart);
  serial_print("\n");

  uint64_t sched_flags = scheduler_lock_irqsave();
  new_task->next = task_list->next;
  task_list->next = new_task;
  scheduler_unlock_irqrestore(sched_flags);
  return pid;
}

void switch_task() {
  g_resched_force = 1;
  asm volatile("int $32");
}

uint64_t schedule(uint64_t current_rsp) {
  task_t *curr = current_task;
  if (!curr)
    return current_rsp;

  /* Fast path: only init task exists (no other tasks), don't attempt scheduling.
   * This avoids all stack checks, spinlocks, and task list traversal. */
  if (curr->id == 0 && curr->next == curr)
    return current_rsp;

  /* Skip all stack checks for init task (PID 0) which uses the boot stack
   * with no allocated bounds (kernel_stack == 0). */
  if (curr->kernel_stack != 0 && curr->kernel_stack_base != 0) {
    /* RSP bounds check - stack grows DOWN from kernel_stack to kernel_stack_base. */
    uint64_t stack_top = curr->kernel_stack;
    uint64_t stack_bottom = curr->kernel_stack_base;
    if (current_rsp < stack_bottom || current_rsp > stack_top) {
      extern void kernel_panic(const char *msg);
      char buf[128];
      serial_print("[kstack] rsp="); serial_print_hex(current_rsp);
      serial_print(" bottom="); serial_print_hex(stack_bottom);
      serial_print(" top="); serial_print_hex(stack_top);
      serial_print(" size="); serial_print_hex(curr->kernel_stack_size);
      serial_print("\n");
      panic_stack_msg(curr, "RSP out of bounds", buf, sizeof(buf));
      kernel_panic(buf);
    }

    /* Top canary check (at original RSP) */
    if (curr->stack_canary_addr && *(uint64_t *)curr->stack_canary_addr != 0xDEADBEEFCAFEBABE) {
      extern void kernel_panic(const char *msg);
      char buf[128];
      panic_stack_msg(curr, "top canary", buf, sizeof(buf));
      kernel_panic(buf);
    }

    /* Bottom canary check */
    if (*(uint64_t *)curr->kernel_stack_base != 0xDEADBEEFCAFEBABE) {
      extern void kernel_panic(const char *msg);
      char buf[128];
      panic_stack_msg(curr, "base canary", buf, sizeof(buf));
      kernel_panic(buf);
    }
  }

  uint64_t sched_flags = scheduler_lock_irqsave();

  int forced = g_resched_force;
  g_resched_force = 0;

  if (check_ctrl_c()) {
    if (foreground_job_id >= 0) {
      task_kill_job_locked(foreground_job_id);
    } else if (last_foreground_pid > 0) {
      task_t *fg = task_list;
      do {
        if (fg->id == last_foreground_pid && fg->user_mode) {
          task_mark_zombie_locked(fg, 128 + 2);
          last_foreground_pid = -1;
          if (fg->process) {
            for (thread_t *thread = fg->process->threads; thread;
                 thread = thread->process_next) {
              task_mark_zombie_locked(thread, 128 + 2);
            }
          }
          break;
        }
        fg = fg->next;
      } while (fg != task_list);
    }
  }

  /* Acorda tarefas adormecidas cujo wake_tick ja passou. wake_tick==0 marca
   * um bloqueio sem timeout (ex.: waitpid), acordado explicitamente. */
  if (task_list) {
    task_t *w = task_list;
    do {
      if (w->state == TASK_SLEEPING && w->wake_tick != 0 &&
          (int64_t)(timer_ticks - w->wake_tick) >= 0) {
        w->state = TASK_READY;
        w->wake_tick = 0;
        w->time_slice = task_quantum(w);
      }
      w = w->next;
    } while (w != task_list);
  }

  /* Salva a pilha da tarefa que estava rodando */
  curr->stack_top = current_rsp;
  curr->cpu_ticks++;

  /* Weighted round-robin: mantem a tarefa atual rodando enquanto tiver
   * quantum. Um yield explicito (switch_task) ou um bloqueio pula isso. */
  if (!forced && curr->state == TASK_RUNNING) {
    if (curr->time_slice > 1) {
      curr->time_slice--;
      scheduler_unlock_irqrestore(sched_flags);
      return current_rsp;
    }
    curr->time_slice = 0;
  }

  // Transition old task from RUNNING to READY
  if (curr->state == TASK_RUNNING) {
      curr->state = TASK_READY;
  }

  /* Round-robin: pick next READY task in list */
  task_t *next = curr->next;
  while (next->state != TASK_READY) {
    if (next == curr)
      break;
    next = next->next;
  }

  /* The boot task is the single-CPU idle context. If all user tasks are
   * blocked, return to it so timer/device interrupts can make progress. */
  if (next->state != TASK_READY && curr->state != TASK_READY) {
    task_t *idle = task_list;
    do {
      if (idle->id == 0 && idle->state != TASK_ZOMBIE) {
        idle->state = TASK_READY;
        next = idle;
        break;
      }
      idle = idle->next;
    } while (idle != task_list);
  }

  if (next->state == TASK_READY) {
      next->state = TASK_RUNNING;
      next->time_slice = task_quantum(next);
      set_current_task(next);
  } else {
      if (curr->state == TASK_READY) {
          curr->state = TASK_RUNNING;
          curr->time_slice = task_quantum(curr);
      }
  }

  task_t *new_curr = current_task;
  new_curr->switch_count++;
  global_switch_count++;

  /* VMM Switch: Trocar CR3 se mudou de processo */
  if (new_curr->process->mm != current_directory) {
    switch_page_directory(new_curr->process->mm);
  }

  /* Atualiza TSS ESP0 para a pilha de kernel da nova tarefa */
  extern tss_entry_t cpus_tss[16];
  if (new_curr->kernel_stack != 0) {
      cpus_tss[get_cpu_id()].rsp0 = new_curr->kernel_stack;
  }

  /* FPU/SSE contexto: o kernel é -mno-sse, mas tasks que usam FP (ex. o
   * decoder MP3, compilado com -msse) precisam ter o estado XMM/x87 salvo
   * e restaurado, ou ele é corrompido por outra task (fast_memcpy usa SSE2). */
  if (new_curr != curr) {
    if (curr->fpu_ctx) {
      fpu_context_save(curr->fpu_ctx);
    }
    if (new_curr->fpu_ctx) {
      fpu_context_restore(new_curr->fpu_ctx);
    }
  }

  /* A blocked thread can be killed while it is not the current task. Such a
   * zombie will never be selected again, so reap one non-running zombie on
   * every scheduler pass as well as the task we just switched away from. */
  task_t *reap_task = NULL;
  if (curr != new_curr && curr->state == TASK_ZOMBIE && curr->id != 0) {
    reap_task = curr;
  } else if (task_list) {
    task_t *candidate = task_list;
    do {
      if (candidate != curr && candidate != new_curr &&
          candidate->id != 0 && candidate->state == TASK_ZOMBIE) {
        reap_task = candidate;
        break;
      }
      candidate = candidate->next;
    } while (candidate != task_list);
  }
  wait_cancel_fn reap_cancel = NULL;
  void *reap_cancel_context = NULL;
  if (reap_task) {
    reap_cancel = reap_task->wait_cancel;
    reap_cancel_context = reap_task->wait_cancel_context;
    reap_task->wait_cancel = NULL;
    reap_task->wait_cancel_context = NULL;
    if (reap_task->waiting_on)
      wait_queue_unlink_locked(reap_task->waiting_on, reap_task);
    task_t *prev = task_list;
    while (prev->next != reap_task && prev->next != task_list)
      prev = prev->next;
    if (prev->next == reap_task) {
      if (task_list == reap_task) task_list = reap_task->next;
      prev->next = reap_task->next;
    } else {
      reap_task = NULL;
    }
  }

  scheduler_unlock_irqrestore(sched_flags);

  if (reap_task) {
    if (reap_cancel) reap_cancel(reap_cancel_context);
    process_t *process = reap_task->process;
    int exit_status = reap_task->exit_status;
    task_process_detach(process, reap_task);
    if (process && process->thread_count == 0) {
      process->zombie = true;
      process->exit_status = exit_status;
      task_process_release_io(process);
      process_wake_parent(process);
    }
    if (reap_task->kernel_stack_base && reap_task->kernel_stack_size)
      kernel_stack_free((void *)reap_task->kernel_stack_base,
                        reap_task->kernel_stack_size);
    if (reap_task->fpu_ctx) kfree(reap_task->fpu_ctx);
    kfree(reap_task);
  }

  return new_curr->stack_top;
}

void task_sleep_ms(uint32_t ms) {
  task_t *t = current_task;
  if (!t)
    return;
  if (ms == 0) {
    switch_task();
    return;
  }

  /* timer_ticks roda a 100 Hz (10 ms por tick). */
  uint32_t ticks = (ms + 9) / 10;
  if (ticks == 0)
    ticks = 1;

  uint64_t sched_flags = scheduler_lock_irqsave();
  t->wake_tick = (uint64_t)timer_ticks + ticks;
  t->state = TASK_SLEEPING;
  scheduler_unlock_irqrestore(sched_flags);

  switch_task();
}

void task_set_priority(int pid, int priority) {
  if (priority < 1)
    priority = 1;
  if (priority > TASK_PRIO_MAX)
    priority = TASK_PRIO_MAX;

  uint64_t sched_flags = scheduler_lock_irqsave();
  task_t *t = task_list;
  if (t) {
    do {
      if (t->id == pid) {
        t->priority = priority;
        break;
      }
      t = t->next;
    } while (t != task_list);
  }
  scheduler_unlock_irqrestore(sched_flags);
}

int task_get_priority(int pid) {
  int found = -1;
  uint64_t sched_flags = scheduler_lock_irqsave();
  task_t *t = task_list;
  if (t) {
    do {
      if (t->id == pid) {
        found = t->priority;
        break;
      }
      t = t->next;
    } while (t != task_list);
  }
  scheduler_unlock_irqrestore(sched_flags);
  return found;
}

int task_set_foreground(int pid) {
  if (!current_task || !current_task->user_mode ||
      (pid != -1 && pid != current_task->id))
    return -1;
  uint64_t sched_flags = scheduler_lock_irqsave();
  foreground_job_id = -1;
  last_foreground_pid = pid;
  current_task->process->spawn_job_id = 0;
  scheduler_unlock_irqrestore(sched_flags);
  return 0;
}

int task_create_job(void) {
  if (!current_task || !current_task->user_mode) return -1;
  uint64_t sched_flags = scheduler_lock_irqsave();
  int job_id = next_job_id++;
  if (job_id <= 0) {
    next_job_id = 2;
    job_id = 1;
  }
  current_task->process->spawn_job_id = job_id;
  scheduler_unlock_irqrestore(sched_flags);
  return job_id;
}

int task_set_foreground_job(int job_id) {
  if (!current_task || !current_task->user_mode || job_id < -1) return -1;
  uint64_t sched_flags = scheduler_lock_irqsave();
  if (job_id >= 0) {
    bool found = false;
    task_t *t = task_list;
    if (t) do {
      if (t->process && t->process->parent == current_task->process &&
          t->process->job_id == job_id) {
        found = true;
        break;
      }
      t = t->next;
    } while (t != task_list);
    if (!found) {
      scheduler_unlock_irqrestore(sched_flags);
      return -1;
    }
  }
  current_task->process->spawn_job_id = 0;
  foreground_job_id = job_id;
  last_foreground_pid = -1;
  scheduler_unlock_irqrestore(sched_flags);
  return 0;
}

static void task_kill_job_locked(int job_id) {
  if (!task_list || job_id < 0) return;
  task_t *t = task_list;
  do {
      if (t->user_mode && t->process && t->process->job_id == job_id &&
          t->state != TASK_ZOMBIE) {
      task_mark_zombie_locked(t, 128 + 2);
    }
    t = t->next;
  } while (t != task_list);
  if (foreground_job_id == job_id) foreground_job_id = -1;
  last_foreground_pid = -1;
}

void task_kill_job(int job_id) {
  uint64_t sched_flags = scheduler_lock_irqsave();
  task_kill_job_locked(job_id);
  scheduler_unlock_irqrestore(sched_flags);
}

int task_snapshot(task_info_t *out, int max_entries) {
  task_t *t;
  int count = 0;
  int user_output = current_task && current_task->user_mode;

  if (!out) return user_output ? -EFAULT : 0;
  if (max_entries <= 0) return 0;
  if (user_output) {
    if ((size_t)max_entries > (size_t)-1 / sizeof(*out) ||
        validate_user_write(out, (size_t)max_entries * sizeof(*out)) < 0)
      return -EFAULT;
  }
  if (!task_list) return 0;

  t = task_list;
  do {
    if (count >= max_entries) {
      break;
    }

    task_info_t snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.id = t->id;
    snapshot.parent_id = t->process && t->process->parent
                             ? t->process->parent->pid : -1;
    snapshot.state = t->state;
    snapshot.heap_start = t->process->heap_start;
    snapshot.heap_end = t->process->heap_end;
    snapshot.cpu_ticks = t->cpu_ticks;
    snapshot.switch_count = t->switch_count;
    snapshot.priority = t->priority;
    snapshot.user_mode = t->user_mode;
    strncpy(snapshot.name, t->name, sizeof(snapshot.name) - 1);
    snapshot.name[sizeof(snapshot.name) - 1] = '\0';
    if (user_output) {
      if (copy_to_user(&out[count], &snapshot, sizeof(snapshot)) < 0)
        return -EFAULT;
    } else {
      out[count] = snapshot;
    }

    count++;
    t = t->next;
  } while (t != task_list);

  return count;
}

const char *task_state_name(task_state_t state) {
  switch (state) {
  case TASK_RUNNING:
    return "RUN";
  case TASK_READY:
    return "READY";
  case TASK_SLEEPING:
    return "SLEEP";
  case TASK_BLOCKED:
    return "BLOCKED";
  case TASK_ZOMBIE:
    return "ZOMB";
  default:
    return "?";
  }
}

uint32_t task_total_switches(void) { return global_switch_count; }

void move_to_user_mode() {
  asm volatile("cli; \
         mov $0x23, %ax; \
         mov %ax, %ds; \
         mov %ax, %es; \
         mov %ax, %fs; \
         mov %ax, %gs; \
         \
         mov %rsp, %rax; \
         pushq $0x23; \
         pushq %rax; \
         pushfq; \
         pop %rax; \
         or $0x200, %rax; \
         pushq %rax; \
         pushq $0x1B; \
         pushq $1f; \
         iretq; \
         1: \
         ");
}

int fork_process(registers_t *regs) {

  task_t *parent = (task_t *)current_task;
  if (!parent || !regs) return -ENOMEM;
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  if (!new_task) return -ENOMEM;
  memset(new_task, 0, sizeof(task_t));

  new_task->id = next_pid++;
  new_task->state = TASK_READY;
  process_t *child_process = task_process_create(
      new_task->id, parent->process, NULL);
  if (!child_process) { kfree(new_task); return -ENOMEM; }
  child_process->job_id = parent->process->spawn_job_id > 0
      ? parent->process->spawn_job_id : parent->process->job_id;

  // Real copy of user-space memory
  child_process->mm = vmm_copy_directory(parent->process->mm);
  if (!child_process->mm) {
    task_process_discard(child_process);
    kfree(new_task);
    return -ENOMEM;
  }

  size_t kernel_stack_size = 0;
  uint64_t kernel_stack_base = (uint64_t)kernel_stack_alloc(
      KERNEL_STACK_MIN_SIZE, &kernel_stack_size);
  if (!kernel_stack_base) {
    vmm_free_directory(child_process->mm);
    child_process->mm = NULL;
    task_process_discard(child_process);
    kfree(new_task);
    return -ENOMEM;
  }
  new_task->kernel_stack_base = kernel_stack_base;
  new_task->kernel_stack = kernel_stack_base + kernel_stack_size;
  new_task->kernel_stack_size = kernel_stack_size;
  new_task->stack_canary = 0xDEADBEEFCAFEBABE;
  new_task->stack_canary_addr = 0;
  *(uint64_t *)kernel_stack_base = new_task->stack_canary;

  new_task->fpu_ctx = fpu_context_create();
  if (!new_task->fpu_ctx) {
    kernel_stack_free((void *)kernel_stack_base, kernel_stack_size);
    vmm_free_directory(child_process->mm);
    child_process->mm = NULL;
    task_process_discard(child_process);
    kfree(new_task);
    return -ENOMEM;
  }
  if (parent->fpu_ctx) {
    fpu_context_save(parent->fpu_ctx);
    memcpy(new_task->fpu_ctx, parent->fpu_ctx, 512);
  }

  uint64_t regs_size = sizeof(registers_t);
  uint64_t new_stack_ptr = new_task->kernel_stack - regs_size;
  memcpy((void *)(uint64_t)new_stack_ptr, regs, (uint32_t)regs_size);

  registers_t *new_regs = (registers_t *)(uint64_t)new_stack_ptr;
  new_regs->rax = 0;
  new_task->stack_top = new_stack_ptr;

  // Copy heap limits and cwd
  child_process->heap_start = parent->process->heap_start;
  child_process->heap_end = parent->process->heap_end;
  child_process->mmap_top = parent->process->mmap_top;
  child_process->uid = parent->process->uid;
  child_process->euid = parent->process->euid;
  child_process->gid = parent->process->gid;
  child_process->egid = parent->process->egid;
  child_process->umask = parent->process->umask;
  new_task->user_mode = parent->user_mode;
  child_process->abi = parent->process->abi;
  new_task->fs_base = parent->fs_base;
  new_task->user_gs_base = parent->user_gs_base;
  new_task->priority = parent->priority;
  new_task->time_slice = task_quantum(new_task);
  new_task->wake_tick = 0;
  strcpy(child_process->cwd, parent->process->cwd);

  // Copy file descriptors
  for (int i = 0; i < 32; i++) {
    child_process->file_descriptors[i] = parent->process->file_descriptors[i];
    sys_fd_get(&child_process->file_descriptors[i]);
  }

  task_assign_name(new_task, "fork", new_task->user_mode);
  task_process_attach(child_process, new_task);
  uint64_t sched_flags = scheduler_lock_irqsave();
  new_task->next = task_list->next;
  task_list->next = new_task;
  if (parent->user_mode)
    last_foreground_pid = new_task->id;
  scheduler_unlock_irqrestore(sched_flags);

  asm volatile("sti");
  return child_process->pid;
}

int task_clone_thread(registers_t *regs, uint64_t user_stack,
                      uint64_t tls_base, int set_tls) {
  thread_t *parent = current_task;
  if (!parent || !parent->process || !regs || !parent->user_mode ||
      !user_stack || user_stack > USER_ADDR_MAX ||
      validate_user_write((void *)(user_stack - 1), 1) < 0 ||
      (set_tls && tls_base &&
       (tls_base < USER_ADDR_MIN || tls_base >= USER_ADDR_MAX)))
    return -EINVAL;

  thread_t *thread = (thread_t *)kmalloc(sizeof(*thread));
  if (!thread) return -ENOMEM;
  memset(thread, 0, sizeof(*thread));
  thread->id = next_pid++;
  thread->state = TASK_READY;
  thread->user_mode = 1;
  thread->priority = parent->priority;
  thread->time_slice = task_quantum(thread);
  thread->fs_base = set_tls ? tls_base : parent->fs_base;
  thread->user_gs_base = parent->user_gs_base;
  thread->clear_child_tid = 0;
  strncpy(thread->name, parent->name, sizeof(thread->name) - 1);

  size_t stack_size = 0;
  thread->kernel_stack_base = (uint64_t)kernel_stack_alloc(
      KERNEL_STACK_MIN_SIZE, &stack_size);
  if (!thread->kernel_stack_base) { kfree(thread); return -ENOMEM; }
  thread->kernel_stack_size = (uint32_t)stack_size;
  thread->kernel_stack = thread->kernel_stack_base + stack_size;
  thread->stack_canary = 0xDEADBEEFCAFEBABE;
  *(uint64_t *)thread->kernel_stack_base = thread->stack_canary;

  registers_t *child_regs = (registers_t *)(thread->kernel_stack -
                                              sizeof(registers_t));
  memcpy(child_regs, regs, sizeof(*child_regs));
  child_regs->rax = 0;
  child_regs->rsp = user_stack;
  thread->stack_top = (uint64_t)child_regs;
  thread->fpu_ctx = fpu_context_create();
  if (!thread->fpu_ctx) {
    kernel_stack_free((void *)thread->kernel_stack_base, stack_size);
    kfree(thread);
    return -ENOMEM;
  }
  if (parent->fpu_ctx) {
    fpu_context_save(parent->fpu_ctx);
    memcpy(thread->fpu_ctx, parent->fpu_ctx, 512);
  }

  task_process_attach(parent->process, thread);
  uint64_t flags = scheduler_lock_irqsave();
  thread->next = task_list->next;
  task_list->next = thread;
  scheduler_unlock_irqrestore(flags);
  return thread->id;
}

int sys_waitpid(int pid, int *status, int options) {
  thread_t *parent_thread = current_task;
  process_t *parent = parent_thread ? parent_thread->process : NULL;
  if (!parent) return -1;
  if (status && parent_thread->user_mode &&
      validate_user_write(status, sizeof(*status)) < 0)
    return -EFAULT;

  while (1) {
    uint64_t sched_flags = scheduler_lock_irqsave();
    process_t *child = process_list;
    bool found = false;
    bool any_child = false;
    while (child) {
      if (child->parent == parent) {
        any_child = true;
        if (pid == -1 || child->pid == pid) {
          found = true;
          if (child->zombie) {
            if (status) {
              if (parent_thread->user_mode) {
                if (copy_to_user(status, &child->exit_status,
                                 sizeof(child->exit_status)) < 0) {
                  scheduler_unlock_irqrestore(sched_flags);
                  return -EFAULT;
                }
              } else {
                *status = child->exit_status;
              }
            }
            int child_pid = child->pid;
            thread_t *leader = child->leader;
            process_t *init_process = process_list;
            while (init_process && init_process->pid != 0)
              init_process = init_process->next;
            for (process_t *orphan = process_list; orphan;
                 orphan = orphan->next)
              if (orphan->parent == child) orphan->parent = init_process;

            if (leader && task_list) {
              task_t *prev = task_list;
              while (prev->next != leader && prev->next != task_list)
                prev = prev->next;
              if (prev->next == leader) {
                if (leader == task_list) task_list = leader->next;
                prev->next = leader->next;
              }
            }
            scheduler_unlock_irqrestore(sched_flags);

            if (leader && leader->kernel_stack_base &&
                leader->kernel_stack_size && leader->id != 0)
              kernel_stack_free((void *)leader->kernel_stack_base,
                                leader->kernel_stack_size);
            if (leader) {
              task_process_detach(child, leader);
              kfree(leader);
            }
            task_process_discard(child);
            return child_pid;
          }
        }
      }
      child = child->next;
    }
    if (!any_child || (pid != -1 && !found)) {
      scheduler_unlock_irqrestore(sched_flags);
      return -1;
    }
    if ((options & 1) && found) {
      scheduler_unlock_irqrestore(sched_flags);
      return 0;
    }
    if (wait_queue_block_locked(&parent->child_waiters, 0, 0, false,
                                NULL, NULL) < 0) {
      scheduler_unlock_irqrestore(sched_flags);
      return -1;
    }
    scheduler_unlock_irqrestore(sched_flags);
    switch_task();
  }
}

void sys_kill_by_pid(int pid) {
  if (pid < 0) return;
  uint64_t sched_flags = scheduler_lock_irqsave();
  for (process_t *process = process_list; process; process = process->next) {
    if (process->pid != pid) continue;
    if (!process->leader || !process->leader->user_mode || process->zombie)
      break;
    for (thread_t *thread = process->threads; thread;
         thread = thread->process_next) {
      task_mark_zombie_locked(thread, 128 + 2);
    }
    if (process->pid == last_foreground_pid) last_foreground_pid = -1;
    break;
  }
  scheduler_unlock_irqrestore(sched_flags);
}

void sys_exit_thread(int status) {
  if (!current_task) for (;;) asm volatile("hlt");
  uint64_t sched_flags = scheduler_lock_irqsave();
  task_mark_zombie_locked(current_task, status);
  if (current_task->process &&
      current_task->process->pid == last_foreground_pid)
    last_foreground_pid = -1;
  scheduler_unlock_irqrestore(sched_flags);

  while (1) {
    switch_task();
  }
}

void sys_exit_process(int status) {
  if (!current_task || !current_task->process)
    for (;;) asm volatile("hlt");
  process_t *process = current_task->process;
  uint64_t sched_flags = scheduler_lock_irqsave();
  if (process->pid == last_foreground_pid) last_foreground_pid = -1;
  for (thread_t *thread = process->threads; thread;
       thread = thread->process_next) {
    task_mark_zombie_locked(thread, status);
  }
  scheduler_unlock_irqrestore(sched_flags);

  while (1) {
    switch_task();
  }
}
