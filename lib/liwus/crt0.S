.global _start
.type _start, @function
_start:
    xor %rbp, %rbp
    /* Stack pointer points to initial stack as set up by execve */
    mov %rsp, %rdi
    and $-16, %rsp
    call liw_crt_start
1:
    hlt
    jmp 1b
