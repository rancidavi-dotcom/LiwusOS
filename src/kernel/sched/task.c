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

cpu_local_t cpus_local[16];
spinlock_t scheduler_lock = {0};
task_t *task_list = NULL;

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
}

// Helper function to convert int to string if not in string.h
extern char *itoa(int value, char *str, int base);

static inline void fpu_context_save(void *area) {
  asm volatile("fxsave (%0)" :: "r"(area) : "memory");
}

static inline void fpu_context_restore(void *area) {
  asm volatile("fxrstor (%0)" :: "r"(area) : "memory");
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
  memset(init_task, 0, sizeof(task_t));
  init_task->id = 0;
  init_task->state = TASK_RUNNING;
  init_task->page_directory = current_directory;
  init_task->user_mode = false;
  strcpy(init_task->cwd, "/");
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
  return create_task_named_stack(entry_point, name, priority, 65536);
}

int create_task_named_stack(void (*entry_point)(), const char *name,
                             int priority, uint64_t stack_size) {
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  memset(new_task, 0, sizeof(task_t));
  new_task->id = next_pid++;
  int pid = new_task->id;
  new_task->state = TASK_READY;
  new_task->parent = current_task;
  new_task->page_directory = current_directory;
  new_task->user_mode = false;
  strcpy(new_task->cwd, current_task ? current_task->cwd : "/");
  task_assign_name(new_task, name, false);
  task_sched_fields_init(new_task);
  if (priority >= 1 && priority <= TASK_PRIO_MAX) {
    new_task->priority = priority;
    new_task->time_slice = priority;
  }

  if (stack_size < 8192) stack_size = 8192;
  uint64_t stack_base = (uint64_t)kmalloc(stack_size);
  uint64_t stack = stack_base + stack_size;
  new_task->kernel_stack_base = stack_base;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)stack_size;

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
  uint64_t stack;
  registers_t *regs;

  memset(new_task, 0, sizeof(task_t));
  new_task->id = next_pid++;
  int pid = new_task->id;
  new_task->state = TASK_READY;
  new_task->parent = current_task;
  new_task->page_directory = current_directory;
  new_task->user_mode = true;
  new_task->job_id = pid;
  strcpy(new_task->cwd, current_task ? current_task->cwd : "/");
  task_assign_name(new_task, name, true);
  task_sched_fields_init(new_task);

  uint64_t stack_size = 8192;
  uint64_t stack_base = (uint64_t)kmalloc(stack_size);
  if (!stack_base) { kfree(new_task); return -1; }
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
  new_task->heap_start = 0x40000000;
  new_task->heap_end = 0x40000000;
  new_task->mmap_top = USER_MMAP_TOP;

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
  uint64_t stack;
  registers_t *regs;

  memset(new_task, 0, sizeof(task_t));
  new_task->id = next_pid++;
  int pid = new_task->id;
  new_task->state = TASK_READY;
  new_task->parent = current_task;
  new_task->page_directory = current_directory;
  new_task->user_mode = true;
  new_task->job_id = pid;
  strcpy(new_task->cwd, current_task ? current_task->cwd : "/");
  task_assign_name(new_task, name, true);
  task_sched_fields_init(new_task);

  uint64_t stack_size = 8192;
  uint64_t stack_base = (uint64_t)kmalloc(stack_size);
  if (!stack_base) { kfree(new_task); return -1; }
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
  new_task->heap_start = hstart;
  new_task->heap_end = hstart;
  new_task->mmap_top = USER_MMAP_TOP;

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
          task_release_io(fg);
          fg->state = TASK_ZOMBIE;
          fg->exit_code = 128 + 2;
          last_foreground_pid = -1;
          if (fg->parent) fg->parent->state = TASK_READY;
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
  if (new_curr->page_directory != current_directory) {
    switch_page_directory(new_curr->page_directory);
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

  scheduler_unlock_irqrestore(sched_flags);

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
  current_task->spawn_job_id = 0;
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
  current_task->spawn_job_id = job_id;
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
      if (t->parent == current_task && t->job_id == job_id) {
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
  current_task->spawn_job_id = 0;
  foreground_job_id = job_id;
  last_foreground_pid = -1;
  scheduler_unlock_irqrestore(sched_flags);
  return 0;
}

static void task_kill_job_locked(int job_id) {
  if (!task_list || job_id < 0) return;
  task_t *t = task_list;
  do {
    if (t->user_mode && t->job_id == job_id && t->state != TASK_ZOMBIE) {
      task_release_io(t);
      t->state = TASK_ZOMBIE;
      t->exit_code = 128 + 2;
      if (t->parent) t->parent->state = TASK_READY;
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
    snapshot.parent_id = t->parent ? t->parent->id : -1;
    snapshot.state = t->state;
    snapshot.heap_start = t->heap_start;
    snapshot.heap_end = t->heap_end;
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
  new_task->parent = parent;
  new_task->job_id = parent->spawn_job_id > 0
      ? parent->spawn_job_id : parent->job_id;

  // Real copy of user-space memory
  new_task->page_directory = vmm_copy_directory(parent->page_directory);
  if (!new_task->page_directory) {
    kfree(new_task);
    return -ENOMEM;
  }

  uint32_t kernel_stack_size = 4096;
  uint64_t kernel_stack_base = (uint64_t)kmalloc(kernel_stack_size);
  if (!kernel_stack_base) {
    vmm_free_directory(new_task->page_directory);
    kfree(new_task);
    return -ENOMEM;
  }
  new_task->kernel_stack_base = kernel_stack_base;
  new_task->kernel_stack = kernel_stack_base + kernel_stack_size;
  new_task->kernel_stack_size = kernel_stack_size;
  new_task->stack_canary = 0xDEADBEEFCAFEBABE;
  new_task->stack_canary_addr = 0;
  *(uint64_t *)kernel_stack_base = new_task->stack_canary;

  uint64_t regs_size = sizeof(registers_t);
  uint64_t new_stack_ptr = new_task->kernel_stack - regs_size;
  memcpy((void *)(uint64_t)new_stack_ptr, regs, (uint32_t)regs_size);

  registers_t *new_regs = (registers_t *)(uint64_t)new_stack_ptr;
  new_regs->rax = 0;
  new_task->stack_top = new_stack_ptr;

  // Copy heap limits and cwd
  new_task->heap_start = parent->heap_start;
  new_task->heap_end = parent->heap_end;
  new_task->mmap_top = parent->mmap_top;
  new_task->user_mode = parent->user_mode;
  new_task->priority = parent->priority;
  new_task->time_slice = task_quantum(new_task);
  new_task->wake_tick = 0;
  strcpy(new_task->cwd, parent->cwd);

  // Copy file descriptors
  for (int i = 0; i < 32; i++) {
    new_task->file_descriptors[i] = parent->file_descriptors[i];
    sys_fd_get(&new_task->file_descriptors[i]);
  }

  task_assign_name(new_task, "fork", new_task->user_mode);
  uint64_t sched_flags = scheduler_lock_irqsave();
  new_task->next = task_list->next;
  task_list->next = new_task;
  if (parent->user_mode)
    last_foreground_pid = new_task->id;
  scheduler_unlock_irqrestore(sched_flags);

  asm volatile("sti");
  return new_task->id;
}

int sys_waitpid(int pid, int *status, int options) {
  (void)options;
  task_t *parent = current_task;
  if (status && parent && parent->user_mode &&
      validate_user_write(status, sizeof(*status)) < 0)
    return -EFAULT;

  while (1) {
    uint64_t sched_flags = scheduler_lock_irqsave();
    task_t *t = task_list;
    bool found = false;
    bool any_child = false;

    do {
      if (t->parent && t->parent->id == parent->id) {
        any_child = true;
        if (pid == -1 || t->id == pid) {
          if (t->state == TASK_ZOMBIE) {
            if (status) {
              if (parent->user_mode) {
                if (copy_to_user(status, &t->exit_code, sizeof(t->exit_code)) < 0) {
                  scheduler_unlock_irqrestore(sched_flags);
                  return -EFAULT;
                }
              } else {
                *status = t->exit_code;
              }
            }
            int child_id = t->id;

            // Simple cleanup: remove from task list
            task_t *prev = task_list;
            while (prev->next != t)
              prev = prev->next;
            prev->next = t->next;

            scheduler_unlock_irqrestore(sched_flags);

            if (t->kernel_stack_base && t->kernel_stack_size) {
               kfree((void *)t->kernel_stack_base);
            }

            // Reparent children to avoid Use-After-Free
            task_t *reparent_curr = task_list;
            do {
              if (reparent_curr->parent == t) {
                reparent_curr->parent = task_list;
              }
              reparent_curr = reparent_curr->next;
            } while (reparent_curr != task_list);

            // Free page directory
            extern page_directory_t *kernel_directory;
            if (t->page_directory && t->page_directory != kernel_directory) {
              vmm_free_directory(t->page_directory);
            }

            kfree(t);
            return child_id;
          }
          found = true;
        }
      }
      t = t->next;
    } while (t != task_list);

    if (!any_child || (pid != -1 && !found)) {
      scheduler_unlock_irqrestore(sched_flags);
      return -1;
    }

    if ((options & 1) && found) {
      scheduler_unlock_irqrestore(sched_flags);
      return 0;
    }

    // Wait and reschedule
    parent->state = TASK_SLEEPING;
    scheduler_unlock_irqrestore(sched_flags);
    switch_task();
  }
}

void sys_kill_by_pid(int pid) {
  if (pid < 0) return;
  uint64_t sched_flags = scheduler_lock_irqsave();
  task_t *t = task_list;
  do {
    if (t->id == pid) {
      if (!t->user_mode) break;
      task_release_io(t);
      t->state = TASK_ZOMBIE;
      t->exit_code = 128 + 2;
      if (t->id == last_foreground_pid)
        last_foreground_pid = -1;
      if (t->parent) {
        t->parent->state = TASK_READY;
      }
      break;
    }
    t = t->next;
  } while (t != task_list);
  scheduler_unlock_irqrestore(sched_flags);
}

void sys_exit_process(int status) {
  /* Clean up any GUI nodes (windows, images) owned by this process */

  task_release_io(current_task);
  uint64_t sched_flags = scheduler_lock_irqsave();
  if (current_task->id == last_foreground_pid)
    last_foreground_pid = -1;
  current_task->state = TASK_ZOMBIE;
  current_task->exit_code = status;

  // Wake up parent
  if (current_task->parent) {
    current_task->parent->state = TASK_READY;
  }
  scheduler_unlock_irqrestore(sched_flags);

  while (1) {
    switch_task();
  }
}
