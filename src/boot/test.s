.global _start

_start:
    /* sys_write(1, msg, 14) */
    mov $4, %eax
    mov $1, %ebx
    mov $msg, %ecx
    mov $14, %edx
    int $0x80

    /* sys_exit(0) */
    mov $1, %eax
    mov $0, %ebx
    int $0x80

msg:
    .ascii "Hello from ELF"
