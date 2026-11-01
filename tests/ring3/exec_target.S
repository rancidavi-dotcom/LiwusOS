.global _start
.type _start, @function
_start:
    xor %rbp, %rbp
    mov %rsp, %rdi
    call exec_target_check
    mov %rax, %rdi
    mov $1, %rax
    int $0x80
1:
    hlt
    jmp 1b
