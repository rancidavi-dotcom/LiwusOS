.global _start
.type _start, @function
_start:
    xor %rbp, %rbp
    mov %rsp, %rdi
    and $-16, %rsp
    call liw_crt_start
1:
    hlt
    jmp 1b
