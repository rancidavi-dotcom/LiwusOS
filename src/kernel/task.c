#include "task.h"
#include "gdt.h" // TSS
#include "isr.h" // registers_t
#include "kheap.h"
#include "string.h"
#include "syscall.h"
#include "vmm.h" // page_directory_t
#include <stddef.h>

task_t *current_task = NULL;
task_t *task_list = NULL;
extern page_directory_t *current_directory;

static int next_pid = 1;
static uint32_t global_switch_count = 0;

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
  task_assign_name(current_task, "kernel", false);

  current_task->next = current_task;
  task_list = current_task;
}

void create_task(void (*entry_point)()) {
  create_task_named(entry_point, NULL);
}

void create_task_named(void (*entry_point)(), const char *name) {
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  memset(new_task, 0, sizeof(task_t));
  new_task->id = next_pid++;
  new_task->state = TASK_READY;
  new_task->page_directory = current_directory;
  new_task->user_mode = false;
  task_assign_name(new_task, name, false);

  uint32_t stack_size = 8192;
  uint32_t stack = (uint32_t)kmalloc(stack_size) + stack_size;
  new_task->kernel_stack = stack;

  /* Prepara a pilha para o iret */
  registers_t *regs = (registers_t *)(stack - sizeof(registers_t));

  regs->eflags = 0x202; /* Interrupções habilitadas */
  regs->cs = 0x08;
  regs->eip = (uint32_t)entry_point;

  regs->ds = 0x10;
  regs->eax = 0;
  regs->ebx = 0;
  regs->ecx = 0;
  regs->edx = 0;
  regs->esi = 0;
  regs->edi = 0;
  regs->ebp = 0;

  new_task->stack_top = (uint32_t)regs;

  new_task->next = task_list->next;
  task_list->next = new_task;
}

void create_user_task(uint32_t entry_point, uint32_t user_stack) {
  create_user_task_named(entry_point, user_stack, NULL);
}

void create_user_task_named(uint32_t entry_point, uint32_t user_stack,
                            const char *name) {
  task_t *new_task = (task_t *)kmalloc(sizeof(task_t));
  uint32_t stack;
  registers_t *regs;

  memset(new_task, 0, sizeof(task_t));
  new_task->id = next_pid++;
  new_task->state = TASK_READY;
  new_task->page_directory = current_directory;
  new_task->user_mode = true;
  task_assign_name(new_task, name, true);

  uint32_t stack_size = 8192;
  stack = (uint32_t)kmalloc(stack_size) + stack_size;
  new_task->kernel_stack = stack;

  regs = (registers_t *)(stack - sizeof(registers_t));
  memset(regs, 0, sizeof(registers_t));

  regs->eflags = 0x202;
  regs->cs = 0x1B;
  regs->ds = 0x23;
  regs->ss = 0x23;
  regs->eip = entry_point;
  regs->useresp = user_stack;
  regs->esp = user_stack;

  new_task->stack_top = (uint32_t)regs;
  new_task->heap_start = 0x40000000;
  new_task->heap_end = 0x40000000;

  new_task->next = task_list->next;
  task_list->next = new_task;
}

void switch_task() { asm volatile("int $32"); }

uint32_t schedule(uint32_t current_esp) {
  if (!current_task)
    return current_esp;

  /* Salva a pilha da tarefa que estava rodando */
  current_task->stack_top = current_esp;
  current_task->cpu_ticks++;

  /* Encontrar próxima tarefa que não esteja SLEEPING ou ZOMBIE */
  task_t *next = current_task->next;
  while (next->state == TASK_SLEEPING || next->state == TASK_ZOMBIE) {
    if (next == current_task)
      break; // Todas as outras estão impedidas? Volta pra atual.
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
  tss_entry.esp0 = current_task->kernel_stack;

  /* Retorna a nova pilha para o Assembly carregar no ESP */
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
  // Configura segmentos para user data (0x23 = 0x20 | 3) e user code (0x1B =
  // 0x18 | 3)
  asm volatile("cli; \
         mov $0x23, %ax; \
         mov %ax, %ds; \
         mov %ax, %es; \
         mov %ax, %fs; \
         mov %ax, %gs; \
         \
         mov %esp, %eax; \
         pushl $0x23; \
         pushl %eax; \
         pushf; \
         pop %eax; \
         or $0x200, %eax; \
         pushl %eax; \
         pushl $0x1B; \
         push $1f; \
         iret; \
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

  // Kernel Stack
  uint32_t kernel_stack_size = 4096;
  new_task->kernel_stack =
      (uint32_t)kmalloc(kernel_stack_size) + kernel_stack_size;

  // Processor State
  uint32_t regs_size = sizeof(registers_t);
  uint32_t new_stack_ptr = new_task->kernel_stack - regs_size;
  memcpy((void *)new_stack_ptr, regs, regs_size);

  registers_t *new_regs = (registers_t *)new_stack_ptr;
  new_regs->eax = 0; // Return 0 for child
  new_task->stack_top = new_stack_ptr;

  // Copy heap limits
  new_task->heap_start = parent->heap_start;
  new_task->heap_end = parent->heap_end;

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
      if (t->parent == parent) {
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

            // TODO: Free kernel stack and PCB
            return child_id;
          }
          found = true;
        }
      }
      t = t->next;
    } while (t != task_list);

    if (!any_child || (pid != -1 && !found))
      return -1;

    // Wait and reschedule
    parent->state = TASK_SLEEPING;
    switch_task();
  }
}

extern void graphics_exclusive_release(int pid);

void sys_exit_process(int status) {
  asm volatile("cli");
  graphics_exclusive_release(current_task->id);
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
