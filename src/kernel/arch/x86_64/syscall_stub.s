/* Linux x86-64 SYSCALL entry. Keep the frame layout synchronized with
 * registers_t in include/kernel/isr.h. The native int 0x80 path is separate. */
.text
.global linux_syscall_entry
.extern linux_syscall_handler

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

linux_syscall_entry:
  swapgs
  mov %rsp, %gs:24                 /* cpu_local_t.syscall_user_rsp */
  mov %gs:16, %rsp                 /* cpu_local_t.kernel_stack */

  /* Build the same interrupt-return tail used by registers_t. */
  pushq $0x33                       /* 64-bit user SS */
  pushq %gs:24                      /* user RSP */
  push %r11                         /* user RFLAGS */
  pushq $0x2b                       /* 64-bit user CS */
  push %rcx                         /* user RIP */
  pushq $0                           /* error code */
  pushq $0x80                        /* synthetic syscall vector */
  PUSH_ALL

  mov %rsp, %rdi
  call linux_syscall_handler

  POP_ALL
  add $16, %rsp                     /* synthetic vector and error code */
  swapgs
  iretq
