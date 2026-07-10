#include "task.h"
#include "gdt.h" // TSS
#include "isr.h" // registers_t
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "syscall.h"
#include "vmm.h" // page_directory_t
#include "keyboard.h"
#include <stddef.h>

task_t *current_task = NULL;
task_t *task_list = NULL;
extern page_directory_t *current_directory;

static int next_pid = 1;
static uint32_t global_switch_count = 0;

int last_foreground_pid = -1;

// Helper function to convert int to string if not in string.h
extern char *itoa(int value, char *str, int base);

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
  current_task = (task_t *)kmalloc(sizeof(task_t));
  memset(current_task, 0, sizeof(task_t));
  current_task->id = 0;
  current_task->state = TASK_RUNNING;
  current_task->page_directory = current_directory;
  current_task->user_mode = false;
  strcpy(current_task->cwd, "/");
  task_assign_name(current_task, "kernel", false);

  current_task->next = current_task;
  task_list = current_task;
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
  uint64_t stack = (uint64_t)kmalloc(stack_size) + stack_size;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)stack_size;

  /* Prepara a pilha para o iret */
  registers_t *  regs = (registers_t *)(uint64_t)(stack - sizeof(registers_t));
  memset(regs, 0, sizeof(registers_t));

  regs->rflags = 0x202;
  regs->cs = 0x08;
  regs->rip = (uint64_t)entry_point;
  regs->rsp = stack;
  regs->ss = 0x10;

  new_task->stack_top = (uint64_t)regs;

  new_task->next = task_list->next;
  task_list->next = new_task;
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
  stack = (uint64_t)kmalloc(stack_size) + stack_size;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)stack_size;

  regs = (registers_t *)(uint64_t)(stack - sizeof(registers_t));
  memset(regs, 0, sizeof(registers_t));

  regs->rflags = 0x202;
  regs->cs = 0x1B;   /* 32-bit compat user code */
  regs->ss = 0x23;   /* 32-bit user data (DS/SS) */
  regs->rip = entry_point;
  regs->rsp = user_stack;

  new_task->stack_top = (uint64_t)regs;
  new_task->heap_start = 0x40000000;
  new_task->heap_end = 0x40000000;

  serial_print("DBG_TASK: pid=");
  char nbuf[12];
  itoa(pid, nbuf, 10); serial_print(nbuf);
  serial_print(" name="); serial_print(name);
  serial_print(" entry="); serial_print_hex(entry_point);
  serial_print(" rsp="); serial_print_hex(user_stack);
  serial_print(" heap_start="); serial_print_hex(0x40000000);
  serial_print(" page_dir="); serial_print_hex((uint64_t)current_directory);
  serial_print("\n");

  new_task->next = task_list->next;
  task_list->next = new_task;
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
  stack = (uint64_t)kmalloc(stack_size) + stack_size;
  new_task->kernel_stack = stack;
  new_task->kernel_stack_size = (uint32_t)stack_size;

  regs = (registers_t *)(uint64_t)(stack - sizeof(registers_t));
  memset(regs, 0, sizeof(registers_t));

  regs->rflags = 0x202;
  regs->cs = 0x2B;   /* 64-bit user code */
  regs->ss = 0x33;   /* 64-bit user data */
  regs->rip = entry_point;
  regs->rsp = user_stack;

  new_task->stack_top = (uint64_t)regs;
  new_task->heap_start = 0x40000000;
  new_task->heap_end = 0x40000000;

  serial_print("DBG_TASK64: pid=");
  char nbuf[12];
  itoa(pid, nbuf, 10); serial_print(nbuf);
  serial_print(" name="); serial_print(name);
  serial_print(" entry="); serial_print_hex(entry_point);
  serial_print(" rsp="); serial_print_hex(user_stack);
  serial_print("\n");

  new_task->next = task_list->next;
  task_list->next = new_task;
  return pid;
}

void switch_task() { asm volatile("int $32"); }

uint64_t schedule(uint64_t current_rsp) {
  if (!current_task)
    return current_rsp;

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
  current_task->stack_top = current_rsp;
  current_task->cpu_ticks++;

  /* Encontrar próxima tarefa que não esteja SLEEPING ou ZOMBIE */
  task_t *next = current_task->next;
  while (next->state == TASK_SLEEPING || next->state == TASK_ZOMBIE) {
    if (next == current_task)
      break;
    next = next->next;
  }
  current_task = next;
  current_task->switch_count++;
  global_switch_count++;

  /* VMM Switch: Trocar CR3 se mudou de processo */
  if (current_task->page_directory != current_directory) {
    switch_page_directory(current_task->page_directory);
  }

  /* Atualiza TSS ESP0 para a pilha de kernel da nova tarefa */
  extern tss_entry_t tss_entry;
  tss_entry.rsp0 = current_task->kernel_stack;

  return current_task->stack_top;
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
  asm volatile("cli");

  task_t *parent = (task_t *)current_task;
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  memset(new_task, 0, sizeof(task_t));

  new_task->id = next_pid++;
  new_task->state = TASK_READY;
  new_task->parent = parent;

  // Real copy of user-space memory
  new_task->page_directory = vmm_copy_directory(parent->page_directory);

  uint32_t kernel_stack_size = 4096;
  new_task->kernel_stack =
      (uint64_t)kmalloc(kernel_stack_size) + kernel_stack_size;
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
  new_task->user_mode = parent->user_mode;
  strcpy(new_task->cwd, parent->cwd);

  new_task->next = task_list->next;
  task_list->next = new_task;

  asm volatile("sti");
  return new_task->id;
}

int sys_waitpid(int pid, int *status, int options) {
  (void)options;
  task_t *parent = current_task;

  while (1) {
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

            // Free kernel stack and PCB
            if (t->kernel_stack && t->kernel_stack_size) {
              void *stack_base = (void*)(t->kernel_stack - t->kernel_stack_size);
              kfree(stack_base);
            }
            // TODO: free page directory via vmm
            kfree(t);
            return child_id;
          }
          found = true;
        }
      }
      t = t->next;
    } while (t != task_list);

    if (!any_child || (pid != -1 && !found)) {
      return -1;
    }

    // Wait and reschedule
    parent->state = TASK_SLEEPING;
    switch_task();
  }
}

void sys_kill_by_pid(int pid) {
  if (pid < 0) return;
  asm volatile("cli");
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
  asm volatile("sti");
}

void sys_exit_process(int status) {
  asm volatile("cli");
  if (current_task->id == last_foreground_pid)
    last_foreground_pid = -1;
  current_task->state = TASK_ZOMBIE;
  current_task->exit_code = status;

  // Wake up parent
  if (current_task->parent) {
    current_task->parent->state = TASK_RUNNING;
  }

  asm volatile("sti");
  while (1) {
    switch_task();
  }
}
