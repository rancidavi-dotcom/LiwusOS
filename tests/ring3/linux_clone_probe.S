.section .text
.global _start
_start:
  /* Invalid flag combinations must fail before creating a task. */
  mov $56, %rax
  mov $0x100, %rdi                 /* CLONE_VM without the thread contract */
  xor %rsi, %rsi
  xor %rdx, %rdx
  xor %r10, %r10
  xor %r8, %r8
  syscall
  cmp $-22, %rax                   /* -EINVAL */
  jne fail

  /* Give the parent and the child distinct, ordinary user memory as TLS. */
  mov $158, %rax                   /* arch_prctl(ARCH_SET_FS, parent_tls) */
  mov $0x1002, %rdi
  lea parent_tls(%rip), %rsi
  syscall
  test %rax, %rax
  jne fail

  /* Allocate a private user stack for the child thread. */
  mov $9, %rax                     /* mmap(NULL, 8192, RW, PRIVATE|ANON, -1, 0) */
  xor %rdi, %rdi
  mov $8192, %rsi
  mov $3, %rdx
  mov $0x22, %r10
  mov $-1, %r8
  xor %r9, %r9
  syscall
  cmp $0, %rax
  jl fail
  lea 8192(%rax), %rsi

  /* CLONE_VM|FS|FILES|SIGHAND|THREAD|SETTLS. */
  mov $56, %rax
  mov $0x90f00, %rdi
  xor %rdx, %rdx
  xor %r10, %r10
  lea child_tls(%rip), %r8
  syscall
  test %rax, %rax
  js fail
  jz child

parent:
  mov %rax, child_tid(%rip)
  mov %fs:0, %rax
  movabs $0x504152454e54544c, %r11
  cmp %r11, %rax
  jne fail
parent_wait:
  cmpq $0, done(%rip)
  je parent_wait
  cmpq $1, child_ok(%rip)
  jne fail
  /* CLONE_SETTLS must not have changed the parent's FS base. */
  mov %fs:0, %rax
  movabs $0x504152454e54544c, %r11
  cmp %r11, %rax
  jne fail
  mov $231, %rax                   /* exit_group(0) */
  xor %rdi, %rdi
  syscall

child:
  mov %fs:0, %rax
  movabs $0x4348494c44544c53, %r11
  cmp %r11, %rax
  jne child_fail
  movq $1, child_ok(%rip)
  movq $1, done(%rip)
  mov $60, %rax                    /* exit this thread only */
  xor %rdi, %rdi
  syscall
  ud2
child_fail:
  movq $1, done(%rip)
  mov $60, %rax
  mov $1, %rdi
  syscall
  ud2

fail:
  mov $231, %rax
  mov $1, %rdi
  syscall
  ud2

.section .data
.align 16
parent_tls:
  .quad 0x504152454e54544c
child_tls:
  .quad 0x4348494c44544c53
.section .bss
.align 8
done:
  .quad 0
child_ok:
  .quad 0
child_tid:
  .quad 0
