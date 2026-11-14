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
  mov $218, %rax                   /* set_tid_address invalid pointer */
  mov $0x1000, %rdi
  syscall
  cmp $-14, %rax
  jne fail
  mov $218, %rax                   /* valid set_tid_address registration */
  lea parent_clear_tid(%rip), %rdi
  syscall
  test %rax, %rax
  jle fail
  mov %eax, parent_tid_self(%rip)
  mov $186, %rax                   /* registration returns the current TID */
  syscall
  cmp parent_tid_self(%rip), %eax
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
  cmp $-110, %rax                  /* relative timeout expires */
  jne fail
  mov $202, %rax
  lea futex_word(%rip), %rdi
  mov $128, %rsi
  xor %rdx, %rdx
  lea futex_bad_nsec(%rip), %r10
  syscall
  cmp $-22, %rax                   /* invalid tv_nsec */
  jne fail
  mov $202, %rax
  lea futex_word(%rip), %rdi
  mov $128, %rsi
  xor %rdx, %rdx
  mov $0x1000, %r10                /* null guard page is not a timespec */
  syscall
  cmp $-14, %rax                   /* -EFAULT */
  jne fail

  /* Give the parent and the child distinct, ordinary user memory as TLS. */
  mov $158, %rax                   /* arch_prctl(ARCH_SET_FS, parent_tls) */
  movl $2, fail_stage(%rip)       /* parent TLS and child-stack setup */
  mov $0x1002, %rdi
  lea parent_tls(%rip), %rsi
  syscall
  test %rax, %rax
  jne fail
  mov $228, %rax                   /* bounded absolute deadline for bitset wait */
  mov $1, %rdi                     /* CLOCK_MONOTONIC */
  lea futex_deadline(%rip), %rsi
  syscall
  test %rax, %rax
  jne fail
  addq $5, futex_deadline(%rip)    /* test must never wait indefinitely */

  mov $157, %rax                   /* Save parent thread's Linux comm name. */
  mov $16, %rdi                    /* PR_GET_NAME */
  lea parent_comm_before(%rip), %rsi
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

  /* Child-TID pointers are writable user addresses and must be validated. */
  mov $56, %rax
  mov $0x1d0f00, %rdi             /* includes CLONE_PARENT_SETTID */
  mov $0x1000, %rdx               /* invalid parent_tid */
  xor %r10, %r10
  lea child_tls(%rip), %r8
  syscall
  cmp $-14, %rax                   /* -EFAULT, no child created */
  jne fail

  mov $56, %rax
  mov $0x12d0f00, %rdi             /* invalid CLONE_CHILD_SETTID address */
  xor %rdx, %rdx
  mov $0x1000, %r10
  lea child_tls(%rip), %r8
  syscall
  cmp $-14, %rax                   /* -EFAULT, no child created */
  jne fail

  mov $56, %rax
  mov $0x2d0f00, %rdi             /* includes CLONE_CHILD_CLEARTID */
  xor %rdx, %rdx
  mov $0x1000, %r10               /* invalid child_tid */
  lea child_tls(%rip), %r8
  syscall
  cmp $-14, %rax                   /* -EFAULT, no child created */
  jne fail

  /* Thread clone with parent/child TID publication, clear/wake and TLS. */
  mov $56, %rax
  movl $3, fail_stage(%rip)       /* clone creation and parent TLS */
  mov $0x17d0f00, %rdi
  lea clone_parent_tid(%rip), %rdx
  lea clear_tid_word(%rip), %r10
  lea child_tls(%rip), %r8
  syscall
  test %rax, %rax
  js fail
  jz child

parent:
  mov %rax, child_tid(%rip)
  cmp clone_parent_tid(%rip), %eax /* CLONE_PARENT_SETTID */
  jne fail
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
  mov $0x89, %rsi                  /* FUTEX_WAIT_BITSET_PRIVATE */
  xor %rdx, %rdx
  lea futex_deadline(%rip), %r10   /* absolute CLOCK_MONOTONIC timeout */
  mov $2, %r9                      /* val3 waiter mask */
  syscall
  test %rax, %rax
  jz parent_futex_ready
  cmp $-11, %rax                   /* store raced with queue insertion */
  je parent_futex_ready
  cmp $-110, %rax                  /* deadline bounds a failed wake path */
  jne parent_futex_unexpected
  movl $41, fail_stage(%rip)
  jmp fail
parent_futex_unexpected:
  movl $42, fail_stage(%rip)
  jmp fail
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
  lea futex_guard_timeout(%rip), %r10
  syscall
  test %rax, %rax
  jz parent_wait
  cmp $-11, %rax
  jne fail
  jmp parent_wait
parent_done:
  cmpq $1, child_ok(%rip)
  jne fail
parent_clear_tid_wait:
  mov clear_tid_word(%rip), %eax
  test %eax, %eax
  jz parent_clear_tid_done
  cmp child_tid(%rip), %eax
  jne fail
  mov %eax, %edx
  mov $202, %rax
  lea clear_tid_word(%rip), %rdi
  mov $128, %rsi                   /* wait until set_tid_address clears it */
  lea futex_guard_timeout(%rip), %r10
  syscall
  test %rax, %rax
  jz parent_clear_tid_wait
  cmp $-11, %rax
  jne fail
  jmp parent_clear_tid_wait
parent_clear_tid_done:
  /* CLONE_SETTLS must not have changed the parent's FS base. */
  mov %fs:0, %rax
  movabs $0x504152454e54544c, %r11
  cmp %r11, %rax
  jne fail
  /* Renaming the child thread must not mutate the parent's comm. */
  mov $157, %rax
  mov $16, %rdi
  lea parent_comm_after(%rip), %rsi
  syscall
  test %rax, %rax
  jne fail
  lea parent_comm_before(%rip), %rsi
  lea parent_comm_after(%rip), %rdi
  mov $16, %rcx
  repe cmpsb
  jne fail
  mov $231, %rax                   /* exit_group(0) */
  xor %rdi, %rdi
  syscall

child:
  movl $5, fail_stage(%rip)       /* child TLS and thread-only exit */
  /* clone inherits the parent's name, then stores the child's name locally. */
  mov $157, %rax
  mov $16, %rdi                    /* PR_GET_NAME */
  lea child_comm_output(%rip), %rsi
  syscall
  test %rax, %rax
  jne child_fail
  lea parent_comm_before(%rip), %rsi
  lea child_comm_output(%rip), %rdi
  mov $16, %rcx
  repe cmpsb
  jne child_fail
  mov $157, %rax
  mov $15, %rdi                    /* PR_SET_NAME */
  lea child_comm_input(%rip), %rsi
  syscall
  test %rax, %rax
  jne child_fail
  mov $157, %rax
  mov $16, %rdi
  lea child_comm_output(%rip), %rsi
  syscall
  test %rax, %rax
  jne child_fail
  lea child_comm_expected(%rip), %rsi
  lea child_comm_output(%rip), %rdi
  mov $16, %rcx
  repe cmpsb
  jne child_fail
  mov $186, %rax                   /* gettid */
  syscall
  cmp clear_tid_word(%rip), %eax
  jne child_fail
  mov %eax, clear_tid_word(%rip)  /* value already published by CLONE_CHILD_SETTID */
  mov %fs:0, %rax
  movabs $0x4348494c44544c53, %r11
  cmp %r11, %rax
  jne child_fail
  movl $1, futex_ready(%rip)
  mov $202, %rax
  lea futex_ready(%rip), %rdi
  mov $0x8a, %rsi                  /* FUTEX_WAKE_BITSET_PRIVATE */
  mov $1, %rdx
  xor %r10, %r10
  mov $2, %r9                      /* must intersect the waiter's mask */
  syscall
  mov $202, %rax
  lea futex_word(%rip), %rdi
  mov $128, %rsi
  xor %rdx, %rdx
  lea futex_guard_timeout(%rip), %r10
  syscall
  test %rax, %rax
  jz child_wait_complete           /* woken after it queued */
  cmp $-11, %rax                   /* value changed before queue insertion */
  jne child_fail
child_wait_complete:
  cmpl $1, futex_word(%rip)
  jne child_fail
  mov $186, %rax                   /* gettid must identify the cloned thread */
  syscall
  cmp clear_tid_word(%rip), %eax
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
  .quad 10000000                   /* 10 ms, rounded to one scheduler tick */
futex_guard_timeout:
  .quad 2
  .quad 0                           /* bound test handshakes; no infinite wait */
futex_bad_nsec:
  .quad 0
  .quad 1000000000
.section .bss
.align 8
futex_deadline:
  .skip 16
fail_stage:
  .long 0
  .align 8
done:
  .quad 0
child_ok:
  .quad 0
child_tid:
  .quad 0
clear_tid_word:
  .long 0
clone_parent_tid:
  .long 0
parent_clear_tid:
  .long 0
parent_comm_before:
  .skip 16
parent_comm_after:
  .skip 16
child_comm_output:
  .skip 16
parent_tid_self:
  .long 0
futex_word:
  .long 0
futex_ready:
  .long 0
futex_done:
  .long 0
.section .rodata
.align 4
child_comm_input:
  .asciz "liwus-worker"
child_comm_expected:
  .ascii "liwus-worker"
  .zero 4
.align 4
readonly_futex_word:
  .long 1
