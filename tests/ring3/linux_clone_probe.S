.section .text
.global _start
_start:
  movl $1, fail_stage(%rip)       /* argument and futex ABI validation */
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

  /* FUTEX_PRIVATE: validate operation flags, alignment and expected value. */
  mov $202, %rax
  lea futex_word+1(%rip), %rdi
  mov $128, %rsi                   /* FUTEX_WAIT_PRIVATE */
  xor %rdx, %rdx
  xor %r10, %r10
  syscall
  cmp $-22, %rax                   /* -EINVAL for unaligned word */
  jne fail
  mov $202, %rax
  lea futex_word(%rip), %rdi
  mov $128, %rsi
  mov $1, %rdx                     /* actual word starts at zero */
  xor %r10, %r10
  syscall
  cmp $-11, %rax                   /* -EAGAIN */
  jne fail
  mov $202, %rax
  lea readonly_futex_word(%rip), %rdi
  mov $128, %rsi
  xor %rdx, %rdx                   /* value is one; WAIT must only need read */
  xor %r10, %r10
  syscall
  cmp $-11, %rax                   /* readable, read-only word mismatches */
  jne fail
  mov $202, %rax
  mov $0x1000, %rdi                /* null guard page */
  mov $129, %rsi                   /* FUTEX_WAKE_PRIVATE */
  mov $1, %rdx
  syscall
  cmp $-14, %rax                   /* -EFAULT */
  jne fail
  mov $202, %rax
  lea futex_word(%rip), %rdi
  xor %rsi, %rsi                   /* shared mode lacks backing identity */
  xor %rdx, %rdx
  syscall
  cmp $-38, %rax                   /* -ENOSYS */
  jne fail
  mov $202, %rax
  lea futex_word(%rip), %rdi
  mov $129, %rsi                   /* no waiter */
  mov $1, %rdx
  syscall
  test %rax, %rax
  jne fail                         /* wake without waiter returns zero */
  mov $202, %rax
  lea futex_word(%rip), %rdi
  mov $128, %rsi
  xor %rdx, %rdx
  lea futex_timeout(%rip), %r10
  syscall
  cmp $-38, %rax                   /* timeout is not implemented */
  jne fail

  /* Give the parent and the child distinct, ordinary user memory as TLS. */
  mov $158, %rax                   /* arch_prctl(ARCH_SET_FS, parent_tls) */
  movl $2, fail_stage(%rip)       /* parent TLS and child-stack setup */
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
  movl $3, fail_stage(%rip)       /* clone creation and parent TLS */
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
parent_futex_ready:
  movl $4, fail_stage(%rip)       /* parent/child futex handshake */
  cmpl $1, futex_ready(%rip)
  je parent_futex_release
  mov $202, %rax
  lea futex_ready(%rip), %rdi
  mov $128, %rsi                   /* FUTEX_WAIT_PRIVATE */
  xor %rdx, %rdx
  xor %r10, %r10
  syscall
  test %rax, %rax
  jz parent_futex_ready
  cmp $-11, %rax                   /* store raced with queue insertion */
  jne fail
  jmp parent_futex_ready
parent_futex_release:
  movl $1, futex_word(%rip)
  mov $202, %rax
  lea futex_word(%rip), %rdi
  mov $129, %rsi                   /* FUTEX_WAKE_PRIVATE */
  mov $1, %rdx
  syscall
  test %rax, %rax
  js fail                          /* no error; zero is legal if WAIT has not queued */
  cmp $1, %rax
  ja fail
parent_wait:
  cmpl $1, futex_done(%rip)
  je parent_done
  mov $202, %rax
  lea futex_done(%rip), %rdi
  mov $128, %rsi
  xor %rdx, %rdx
  xor %r10, %r10
  syscall
  test %rax, %rax
  jz parent_wait
  cmp $-11, %rax
  jne fail
  jmp parent_wait
parent_done:
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
  movl $5, fail_stage(%rip)       /* child TLS and thread-only exit */
  mov %fs:0, %rax
  movabs $0x4348494c44544c53, %r11
  cmp %r11, %rax
  jne child_fail
  movl $1, futex_ready(%rip)
  mov $202, %rax
  lea futex_ready(%rip), %rdi
  mov $129, %rsi
  mov $1, %rdx
  syscall
  mov $202, %rax
  lea futex_word(%rip), %rdi
  mov $128, %rsi
  xor %rdx, %rdx
  xor %r10, %r10
  syscall
  test %rax, %rax
  jz child_wait_complete           /* woken after it queued */
  cmp $-11, %rax                   /* value changed before queue insertion */
  jne child_fail
child_wait_complete:
  cmpl $1, futex_word(%rip)
  jne child_fail
  movq $1, child_ok(%rip)
  movq $1, done(%rip)
  movl $1, futex_done(%rip)
  mov $202, %rax
  lea futex_done(%rip), %rdi
  mov $129, %rsi
  mov $1, %rdx
  syscall
  mov $60, %rax                    /* exit this thread only */
  xor %rdi, %rdi
  syscall
  ud2
child_fail:
  movq $1, done(%rip)
  movl $1, futex_done(%rip)
  mov $202, %rax
  lea futex_done(%rip), %rdi
  mov $129, %rsi
  mov $1, %rdx
  syscall
  mov $60, %rax
  mov $6, %rdi
  syscall
  ud2

fail:
  mov $231, %rax
  mov fail_stage(%rip), %rdi
  syscall
  ud2

.section .data
.align 16
parent_tls:
  .quad 0x504152454e54544c
child_tls:
  .quad 0x4348494c44544c53
futex_timeout:
  .quad 0
  .quad 1
.section .bss
.align 8
fail_stage:
  .long 0
  .align 8
done:
  .quad 0
child_ok:
  .quad 0
child_tid:
  .quad 0
futex_word:
  .long 0
futex_ready:
  .long 0
futex_done:
  .long 0
.section .rodata
.align 4
readonly_futex_word:
  .long 1
