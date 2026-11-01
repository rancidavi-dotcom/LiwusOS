.section .text
.global _start
_start:
    /* Linux x86-64 getpid(39). */
    mov $39, %rax
    syscall
    test %rax, %rax
    jle fail

    /* Linux mmap(9), fourth argument in R10. */
    mov $9, %rax
    xor %rdi, %rdi
    mov $4096, %rsi
    mov $3, %rdx                 /* PROT_READ | PROT_WRITE */
    mov $0x22, %r10               /* MAP_PRIVATE | MAP_ANONYMOUS */
    mov $-1, %r8
    xor %r9, %r9
    syscall
    test %rax, %rax
    js fail
    mov %rax, %rbx
    movb $0x5a, (%rbx)

    /* Linux munmap(11). */
    mov $11, %rax
    mov %rbx, %rdi
    mov $4096, %rsi
    syscall
    test %rax, %rax
    jne fail

    /* Linux write(1). */
    mov $1, %rax
    mov $1, %rdi
    lea message(%rip), %rsi
    mov $message_end-message, %rdx
    syscall
    cmp $message_end-message, %rax
    jne fail

    xor %rdi, %rdi
    jmp exit
fail:
    mov $1, %rdi
exit:
    mov $60, %rax
    syscall
    ud2

message:
    .ascii "Linux SYSCALL ABI: getpid/mmap/munmap/write PASS\n"
message_end:
