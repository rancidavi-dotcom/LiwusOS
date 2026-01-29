; Minimal static hello for LiwusOS - prints "OLAR DO TCC!" via int 0x80 syscall
; Build: nasm -f elf64 hello_static.asm -o hello_static.o && ld -static -o hello_static hello_static.o

section .text
global _start

_start:
    ; write(1, msg, len) via int 0x80
    mov rax, 4          ; sys_write
    mov rdi, 1          ; fd = stdout
    mov rsi, msg        ; buffer
    mov rdx, msg_len    ; length
    int 0x80

    ; exit(0) via int 0x80
    mov rax, 1          ; sys_exit
    mov rdi, 0          ; exit code
    int 0x80

section .rodata
msg: db "OLAR DO TCC! ", 0
msg_len equ $ - msg

; argv[0] string for display
argv0: db "/hello", 0