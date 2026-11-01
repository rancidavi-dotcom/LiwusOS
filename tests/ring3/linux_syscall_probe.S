.section .text
.global _start
_start:
    /* Linux x86-64 getpid(39). */
    mov $39, %rax
    syscall
    test %rax, %rax
    jle fail

    /* Linux fstat(5) and Linux struct stat's x86-64 field offsets. */
    mov $5, %rax
    mov $1, %rdi
    lea statbuf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov statbuf+24(%rip), %eax
    and $0xf000, %eax
    cmp $0x2000, %eax             /* S_IFCHR */
    jne fail

    /* openat(257) translates Linux flags and negative errno values. */
    mov $257, %rax
    mov $-100, %rdi               /* AT_FDCWD */
    lea missing_path(%rip), %rsi
    xor %rdx, %rdx                /* O_RDONLY */
    xor %r10, %r10                /* mode */
    syscall
    cmp $-2, %rax                 /* -ENOENT */
    jne fail

    mov $10000, %rax
    syscall
    cmp $-38, %rax                /* -ENOSYS */
    jne fail

    /* Linux arch_prctl(ARCH_SET_FS / ARCH_GET_FS) and FS:0 TLS access. */
    mov $158, %rax
    mov $0x1002, %rdi             /* ARCH_SET_FS */
    lea tls_word(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov %fs:0, %rax
    movabs $0x123456789abcdef0, %rdx
    cmp %rdx, %rax
    jne fail
    mov $158, %rax
    mov $0x1003, %rdi             /* ARCH_GET_FS */
    lea fs_result(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    lea tls_word(%rip), %rax
    cmp fs_result(%rip), %rax
    jne fail

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
    .ascii "Linux SYSCALL ABI: getpid/fstat/openat/mmap/munmap/FS-TLS/write PASS\n"
message_end:

missing_path:
    .asciz "/liwusos-abi-probe-missing"

.section .bss
.align 16
statbuf:
    .skip 144
fs_result:
    .skip 8

.section .data
.align 8
tls_word:
    .quad 0x123456789abcdef0
