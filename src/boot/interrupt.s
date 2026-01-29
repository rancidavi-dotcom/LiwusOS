/* interrupt.s — x86_64 interrupt handling */

.section .text

.macro PUSH_ALL
  push %r15
  push %r14
  push %r13
  push %r12
  push %r11
  push %r10
  push %r9
  push %r8
  push %rdi
  push %rsi
  push %rbp
  push %rbx
  push %rdx
  push %rcx
  push %rax
.endm

.macro POP_ALL
  pop %rax
  pop %rcx
  pop %rdx
  pop %rbx
  pop %rbp
  pop %rsi
  pop %rdi
  pop %r8
  pop %r9
  pop %r10
  pop %r11
  pop %r12
  pop %r13
  pop %r14
  pop %r15
.endm

.macro ISR_NOERRCODE num
  .global isr\num
  isr\num:
    cli
    push $0          /* Dummy error code */
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
.global isr_common_stub
isr_common_stub:
  PUSH_ALL

  mov $0x10, %ax          /* Kernel data segment */
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %fs
  mov %ax, %gs
  /* Loading a data selector into %gs cleared the GS base. Re-point
     IA32_GS_BASE at the BSP per-CPU block (cpus_local in sched/task.c).
     NOTE: SMP APs would need their own base here. */
  movabs $cpus_local, %rax
  mov %rax, %r10
  shr $32, %r10
  mov %r10d, %edx
  mov $0xC0000101, %ecx
  wrmsr

  mov %rsp, %rdi         /* First arg: pointer to registers_t */
  call isr_handler

  POP_ALL
  add $16, %rsp          /* Pop int_no and err_code (8 bytes each = 16) */
  iretq

.macro IRQ num, target
  .global irq\num
  irq\num:
    cli
    push $0               /* Dummy error code */
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
  PUSH_ALL

  mov $0x10, %ax          /* Kernel data segment */
  mov %ax, %ds
  mov %ax, %es
  mov %ax, %fs
  mov %ax, %gs
  /* Restore GS base (see comment in isr_common_stub) */
  movabs $cpus_local, %rax
  mov %rax, %r10
  shr $32, %r10
  mov %r10d, %edx
  mov $0xC0000101, %ecx
  wrmsr

  mov %rsp, %rdi          /* First arg: pointer to registers_t */
  call irq_handler
  mov %rax, %rsp          /* Schedule() returns new stack pointer */

  POP_ALL
  add $16, %rsp           /* Pop int_no and err_code */
  iretq

/* Syscall interrupt handler */
.global isr128
isr128:
  cli
  push $0                 /* Dummy error code */
  push $128               /* Interrupt number */
  jmp isr_common_stub
