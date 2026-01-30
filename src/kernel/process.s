/* process.s */

.global switch_to_task
switch_to_task:
    /* Salva o contexto da tarefa atual */
    push %ebp
    push %ebx
    push %esi
    push %edi

    /* Salva o ESP antigo na estrutura da tarefa */
    mov 20(%esp), %eax      /* Primeiro argumento: &(old->stack_top) */
    mov %esp, (%eax)

    /* Carrega o novo ESP da próxima tarefa */
    mov 24(%esp), %eax      /* Segundo argumento: new_esp */
    mov %eax, %esp

    /* Restaura o contexto da nova tarefa */
    pop %edi
    pop %esi
    pop %ebx
    pop %ebp

    ret                     /* Pula para o endereço que estiver no topo da nova pilha! */
