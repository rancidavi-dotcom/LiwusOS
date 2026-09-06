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

cpu_local_t cpus_local[16];
spinlock_t scheduler_lock = {0};
task_t *task_list = NULL;
extern page_directory_t *current_directory;

extern uint64_t last_elf_heap_start;

static int next_pid = 1;
static uint32_t global_switch_count = 0;

int last_foreground_pid = -1;

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
  asm volatile("fxsave64 (%0)" :: "r"(area) : "memory");
}

static inline void fpu_context_restore(void *area) {
  asm volatile("fxrstor64 (%0)" :: "r"(area) : "memory");
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
  strcpy(init_task->cwd, "/house/localhost");
  task_assign_name(init_task, "kernel", false);

  init_task->next = init_task;
  task_list = init_task;

  set_current_task(init_task);
}

int create_task(void (*entry_point)()) {
  return create_task_named(entry_point, NULL);
}

int create_task_named(void (*entry_point)(), const char *name) {
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

  uint64_t stack_size = 8192;
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

  new_task->stack_top = (uint64_t)regs;

  spinlock_acquire(&scheduler_lock);
  new_task->next = task_list->next;
  task_list->next = new_task;
  spinlock_release(&scheduler_lock);
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
  strcpy(new_task->cwd, current_task ? current_task->cwd : "/");
  task_assign_name(new_task, name, true);

  uint64_t stack_size = 8192;
  uint64_t stack_base = (uint64_t)kmalloc(stack_size);
  stack = stack_base + stack_size;
  /* Alinhar kernel stack para 16 bytes (ABI x86_64) */
  stack = stack & ~0xF;
  new_task->kernel_stack_base = stack_base;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)stack_size;

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
  new_task->mmap_top = 0xBFFF0000;

  serial_print("DBG_TASK: pid=");
  char nbuf[12];
  itoa(pid, nbuf, 10); serial_print(nbuf);
  serial_print(" name="); serial_print(name);
  serial_print(" entry="); serial_print_hex(entry_point);
  serial_print(" rsp="); serial_print_hex(user_stack);
  serial_print(" heap_start="); serial_print_hex(0x40000000);
  serial_print(" page_dir="); serial_print_hex((uint64_t)current_directory);
  serial_print("\n");

  spinlock_acquire(&scheduler_lock);
  new_task->next = task_list->next;
  task_list->next = new_task;
  spinlock_release(&scheduler_lock);
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
  strcpy(new_task->cwd, current_task ? current_task->cwd : "/");
  task_assign_name(new_task, name, true);

  uint64_t stack_size = 8192;
  uint64_t stack_base = (uint64_t)kmalloc(stack_size);
  stack = stack_base + stack_size;
  /* Alinhar kernel stack para 16 bytes (ABI x86_64) */
  stack = stack & ~0xF;
  new_task->kernel_stack_base = stack_base;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)stack_size;

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
  new_task->mmap_top = 0xBFFF0000;

  serial_print("DBG_TASK64: pid=");
  char nbuf[12];
  itoa(pid, nbuf, 10); serial_print(nbuf);
  serial_print(" name="); serial_print(name);
  serial_print(" entry="); serial_print_hex(entry_point);
  serial_print(" rsp="); serial_print_hex(user_stack);
  serial_print(" heap_start="); serial_print_hex(hstart);
  serial_print("\n");

  spinlock_acquire(&scheduler_lock);
  new_task->next = task_list->next;
  task_list->next = new_task;
  spinlock_release(&scheduler_lock);
  return pid;
}

void switch_task() { asm volatile("int $32"); }

uint64_t schedule(uint64_t current_rsp) {
  task_t *curr = current_task;
  if (!curr)
    return current_rsp;

  spinlock_acquire(&scheduler_lock);

  if (check_ctrl_c() && last_foreground_pid > 0) {
    task_t *fg = task_list;
    do {
      if (fg->id == last_foreground_pid && fg->user_mode) {
        fg->state = TASK_ZOMBIE;
        fg->exit_code = 128 + 2;
        last_foreground_pid = -1;
        if (fg->parent) {
          fg->parent->state = TASK_RUNNING;
        }
        break;
      }
      fg = fg->next;
    } while (fg != task_list);
  }

  /* Salva a pilha da tarefa que estava rodando */
  curr->stack_top = current_rsp;
  curr->cpu_ticks++;

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
      set_current_task(next);
  } else {
      if (curr->state == TASK_READY) {
          curr->state = TASK_RUNNING;
      }
  }

  task_t *new_curr = current_task;
  new_curr->switch_count++;
  global_switch_count++;

  /* Debug: log task switch for user tasks */
  if (new_curr->user_mode && new_curr != curr) {
    serial_print("SCHED: switch to pid=");
    char dbg[16];
    itoa(new_curr->id, dbg, 10);
    serial_print(dbg);
    serial_print(" (");
    serial_print(new_curr->name);
    serial_print(") kernel_stack=");
    serial_print_hex(new_curr->kernel_stack);
    serial_print(" stack_top=");
    serial_print_hex(new_curr->stack_top);
    serial_print("\n");
  }

  /* VMM Switch: Trocar CR3 se mudou de processo */
  if (new_curr->page_directory != current_directory) {
    switch_page_directory(new_curr->page_directory);
  }

  /* Atualiza TSS ESP0 para a pilha de kernel da nova tarefa */
  extern tss_entry_t cpus_tss[16];
  cpus_tss[get_cpu_id()].rsp0 = new_curr->kernel_stack;

  if (new_curr->user_mode && new_curr != curr) {
    serial_print("SCHED: TSS.rsp0 updated to ");
    serial_print_hex(new_curr->kernel_stack);
    serial_print("\n");
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

  spinlock_release(&scheduler_lock);

  return new_curr->stack_top;
}

int task_snapshot(task_info_t *out, int max_entries) {
  task_t *t;
  int count = 0;

  if (!task_list || !out || max_entries <= 0) {
    return 0;
  }

  t = task_list;
  do {
    if (count >= max_entries) {
      break;
    }

    out[count].id = t->id;
    out[count].parent_id = t->parent ? t->parent->id : -1;
    out[count].state = t->state;
    out[count].heap_start = t->heap_start;
    out[count].heap_end = t->heap_end;
    out[count].cpu_ticks = t->cpu_ticks;
    out[count].switch_count = t->switch_count;
    out[count].user_mode = t->user_mode;
    strncpy(out[count].name, t->name, sizeof(out[count].name) - 1);
    out[count].name[sizeof(out[count].name) - 1] = '\0';

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
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  memset(new_task, 0, sizeof(task_t));

  new_task->id = next_pid++;
  new_task->state = TASK_READY;
  new_task->parent = parent;

  // Real copy of user-space memory
  new_task->page_directory = vmm_copy_directory(parent->page_directory);

  uint32_t kernel_stack_size = 4096;
  uint64_t kernel_stack_base = (uint64_t)kmalloc(kernel_stack_size);
  new_task->kernel_stack_base = kernel_stack_base;
  new_task->kernel_stack = kernel_stack_base + kernel_stack_size;
  new_task->kernel_stack_size = kernel_stack_size;

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
  strcpy(new_task->cwd, parent->cwd);

  // Copy file descriptors
  for (int i = 0; i < 32; i++) {
    new_task->file_descriptors[i] = parent->file_descriptors[i];
    if (new_task->file_descriptors[i].type == 3) { // 3 = FD_TYPE_PIPE
      pipe_t *p = (pipe_t *)new_task->file_descriptors[i].socket_ptr;
      if (p) p->refcount++;
    }
  }

  
  asm volatile("sti");
  return new_task->id;
}

int sys_waitpid(int pid, int *status, int options) {
  (void)options;
  task_t *parent = current_task;

  while (1) {
    spinlock_acquire(&scheduler_lock);
    task_t *t = task_list;
    bool found = false;
    bool any_child = false;

    do {
      if (t->parent && t->parent->id == parent->id) {
        any_child = true;
        if (pid == -1 || t->id == pid) {
          if (t->state == TASK_ZOMBIE) {
            if (status)
              *status = t->exit_code;
            int child_id = t->id;

            // Simple cleanup: remove from task list
            task_t *prev = task_list;
            while (prev->next != t)
              prev = prev->next;
            prev->next = t->next;

            spinlock_release(&scheduler_lock);

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
      spinlock_release(&scheduler_lock);
      return -1;
    }

    // Wait and reschedule
    parent->state = TASK_SLEEPING;
    spinlock_release(&scheduler_lock);
    switch_task();
  }
}

void sys_kill_by_pid(int pid) {
  if (pid < 0) return;
  spinlock_acquire(&scheduler_lock);
  task_t *t = task_list;
  do {
    if (t->id == pid) {
      if (!t->user_mode) break;
      t->state = TASK_ZOMBIE;
      t->exit_code = 128 + 2;
      if (t->parent) {
        t->parent->state = TASK_RUNNING;
      }
      break;
    }
    t = t->next;
  } while (t != task_list);
  spinlock_release(&scheduler_lock);
}

void sys_exit_process(int status) {
  spinlock_acquire(&scheduler_lock);
  if (current_task->id == last_foreground_pid)
    last_foreground_pid = -1;
  current_task->state = TASK_ZOMBIE;
  current_task->exit_code = status;

  // Wake up parent
  if (current_task->parent) {
    current_task->parent->state = TASK_RUNNING;
  }
  spinlock_release(&scheduler_lock);

  while (1) {
    switch_task();
  }
}
