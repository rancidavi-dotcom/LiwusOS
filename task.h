#ifndef TASK_H
#define TASK_H

#include <stdint.h>

/* Estrutura que guarda o estado da CPU para uma tarefa */
struct context {
    uint32_t esp;
    uint32_t ebp;
    uint32_t ebx;
    uint32_t esi;
    uint32_t edi;
    uint32_t eflags;
};

typedef struct task {
    int id;
    uint32_t stack_top;      /* Ponteiro para o topo da pilha da tarefa */
    struct task* next;       /* Próxima tarefa na lista circular */
} task_t;

void init_tasking();
void create_task(void (*entry_point)());
void switch_task();

#endif
