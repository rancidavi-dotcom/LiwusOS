.section .text
.global _start
_start:
    xor %rbp, %rbp
    mov (%rsp), %rdi        /* argc */
    lea 8(%rsp), %rsi       /* argv */
    xor %rdx, %rdx          /* envp = NULL */
    andq $-16, %rsp         /* align stack to 16 bytes */

    call main

    mov %rax, %rdi
    call _exit
    hlt
