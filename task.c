#include "task.h"
#include "kheap.h"
#include <stddef.h>

typedef struct {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} regs_t;

task_t* current_task = NULL;
task_t* task_list = NULL;

void init_tasking() {
    current_task = (task_t*)kmalloc(sizeof(task_t));
    current_task->id = 0;
    current_task->stack_top = 0; /* Será definido na primeira interrupção */
    current_task->next = current_task;
    task_list = current_task;
}

void create_task(void (*entry_point)()) {
    task_t* new_task = (task_t*)kmalloc(sizeof(task_t));
    new_task->id = 1;

    uint32_t stack = (uint32_t)kmalloc(4096) + 4096;
    
    /* Prepara a pilha para o iret */
    regs_t* regs = (regs_t*)(stack - sizeof(regs_t));
    
    regs->eflags = 0x202; /* Interrupções habilitadas */
    regs->cs = 0x08;
    regs->eip = (uint32_t)entry_point;
    
    regs->ds = 0x10;
    regs->eax = 0; regs->ebx = 0; regs->ecx = 0; regs->edx = 0;
    regs->esi = 0; regs->edi = 0; regs->ebp = 0;
    
    new_task->stack_top = (uint32_t)regs;

    new_task->next = task_list->next;
    task_list->next = new_task;
}

void switch_task() {
    asm volatile("int $32");
}

uint32_t schedule(uint32_t current_esp) {
    if (!current_task) return current_esp;

    /* Salva a pilha da tarefa que estava rodando */
    current_task->stack_top = current_esp;
    
    /* Pula para a próxima */
    current_task = current_task->next;
    
    /* Retorna a nova pilha para o Assembly carregar no ESP */
    return current_task->stack_top;
}