.global setjmp
.type setjmp, @function
setjmp:
    mov %rbx, (%rdi)
    mov %rsp, 8(%rdi)
    mov %rbp, 16(%rdi)
    mov %r12, 24(%rdi)
    mov %r13, 32(%rdi)
    mov %r14, 40(%rdi)
    mov %r15, 48(%rdi)
    mov (%rsp), %rax       /* return address */
    mov %rax, 56(%rdi)
    xor %rax, %rax
    ret

.global longjmp
.type longjmp, @function
longjmp:
    mov %rsi, %rax         /* return value */
    test %rax, %rax
    jnz 1f
    inc %rax               /* if val == 0, return 1 */
1:
    mov (%rdi), %rbx
    mov 8(%rdi), %rsp
    mov 16(%rdi), %rbp
    mov 24(%rdi), %r12
    mov 32(%rdi), %r13
    mov 40(%rdi), %r14
    mov 48(%rdi), %r15
    mov 56(%rdi), %rdx     /* return address */
    mov %rdx, (%rsp)
    ret
