/* interrupt.s */

.global idt_flush
idt_flush:
    mov 4(%esp), %eax
    lidt (%eax)
    ret

.macro ISR_NOERRCODE num
  .global isr\num
  isr\num:
    cli
    push $0
    push $\num
    jmp isr_common_stub
.endm

.macro ISR_ERRCODE num
  .global isr\num
  isr\num:
    cli
    push $\num
    jmp isr_common_stub
.endm

ISR_NOERRCODE 0
/* ... (continuar com as outras 31 ISRs se necessário, mas vamos focar nas IRQs para o fix) ... */
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE   8
ISR_NOERRCODE 9
ISR_ERRCODE   10
ISR_ERRCODE   11
ISR_ERRCODE   12
ISR_ERRCODE   13
ISR_ERRCODE   14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE   17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

.extern isr_handler
isr_common_stub:
    pusha
    mov %ds, %ax
    push %eax
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    call isr_handler
    pop %eax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    popa
    add $8, %esp
    iret

.macro IRQ num, target
  .global irq\num
  irq\num:
    cli
    push $0
    push $\target
    jmp irq_common_stub
.endm

IRQ 0, 32
IRQ 1, 33
IRQ 2, 34
IRQ 3, 35
IRQ 4, 36
IRQ 5, 37
IRQ 6, 38
IRQ 7, 39
IRQ 8, 40
IRQ 9, 41
IRQ 10, 42
IRQ 11, 43
IRQ 12, 44
IRQ 13, 45
IRQ 14, 46
IRQ 15, 47

.extern irq_handler
irq_common_stub:
    pusha                    /* Salva TUDO: EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI */
    mov %ds, %ax
    push %eax                /* Salva DS */

    mov $0x10, %ax           /* Carrega segmentos do Kernel */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp                /* Passa o ponteiro da pilha atual para o C */
    call irq_handler
    mov %eax, %esp           /* A MAGIA: O C retorna o novo ESP, e nós mudamos de pilha aqui! */

    pop %eax                 /* Restaura DS da nova tarefa */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    popa                     /* Restaura registros da nova tarefa */
    add $8, %esp
    iret                     /* Retorna para a nova tarefa com EFLAGS/Interrupts restaurados! */