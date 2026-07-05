/* process.s — x86_64 task switching */

.global switch_to_task
switch_to_task:
  /* Save context of current task */
  push %rbp
  push %rbx
  push %rsi
  push %rdi
  push %r12
  push %r13
  push %r14
  push %r15

  /* Save old stack pointer: arg1 = &(old->stack_top) */
  mov 8(%rsp), %rdi     /* First argument is at rsp+8 after 8 pushes (64 bytes) */
  mov %rsp, (%rdi)

  /* Load new stack pointer: arg2 = new_esp */
  mov 16(%rsp), %rsi    /* Second argument */
  mov %rsi, %rsp

  /* Restore context of new task */
  pop %r15
  pop %r14
  pop %r13
  pop %r12
  pop %rdi
  pop %rsi
  pop %rbx
  pop %rbp

  ret
