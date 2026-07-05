.section .text
.global _start
_start:
    xor %rbp, %rbp
    mov (%rsp), %rdi
    lea 8(%rsp), %rsi
    xor %rdx, %rdx
    andq $-16, %rsp
    call main
    mov %rax, %rdi
    call _exit
    hlt
