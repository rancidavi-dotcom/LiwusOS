.section .text
.global _start
_start:
    /* The child re-execs this ELF with argc=2 to validate Linux execve. */
    cmpq $2, (%rsp)
    jne .Lnormal_entry
    mov 16(%rsp), %rax
    test %rax, %rax
    jz .Laux_startup
    cmpb $'x', (%rax)              /* external-ELF exec test mode */
    je .Lexternal_linux_exec
    jmp .Laux_startup
.Lnormal_entry:
    mov $1, %r12d
    jmp .Lmain_probe
.Laux_startup:
    test $0xf, %rsp                /* Linux x86-64 entry stack alignment */
    jne fail
    mov (%rsp), %rcx
    lea 8(%rsp,%rcx,8), %r10       /* argv[argc] */
.Laux_skip_argv:
    cmpq $0, (%r10)
    je .Laux_skip_env_start
    add $8, %r10
    jmp .Laux_skip_argv
.Laux_skip_env_start:
    add $8, %r10                   /* envp starts after argv NULL */
.Laux_skip_env:
    cmpq $0, (%r10)
    je .Laux_start
    add $8, %r10
    jmp .Laux_skip_env
.Laux_start:
    add $8, %r10                   /* first Elf64_auxv_t */
    xor %r9d, %r9d                 /* required-entry mask */
    mov $173, %r12d                /* diagnostic: reached auxv */
.Laux_loop:
    mov 0(%r10), %rax              /* a_type */
    mov 8(%r10), %rdx              /* a_val */
    add $16, %r10
    mov %eax, %r12d                /* diagnostic: current auxv type */
    test %rax, %rax
    jz .Laux_done
    cmp $3, %rax                   /* AT_PHDR */
    jne .Laux_phent
    cmp $0x400040, %rdx
    jne fail
    or $0x0001, %r9d
    jmp .Laux_loop
.Laux_phent:
    cmp $4, %rax                   /* AT_PHENT */
    jne .Laux_phnum
    cmp $56, %rdx
    jne fail
    or $0x0002, %r9d
    jmp .Laux_loop
.Laux_phnum:
    cmp $5, %rax                   /* AT_PHNUM */
    jne .Laux_pagesz
    cmp $3, %rdx
    jne fail
    or $0x0004, %r9d
    jmp .Laux_loop
.Laux_pagesz:
    cmp $6, %rax                   /* AT_PAGESZ */
    jne .Laux_base
    cmp $4096, %rdx
    jne fail
    or $0x0008, %r9d
    jmp .Laux_loop
.Laux_base:
    cmp $7, %rax                   /* AT_BASE: static executable */
    jne .Laux_entry
    test %rdx, %rdx
    jne fail
    or $0x0010, %r9d
    jmp .Laux_loop
.Laux_entry:
    cmp $9, %rax                   /* AT_ENTRY */
    jne .Laux_brk
    lea _start(%rip), %rcx
    cmp %rcx, %rdx
    jne fail
    or $0x0020, %r9d
    jmp .Laux_loop
.Laux_brk:
    cmp $10, %rax                  /* AT_BRK: exact initial program break */
    jne .Laux_uid
    test %rdx, %rdx
    je fail
    test $4095, %rdx               /* Linux initializes start_brk page-aligned */
    jnz fail
    mov %rdx, aux_brk_value(%rip)
    or $0x20000, %r9d
    jmp .Laux_loop
.Laux_uid:
    cmp $11, %rax                  /* AT_UID */
    jne .Laux_euid
    test %rdx, %rdx
    jne fail
    or $0x0040, %r9d
    jmp .Laux_loop
.Laux_euid:
    cmp $12, %rax                  /* AT_EUID */
    jne .Laux_gid
    test %rdx, %rdx
    jne fail
    or $0x0080, %r9d
    jmp .Laux_loop
.Laux_gid:
    cmp $13, %rax                  /* AT_GID */
    jne .Laux_egid
    test %rdx, %rdx
    jne fail
    or $0x0100, %r9d
    jmp .Laux_loop
.Laux_egid:
    cmp $14, %rax                  /* AT_EGID */
    jne .Laux_platform
    test %rdx, %rdx
    jne fail
    or $0x0200, %r9d
    jmp .Laux_loop
.Laux_platform:
    cmp $15, %rax                  /* AT_PLATFORM */
    jne .Laux_hwcap
    cmpl $0x5f363878, (%rdx)       /* "x86_64" */
    jne fail
    cmpw $0x3436, 4(%rdx)
    jne fail
    cmpb $0, 6(%rdx)
    jne fail
    or $0x0400, %r9d
    jmp .Laux_loop
.Laux_hwcap:
    cmp $16, %rax                  /* AT_HWCAP: Linux x86 CPUID.1:EDX */
    jne .Laux_clktck
    mov %rdx, %r11                 /* CPUID clobbers RDX. */
    push %rbx
    mov $1, %eax
    xor %ecx, %ecx
    cpuid
    pop %rbx
    cmp %edx, %r11d
    jne fail
    or $0x8000, %r9d
    jmp .Laux_loop
.Laux_clktck:
    cmp $17, %rax                  /* AT_CLKTCK */
    jne .Laux_secure
    cmp $100, %rdx
    jne fail
    or $0x0800, %r9d
    jmp .Laux_loop
.Laux_secure:
    cmp $23, %rax                  /* AT_SECURE */
    jne .Laux_random
    test %rdx, %rdx
    jne fail
    or $0x1000, %r9d
    jmp .Laux_loop
.Laux_random:
    cmp $25, %rax                  /* AT_RANDOM */
    jne .Laux_hwcap2
    test %rdx, %rdx
    je fail
    mov (%rdx), %rcx
    or 8(%rdx), %rcx
    je fail
    or $0x2000, %r9d
    jmp .Laux_loop
.Laux_hwcap2:
    cmp $26, %rax                  /* AT_HWCAP2 */
    jne .Laux_execfn
    test %rdx, %rdx                /* no ring-3 MWAIT or FSGSBASE */
    jne fail
    or $0x10000, %r9d
    jmp .Laux_loop
.Laux_execfn:
    cmp $31, %rax                  /* AT_EXECFN */
    jne .Laux_loop
    cmpb $'l', (%rdx)
    jne fail
    or $0x4000, %r9d
    jmp .Laux_loop
.Laux_done:
    cmp $0x3ffff, %r9d
    jne .Laux_missing_entry
    mov $1, %r12d
    jmp .Laux_valid
.Laux_missing_entry:
    mov $173, %r12d
    jmp fail
.Laux_valid:
    jmp linux_exec_child
.Lexternal_linux_exec:
    mov $1, %r12d
    mov $59, %rax                  /* execve external static ELF */
    lea external_linux_path(%rip), %rdi
    lea external_linux_argv(%rip), %rsi
    lea external_linux_envp(%rip), %rdx
    syscall
    mov $60, %rax                  /* any return means failure */
    mov $1, %rdi
    syscall
    ud2
.Lmain_probe:
    /* getrandom(318): CPUID feature selection, transfer, flags and pointers. */
    mov $1, %eax
    cpuid
    bt $30, %ecx                  /* RDRAND advertised by CPUID.1:ECX */
    jc .Lgetrandom_supported
    mov $318, %rax
    lea random_result(%rip), %rdi
    mov $16, %rsi
    xor %rdx, %rdx
    syscall
    cmp $-5, %rax                  /* no weak fallback when hardware RNG absent */
    jne fail
    jmp .Lgetrandom_errors
.Lgetrandom_supported:
    mov $318, %rax
    lea random_result(%rip), %rdi
    mov $16, %rsi
    mov $3, %rdx                   /* GRND_NONBLOCK | GRND_RANDOM */
    syscall
    cmp $16, %rax
    jne fail
    mov random_result(%rip), %rax
    or random_result+8(%rip), %rax
    jz fail
.Lgetrandom_errors:
    mov $318, %rax                 /* unknown flags */
    lea random_result(%rip), %rdi
    mov $1, %rsi
    mov $4, %rdx
    syscall
    cmp $-22, %rax
    jne fail
    mov $318, %rax                 /* invalid pointer */
    movabs $0x0000800000000000, %rdi
    mov $1, %rsi
    xor %rdx, %rdx
    syscall
    cmp $-14, %rax
    jne fail
    mov $318, %rax                 /* zero count does not access the pointer */
    movabs $0x0000800000000000, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail

    /* AVX state must survive a blocking syscall and scheduler context switch. */
    mov $1, %eax
    cpuid
    mov %ecx, %r8d
    and $0x14000000, %r8d         /* hardware XSAVE and AVX */
    cmp $0x14000000, %r8d
    jne .Lavx_probe_done
    test $0x08000000, %ecx         /* hardware present, OS must enable XSAVE */
    jz fail
    xor %ecx, %ecx
    xgetbv
    and $7, %eax                  /* x87, SSE and AVX state enabled in XCR0 */
    cmp $7, %eax
    jne fail
    vmovdqu xstate_pattern(%rip), %ymm0
    mov $35, %rax                 /* nanosleep blocks and forces scheduling */
    lea sleep_timespec(%rip), %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    jne fail
    vmovdqu xstate_pattern(%rip), %ymm1
    vpcmpeqb %ymm0, %ymm1, %ymm2
    vpmovmskb %ymm2, %eax
    cmp $-1, %eax
    jne fail

    /* fork must copy the calling thread's extended register state. */
    mov $57, %rax                 /* fork */
    syscall
    test %rax, %rax
    jz .Lavx_fork_child
    js fail
    mov %eax, xstate_child_pid(%rip)
    mov $61, %rax                 /* wait4(child, &status, 0, NULL) */
    mov xstate_child_pid(%rip), %rdi
    lea wait_status(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp xstate_child_pid(%rip), %eax
    jne fail
    cmpl $0, wait_status(%rip)
    jne fail
    /* The parent must retain its own YMM state after fork/wait. */
    vmovdqu xstate_pattern(%rip), %ymm1
    vpcmpeqb %ymm0, %ymm1, %ymm2
    vpmovmskb %ymm2, %eax
    cmp $-1, %eax
    jne fail
    vzeroupper
    jmp .Lavx_probe_done
.Lavx_fork_child:
    vmovdqu xstate_pattern(%rip), %ymm1
    vpcmpeqb %ymm0, %ymm1, %ymm2
    vpmovmskb %ymm2, %eax
    cmp $-1, %eax
    jne .Lavx_fork_child_fail
    xor %edi, %edi
    jmp .Lavx_fork_child_exit
.Lavx_fork_child_fail:
    mov $1, %edi
.Lavx_fork_child_exit:
    mov $60, %rax                 /* exit(status) */
    syscall
    ud2
    vzeroupper
.Lavx_probe_done:

    /* Linux x86-64 getpid(39). */
    mov $39, %rax
    syscall
    test %rax, %rax
    jle fail
    mov %rax, pid_result(%rip)
    mov $186, %rax                           /* gettid */
    syscall
    cmp pid_result(%rip), %eax
    jne fail
    mov $110, %rax                           /* getppid */
    syscall
    test %rax, %rax
    js fail
    /* getcpu(309): current LIWUS scheduler CPU and the only NUMA node (0). */
    mov $109, %r12d
    mov $309, %rax
    lea cpu_result(%rip), %rdi
    lea node_result(%rip), %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    cmpl $16, cpu_result(%rip)
    jae fail
    cmpl $0, node_result(%rip)
    jne fail
    mov $309, %rax                 /* nullable result pointers */
    xor %rdi, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $309, %rax
    mov $0x1000, %rdi              /* invalid CPU output */
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    cmp $-14, %rax
    jne fail

    /* Linux fork + execve must keep the Linux syscall personality. */
    mov $110, %r12d
    mov $293, %rax                 /* pipe2(O_CLOEXEC | O_NONBLOCK) */
    lea exec_pipe_fds(%rip), %rdi
    mov $0x80800, %rsi
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax                  /* F_GETFD on pipe2 endpoints */
    mov exec_pipe_fds(%rip), %edi
    mov $1, %esi
    syscall
    cmp $1, %rax
    jne fail
    mov $72, %rax
    mov exec_pipe_fds+4(%rip), %edi
    mov $1, %esi
    syscall
    cmp $1, %rax
    jne fail
    mov $72, %rax                  /* F_SETFL shares O_NONBLOCK via dup */
    mov exec_pipe_fds(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $0x800, %rax               /* O_RDONLY | O_NONBLOCK */
    jne fail
    mov $72, %rax
    mov exec_pipe_fds+4(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $0x801, %rax               /* O_WRONLY | O_NONBLOCK */
    jne fail
    mov $0, %rax                    /* empty nonblocking read -> EAGAIN */
    mov exec_pipe_fds(%rip), %rdi
    lea append_readback(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $-11, %rax
    jne fail
    mov $32, %rax                   /* duplicate the read endpoint */
    mov exec_pipe_fds(%rip), %rdi
    syscall
    test %rax, %rax
    js fail
    mov %eax, exec_nonblock_dup(%rip)
    mov $72, %rax                   /* clearing O_NONBLOCK through dup */
    mov exec_nonblock_dup(%rip), %rdi
    mov $4, %rsi
    mov $0x8000, %rdx              /* musl's immutable O_LARGEFILE only */
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov exec_pipe_fds(%rip), %rdi
    mov $3, %rsi
    syscall
    test %rax, %rax                /* shared endpoint now blocks */
    jne fail
    mov $72, %rax                   /* restore nonblocking via duplicate */
    mov exec_nonblock_dup(%rip), %rdi
    mov $4, %rsi
    mov $0x8800, %rdx              /* O_LARGEFILE | O_NONBLOCK */
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov exec_nonblock_dup(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $1, %rax                    /* nonblocking write/read round trip */
    mov exec_pipe_fds+4(%rip), %rdi
    lea append_initial(%rip), %rsi
    mov $3, %rdx
    syscall
    cmp $3, %rax
    jne fail
    mov $0, %rax
    mov exec_pipe_fds(%rip), %rdi
    lea append_readback(%rip), %rsi
    mov $3, %rdx
    syscall
    cmp $3, %rax
    jne fail
    mov $4090, %rdx                /* nearly fill the pipe */
    mov $1, %rax
    mov exec_pipe_fds+4(%rip), %rdi
    lea pipe_fill(%rip), %rsi
    syscall
    cmp $4090, %rax
    jne fail
    mov $1, %rax                    /* short space: atomic write -> EAGAIN */
    mov exec_pipe_fds+4(%rip), %rdi
    lea append_initial(%rip), %rsi
    mov $10, %rdx
    syscall
    cmp $-11, %rax
    jne fail
    mov $0, %rax                    /* only the original bytes are present */
    mov exec_pipe_fds(%rip), %rdi
    lea pipe_readback(%rip), %rsi
    mov $4096, %rdx
    syscall
    cmp $4090, %rax
    jne fail
    mov $167, %r12d
    mov $5, %rax                   /* fstat(pipe read end) -> S_IFIFO */
    mov exec_pipe_fds(%rip), %rdi
    lea statbuf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov statbuf+24(%rip), %eax
    and $0xf000, %eax
    cmp $0x1000, %eax
    jne fail
    mov statbuf+8(%rip), %rax      /* one stable inode shared by both ends */
    mov %rax, pipe_inode(%rip)
    mov $5, %rax                   /* fstat(pipe write end) */
    mov exec_pipe_fds+4(%rip), %rdi
    lea statbuf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov statbuf+24(%rip), %eax
    and $0xf000, %eax
    cmp $0x1000, %eax
    jne fail
    mov statbuf+8(%rip), %rax
    cmp pipe_inode(%rip), %rax
    jne fail
    mov $168, %r12d
    mov $0, %rax                   /* read from a pipe's write end */
    mov exec_pipe_fds+4(%rip), %rdi
    lea append_readback(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $-9, %rax                  /* Linux EBADF */
    jne fail
    mov $1, %rax                   /* write to a pipe's read end */
    mov exec_pipe_fds(%rip), %rdi
    lea append_initial(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $0, %rax                   /* a free descriptor is EBADF */
    mov $31, %rdi
    lea append_readback(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $1, %rax
    mov $31, %rdi
    lea append_initial(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $171, %r12d
    mov $19, %rax                  /* readv validates its descriptor at count 0 */
    mov $31, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $20, %rax                  /* writev has the same EBADF contract */
    mov $31, %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    cmp $-9, %rax
    jne fail
    mov $72, %rax                  /* F_DUPFD_CLOEXEC */
    mov exec_pipe_fds(%rip), %rdi
    mov $1030, %rsi
    mov $5, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %eax, exec_dupfd(%rip)
    mov $72, %rax
    mov exec_dupfd(%rip), %edi
    mov $1, %esi
    syscall
    cmp $1, %rax
    jne fail
    mov $32, %rax                  /* dup clears FD_CLOEXEC */
    mov exec_pipe_fds(%rip), %rdi
    syscall
    test %rax, %rax
    js fail
    mov %eax, exec_dup_plain(%rip)
    mov $72, %rax
    mov exec_dup_plain(%rip), %edi
    mov $1, %esi
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax                  /* F_SETFD sets the descriptor flag */
    mov exec_dup_plain(%rip), %rdi
    mov $2, %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov exec_dup_plain(%rip), %edi
    mov $1, %esi
    syscall
    cmp $1, %rax
    jne fail
    mov $32, %rax                  /* a fresh duplicate starts clear */
    mov exec_dup_plain(%rip), %rdi
    syscall
    test %rax, %rax
    js fail
    mov %eax, exec_dup_clear(%rip)
    mov $72, %rax
    mov exec_dup_clear(%rip), %edi
    mov $1, %esi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov exec_dupfd(%rip), %rdi
    syscall
    mov $3, %rax
    mov exec_dup_plain(%rip), %rdi
    syscall
    mov $3, %rax
    mov exec_dup_clear(%rip), %rdi
    syscall
    mov $57, %rax
    syscall
    test %rax, %rax
    js fail
    jz linux_exec_child_call
    mov %eax, exec_child_pid(%rip)
    mov $3, %rax                  /* parent closes its pipe endpoints */
    mov exec_pipe_fds(%rip), %rdi
    syscall
    mov $3, %rax
    mov exec_pipe_fds+4(%rip), %rdi
    syscall
    mov $61, %rax                 /* wait4(child, &status, 0, NULL) */
    mov exec_child_pid(%rip), %edi
    lea exec_child_status(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp exec_child_pid(%rip), %eax
    jne fail
    cmpl $0x2a00, exec_child_status(%rip) /* child exited 42 */
    jne .Lexec_child_status_fail
    mov $309, %rax
    xor %rdi, %rdi
    mov $0x1000, %rsi              /* invalid NUMA output */
    xor %rdx, %rdx
    syscall
    cmp $-14, %rax
    jne fail

    /* Linux identity getters reflect this process's generic LIWUS creds. */
    mov $102, %rax                            /* getuid */
    syscall
    mov %eax, uid_result(%rip)
    mov $107, %rax                            /* geteuid */
    syscall
    cmp uid_result(%rip), %eax
    jne fail
    mov $104, %rax                            /* getgid */
    syscall
    mov %eax, gid_result(%rip)
    mov $108, %rax                            /* getegid */
    syscall
    cmp gid_result(%rip), %eax
    jne fail
    mov $118, %rax                            /* getresuid */
    lea uid_triplet(%rip), %rdi
    lea uid_triplet+4(%rip), %rsi
    lea uid_triplet+8(%rip), %rdx
    syscall
    test %rax, %rax
    jne fail
    mov uid_triplet(%rip), %eax
    cmp uid_result(%rip), %eax
    jne fail
    mov uid_triplet+4(%rip), %eax
    cmp uid_result(%rip), %eax
    jne fail
    mov uid_triplet+8(%rip), %eax
    cmp uid_result(%rip), %eax
    jne fail
    mov $118, %rax
    mov $0x1000, %rdi                         /* invalid optional output */
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    cmp $-14, %rax                            /* -EFAULT */
    jne fail
    mov $120, %rax                            /* getresgid */
    lea gid_triplet(%rip), %rdi
    lea gid_triplet+4(%rip), %rsi
    lea gid_triplet+8(%rip), %rdx
    syscall
    test %rax, %rax
    jne fail
    mov gid_triplet(%rip), %eax
    cmp gid_result(%rip), %eax
    jne fail
    mov gid_triplet+4(%rip), %eax
    cmp gid_result(%rip), %eax
    jne fail
    mov gid_triplet+8(%rip), %eax
    cmp gid_result(%rip), %eax
    jne fail
    mov $120, %rax
    xor %rdi, %rdi                            /* all output pointers optional */
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $115, %rax                            /* empty supplementary group set */
    xor %rdi, %rdi                            /* size=0 queries the count */
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    jne fail
    mov $115, %rax
    mov $1, %rdi
    mov $0x1000, %rsi                         /* no copy when the list is empty */
    syscall
    test %rax, %rax
    jne fail
    mov $115, %rax
    mov $-1, %rdi
    syscall
    cmp $-22, %rax                            /* -EINVAL */
    jne fail

    /* umask(95) shares only generic per-process permission state. */
    mov $95, %rax
    mov $027, %rdi
    syscall
    cmp $022, %rax                            /* process default */
    jne fail
    mov $95, %rax
    mov $022, %rdi                            /* restore the default */
    syscall
    cmp $027, %rax
    jne fail
    mov $97, %rax                             /* getrlimit(RLIMIT_NOFILE) */
    mov $7, %rdi
    lea rlimit_buf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    cmpq $32, rlimit_buf(%rip)
    jne fail
    cmpq $32, rlimit_buf+8(%rip)
    jne fail
    mov $97, %rax
    mov $7, %rdi
    mov $0x1000, %rsi
    syscall
    cmp $-14, %rax                            /* -EFAULT */
    jne fail
    mov $97, %rax
    mov $4, %rdi                              /* valid but unimplemented */
    lea rlimit_buf(%rip), %rsi
    syscall
    cmp $-38, %rax                            /* -ENOSYS */
    jne fail
    mov $97, %rax
    mov $16, %rdi                             /* outside Linux resource range */
    lea rlimit_buf(%rip), %rsi
    syscall
    cmp $-22, %rax                            /* -EINVAL */
    jne fail
    mov $302, %rax                            /* prlimit64 current-process query */
    xor %rdi, %rdi                            /* pid=0 means self */
    mov $7, %rsi
    xor %rdx, %rdx                            /* no new limit */
    lea rlimit_buf(%rip), %r10
    syscall
    test %rax, %rax
    jne fail
    mov $302, %rax
    mov $0x7fffffff, %rdi                     /* nonexistent pid */
    mov $7, %rsi
    xor %rdx, %rdx
    lea rlimit_buf(%rip), %r10
    syscall
    cmp $-3, %rax                             /* -ESRCH */
    jne fail
    mov $302, %rax
    xor %rdi, %rdi
    mov $7, %rsi
    lea rlimit_buf(%rip), %rdx                /* setting limits is unsupported */
    lea rlimit_buf(%rip), %r10
    syscall
    cmp $-38, %rax                            /* -ENOSYS */
    jne fail

    /* uname(63): Linux's 6x65-byte layout, with LIWUS kernel identity. */
    mov $63, %rax
    lea utsname_buf(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    cmpb $'L', utsname_buf(%rip)             /* sysname = Linux */
    jne fail
    cmpb $'l', utsname_buf+65(%rip)          /* nodename = liwus */
    jne fail
    cmpb $'L', utsname_buf+130(%rip)         /* release = LiwusOS-2.0 */
    jne fail
    cmpb $'L', utsname_buf+195(%rip)         /* version = LIWUS x86_64 */
    jne fail
    cmpb $'x', utsname_buf+260(%rip)         /* machine = x86_64 */
    jne fail
    cmpb $0, utsname_buf+5(%rip)
    jne fail
    cmpb $0, utsname_buf+70(%rip)
    jne fail
    cmpb $0, utsname_buf+141(%rip)
    jne fail
    cmpb $0, utsname_buf+208(%rip)
    jne fail
    cmpb $0, utsname_buf+266(%rip)
    jne fail
    mov $63, %rax
    mov $0x1000, %rdi                        /* guard page */
    syscall
    cmp $-14, %rax                            /* -EFAULT */
    jne fail

    /* prctl(157): Linux stores a per-thread comm name, max 15 bytes + NUL. */
    mov $157, %rax
    mov $15, %rdi                             /* PR_SET_NAME */
    lea prctl_name_input(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov $157, %rax
    mov $16, %rdi                             /* PR_GET_NAME */
    lea prctl_name_output(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    lea prctl_name_expected(%rip), %rsi
    lea prctl_name_output(%rip), %rdi
    mov $16, %rcx
    repe cmpsb
    jne fail
    /* fork inherits the calling thread's comm, independently of later GET. */
    mov $57, %rax
    syscall
    test %rax, %rax
    jz .Lprctl_fork_child
    js fail
    mov %eax, xstate_child_pid(%rip)
    mov $61, %rax
    mov xstate_child_pid(%rip), %rdi
    lea wait_status(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp xstate_child_pid(%rip), %eax
    jne fail
    cmpl $0, wait_status(%rip)
    jne fail
    jmp .Lprctl_fork_done
.Lprctl_fork_child:
    mov $157, %rax
    mov $16, %rdi
    lea prctl_name_output(%rip), %rsi
    syscall
    test %rax, %rax
    jne .Lprctl_fork_child_fail
    lea prctl_name_expected(%rip), %rsi
    lea prctl_name_output(%rip), %rdi
    mov $16, %rcx
    repe cmpsb
    jne .Lprctl_fork_child_fail
    xor %edi, %edi
    jmp .Lprctl_fork_child_exit
.Lprctl_fork_child_fail:
    mov $1, %edi
.Lprctl_fork_child_exit:
    mov $60, %rax
    syscall
    ud2
.Lprctl_fork_done:
    /* Parent's per-thread name is unchanged by the child's copy. */
    mov $157, %rax
    mov $16, %rdi
    lea prctl_name_output(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    lea prctl_name_expected(%rip), %rsi
    lea prctl_name_output(%rip), %rdi
    mov $16, %rcx
    repe cmpsb
    jne fail
    mov $157, %rax                            /* bad SET_NAME pointer */
    mov $15, %rdi
    movabs $0x0000800000000000, %rsi
    syscall
    cmp $-14, %rax
    jne fail
    mov $157, %rax                            /* bad GET_NAME pointer */
    mov $16, %rdi
    mov $0x1000, %rsi
    syscall
    cmp $-14, %rax
    jne fail
    mov $157, %rax                            /* unsupported prctl option */
    mov $0x7fffffff, %rdi
    xor %rsi, %rsi
    syscall
    cmp $-22, %rax
    jne fail

    /* gettimeofday(96): Linux timeval is two signed 64-bit fields. */
    mov $96, %rax
    lea timeval_buf(%rip), %rdi
    xor %rsi, %rsi                            /* timezone ignored */
    syscall
    test %rax, %rax
    jne fail
    cmpq $0, timeval_buf+8(%rip)
    jl fail
    cmpq $1000000, timeval_buf+8(%rip)
    jge fail
    mov $96, %rax
    mov $0x1000, %rdi
    syscall
    cmp $-14, %rax                            /* -EFAULT */
    jne fail
    mov $96, %rax
    xor %rdi, %rdi                            /* Linux permits null timeval */
    syscall
    test %rax, %rax
    jne fail

    /* time(201): optional x86-64 time_t output and EFAULT translation. */
    mov $201, %rax
    lea time_result(%rip), %rdi
    syscall
    test %rax, %rax
    js fail
    cmp time_result(%rip), %rax
    jne fail
    mov $201, %rax
    mov $0x1000, %rdi
    syscall
    cmp $-14, %rax                             /* -EFAULT */
    jne fail
    mov $201, %rax
    xor %rdi, %rdi                             /* optional pointer may be NULL */
    syscall
    test %rax, %rax
    js fail

    /* sysinfo(99): verify x86-64 layout and generic physical-memory stats. */
    mov $99, %rax
    lea sysinfo_buf(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    cmpq $0, sysinfo_buf+32(%rip)              /* totalram */
    jle fail
    mov sysinfo_buf+40(%rip), %rax             /* freeram <= totalram */
    cmp sysinfo_buf+32(%rip), %rax
    ja fail
    cmpl $1, sysinfo_buf+104(%rip)             /* mem_unit=bytes */
    jne fail
    mov $99, %rax
    mov $0x1000, %rdi
    syscall
    cmp $-14, %rax                             /* -EFAULT */
    jne fail

    /* clock_gettime(228): monotonic output, invalid id and pointer errors. */
    mov $228, %rax
    mov $1, %rdi                              /* CLOCK_MONOTONIC */
    lea monotonic_buf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    cmpq $0, monotonic_buf+8(%rip)
    jl fail
    cmpq $1000000000, monotonic_buf+8(%rip)
    jge fail
    mov $228, %rax
    mov $2, %rdi                              /* unsupported clock */
    lea monotonic_buf(%rip), %rsi
    syscall
    cmp $-22, %rax                            /* -EINVAL */
    jne fail
    mov $228, %rax
    xor %rdi, %rdi                            /* CLOCK_REALTIME */
    lea realtime_buf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    cmpq $0, realtime_buf+8(%rip)
    jl fail
    cmpq $1000000000, realtime_buf+8(%rip)
    jge fail
    mov $228, %rax
    xor %rdi, %rdi
    mov $0x1000, %rsi
    syscall
    cmp $-14, %rax                            /* -EFAULT */
    jne fail

    /* clock_getres(229): timer resolution and optional output pointer. */
    mov $229, %rax
    mov $1, %rdi
    lea resolution_buf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    cmpq $0, resolution_buf(%rip)
    jne fail
    cmpq $10000000, resolution_buf+8(%rip)
    jne fail
    mov $229, %rax
    mov $2, %rdi
    lea resolution_buf(%rip), %rsi
    syscall
    cmp $-22, %rax                            /* -EINVAL */
    jne fail
    mov $229, %rax
    mov $1, %rdi
    mov $0x1000, %rsi
    syscall
    cmp $-14, %rax                            /* -EFAULT */
    jne fail
    mov $229, %rax
    mov $1, %rdi
    xor %rsi, %rsi                            /* resolution pointer optional */
    syscall
    test %rax, %rax
    jne fail

    /* sched_yield(24) and nanosleep(35) use the generic LIWUS scheduler. */
    mov $24, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $35, %rax
    lea sleep_timespec(%rip), %rdi
    xor %rsi, %rsi                            /* no remainder requested */
    syscall
    test %rax, %rax
    jne fail
    mov $35, %rax
    mov $0x1000, %rdi
    syscall
    cmp $-14, %rax                            /* -EFAULT */
    jne fail
    mov $35, %rax
    lea bad_sleep_timespec(%rip), %rdi
    syscall
    cmp $-22, %rax                            /* -EINVAL */
    jne fail

    /* clock_nanosleep(230): relative and expired absolute deadlines. */
    mov $230, %rax
    mov $1, %rdi                              /* CLOCK_MONOTONIC */
    xor %rsi, %rsi                            /* relative */
    lea sleep_timespec(%rip), %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $228, %rax
    mov $1, %rdi                              /* monotonic now + 50 ms */
    lea monotonic_buf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    addq $50000000, monotonic_buf+8(%rip)
    cmpq $1000000000, monotonic_buf+8(%rip)
    jb 1f
    subq $1000000000, monotonic_buf+8(%rip)
    incq monotonic_buf(%rip)
1:
    mov $230, %rax
    mov $1, %rdi
    mov $1, %rsi                              /* TIMER_ABSTIME */
    lea monotonic_buf(%rip), %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $230, %rax
    mov $1, %rdi
    mov $1, %rsi                              /* TIMER_ABSTIME */
    lea futex_expired(%rip), %rdx             /* deadline 0 has passed */
    syscall
    test %rax, %rax
    jne fail
    mov $230, %rax
    mov $1, %rdi
    mov $1, %rsi
    mov $0x1000, %rdx
    syscall
    cmp $-14, %rax                            /* -EFAULT */
    jne fail
    mov $230, %rax
    mov $1, %rdi
    mov $2, %rsi                              /* unsupported flag */
    lea sleep_timespec(%rip), %rdx
    syscall
    cmp $-22, %rax                            /* -EINVAL */
    jne fail
    mov $230, %rax
    mov $1, %rdi
    xor %rsi, %rsi
    lea bad_sleep_timespec(%rip), %rdx
    syscall
    cmp $-22, %rax                            /* -EINVAL */
    jne fail
    mov $230, %rax
    mov $2, %rdi                              /* unsupported clock */
    xor %rsi, %rsi
    lea sleep_timespec(%rip), %rdx
    syscall
    cmp $-22, %rax                            /* -EINVAL */
    jne fail

    /* Linux fork(57) + wait4(61): WNOHANG, EFAULT preservation, status ABI. */
    mov $80, %r12d
    mov $57, %rax
    syscall
    test %rax, %rax
    jz linux_fork_child
    js fail
    mov $81, %r12d
    mov %eax, child_pid(%rip)
    mov $61, %rax
    mov child_pid(%rip), %edi
    lea wait_status(%rip), %rsi
    mov $1, %rdx                            /* WNOHANG */
    lea wait_rusage(%rip), %r10
    syscall
    test %rax, %rax
    jz wait_child_blocking
    cmp child_pid(%rip), %eax
    jne fail
    jmp wait_child_status
wait_child_blocking:
    mov $82, %r12d
    mov $61, %rax
    mov child_pid(%rip), %edi
    mov $0x1000, %rsi                       /* invalid status pointer */
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp $-14, %rax                          /* EFAULT must not reap child */
    jne fail
    mov $83, %r12d
    mov $61, %rax
    mov child_pid(%rip), %edi
    lea wait_status(%rip), %rsi
    xor %rdx, %rdx
    lea wait_rusage(%rip), %r10
    syscall
    cmp child_pid(%rip), %eax
    jne fail
wait_child_status:
    mov $84, %r12d
    cmpl $0x2500, wait_status(%rip)         /* exit(37) => WEXITSTATUS 37 */
    jne fail
    xor %ecx, %ecx
    lea wait_rusage(%rip), %r8
wait_rusage_check:
    cmpq $0, (%r8,%rcx,8)
    jne fail
    inc %ecx
    cmp $18, %ecx                           /* struct rusage is 144 bytes */
    jne wait_rusage_check
    mov $61, %rax
    mov child_pid(%rip), %edi
    xor %rsi, %rsi
    mov $1, %rdx
    xor %r10, %r10
    syscall
    cmp $-10, %rax                          /* reaped child is no longer waitable */
    jne fail
    jmp linux_fork_done
linux_fork_child:
    mov $37, %rdi
    mov $60, %rax
    syscall
    ud2
linux_fork_done:

    /* access(21)/faccessat(269) translate Linux R_OK/W_OK/X_OK to VFS rights for mounted /sys. */
    mov $21, %rax
    lea system_path(%rip), %rdi
    xor %rsi, %rsi                /* F_OK */
    syscall
    test %rax, %rax
    jne fail
    mov $151, %r12d
    mov $21, %rax
    lea missing_path(%rip), %rdi
    xor %rsi, %rsi
    syscall
    cmp $-2, %rax                 /* -ENOENT */
    jne fail
    mov $2, %rax                  /* legacy open must preserve ENOENT */
    lea missing_path(%rip), %rdi
    mov $0x8000, %rsi             /* ignored x86-64 O_LARGEFILE */
    xor %rdx, %rdx
    syscall
    cmp $-2, %rax
    jne fail

    /* mkdirat(258)/unlinkat(263), limited to the generic AT_FDCWD VFS path. */
    mov $103, %r12d
    mov $263, %rax
    mov $-100, %rdi
    lea abi_test_dir(%rip), %rsi
    mov $0x200, %r10               /* AT_REMOVEDIR: clean prior interrupted run */
    syscall
    cmp $-2, %rax
    je 1f
    test %rax, %rax
    jne fail
1:
    mov $104, %r12d
    mov $258, %rax
    mov $-100, %rdi
    lea abi_test_dir(%rip), %rsi
    mov $0700, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $87, %rax                 /* unlink of the now-missing file */
    lea legacy_open_path(%rip), %rdi
    syscall
    cmp $-2, %rax                 /* -ENOENT */
    jne fail
    mov $87, %rax                 /* invalid pathname pointer */
    mov $0x1000, %rdi
    syscall
    cmp $-14, %rax                /* -EFAULT */
    jne fail
    /* Legacy rename(82): move a file, preserve ENOENT/EEXIST semantics. */
    mov $257, %rax
    mov $-100, %rdi
    lea rename_source(%rip), %rsi
    mov $0x41, %rdx               /* O_WRONLY|O_CREAT */
    mov $0600, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea rename_blocker(%rip), %rsi
    mov $0x41, %rdx
    mov $0600, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $82, %rax
    lea rename_source(%rip), %rdi
    lea rename_source(%rip), %rsi
    syscall
    test %rax, %rax              /* same-path rename is a successful no-op */
    jne fail
    mov $82, %rax
    lea rename_source(%rip), %rdi
    lea rename_destination(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov $2, %rax
    lea rename_source(%rip), %rdi
    xor %rsi, %rsi
    syscall
    cmp $-2, %rax
    jne fail
    mov $2, %rax
    lea rename_destination(%rip), %rdi
    xor %rsi, %rsi
    syscall
    test %rax, %rax
    js fail
    mov %rax, %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $82, %rax
    lea rename_destination(%rip), %rdi
    lea rename_blocker(%rip), %rsi
    syscall
    cmp $-17, %rax               /* -EEXIST */
    jne fail
    mov $82, %rax
    lea rename_destination(%rip), %rdi
    lea missing_path(%rip), %rsi
    syscall
    cmp $-2, %rax                /* -ENOENT */
    jne fail
    mov $87, %rax
    lea rename_destination(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $87, %rax
    lea rename_blocker(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    /* renameat(264)/renameat2(316) through a real directory descriptor. */
    mov $257, %rax
    mov $-100, %rdi
    lea abi_test_dir(%rip), %rsi
    mov $0x10000, %rdx           /* O_DIRECTORY */
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, %r13d
    mov $257, %rax
    mov %r13, %rdi
    lea rename_at_source(%rip), %rsi
    mov $0x41, %rdx
    mov $0600, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov %r13, %rdi
    lea rename_at_blocker(%rip), %rsi
    mov $0x41, %rdx
    mov $0600, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $264, %rax
    mov %r13, %rdi
    lea rename_at_source(%rip), %rsi
    mov %r13, %rdx
    lea rename_at_destination(%rip), %r10
    syscall
    test %rax, %rax
    jne fail
    mov $316, %rax
    mov %r13, %rdi
    lea rename_at_destination(%rip), %rsi
    mov %r13, %rdx
    lea rename_at_source(%rip), %r10
    xor %r8, %r8
    syscall
    test %rax, %rax
    jne fail
    mov $316, %rax
    mov %r13, %rdi
    lea rename_at_source(%rip), %rsi
    mov %r13, %rdx
    lea rename_at_destination(%rip), %r10
    mov $1, %r8                 /* RENAME_NOREPLACE is unsupported */
    syscall
    cmp $-22, %rax
    jne fail
    mov $257, %rax
    mov %r13, %rdi
    lea rename_at_source(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $263, %rax
    mov %r13, %rdi
    lea rename_at_source(%rip), %rsi
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $263, %rax
    mov %r13, %rdi
    lea rename_at_blocker(%rip), %rsi
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail
    mov %r13, %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $264, %rax
    mov $1, %rdi                 /* stdout is valid but not a directory */
    lea rename_at_source(%rip), %rsi
    mov $1, %rdx
    lea rename_at_destination(%rip), %r10
    syscall
    cmp $-20, %rax               /* -ENOTDIR */
    jne fail
    mov $264, %rax
    mov $99, %rdi                /* unused descriptor */
    lea rename_at_source(%rip), %rsi
    mov $99, %rdx
    lea rename_at_destination(%rip), %r10
    syscall
    cmp $-9, %rax                /* -EBADF */
    jne fail
    mov $84, %rax                 /* cleanup after an interrupted run */
    lea legacy_dir_path(%rip), %rdi
    syscall
    cmp $-2, %rax
    je 4f
    test %rax, %rax
    jne fail
4:
    mov $83, %rax
    lea legacy_dir_path(%rip), %rdi
    mov $0700, %rsi
    syscall
    test %rax, %rax
    jne fail
    mov $83, %rax
    lea legacy_dir_path(%rip), %rdi
    mov $0700, %rsi
    syscall
    cmp $-17, %rax                /* -EEXIST */
    jne fail
    mov $84, %rax
    lea legacy_dir_path(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $84, %rax
    lea legacy_dir_path(%rip), %rdi
    syscall
    cmp $-2, %rax                 /* -ENOENT */
    jne fail
    mov $105, %r12d
    mov $258, %rax                 /* existing directory */
    mov $-100, %rdi
    lea abi_test_dir(%rip), %rsi
    mov $0700, %rdx
    syscall
    cmp $-17, %rax                 /* -EEXIST */
    jne fail
    mov $106, %r12d
    mov $263, %rax
    mov $-100, %rdi
    lea abi_test_dir(%rip), %rsi
    mov $0x200, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $107, %r12d
    mov $263, %rax                 /* missing path */
    mov $-100, %rdi
    lea abi_test_dir(%rip), %rsi
    mov $0x200, %r10
    syscall
    cmp $-2, %rax
    jne fail
    mov $108, %r12d
    mov $263, %rax                 /* unsupported unlinkat flags */
    mov $-100, %rdi
    lea abi_test_dir(%rip), %rsi
    mov $1, %r10
    syscall
    cmp $-22, %rax                 /* -EINVAL */
    jne fail
    mov $21, %rax
    lea system_path(%rip), %rdi
    mov $8, %rsi                  /* invalid access bits */
    syscall
    cmp $-22, %rax                /* -EINVAL */
    jne fail
    mov $21, %rax
    mov $0x1000, %rdi             /* invalid pathname */
    xor %rsi, %rsi
    syscall
    cmp $-14, %rax                /* -EFAULT */
    jne fail
    mov $152, %r12d
    mov $269, %rax
    mov $-100, %rdi               /* AT_FDCWD */
    lea system_path(%rip), %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $439, %rax                /* faccessat2(flags=0) */
    mov $-100, %rdi
    lea system_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $439, %rax                /* flags outside current subset */
    mov $-100, %rdi
    lea system_path(%rip), %rsi
    xor %rdx, %rdx
    mov $0x200, %r10              /* AT_EACCESS */
    syscall
    cmp $-22, %rax                /* -EINVAL */
    jne fail
    mov $153, %r12d
    mov $257, %rax
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    mov $0x10800, %rdx            /* O_DIRECTORY|O_NONBLOCK (directory no-op) */
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, dir_fd(%rip)
    mov $154, %r12d
    mov $269, %rax
    mov dir_fd(%rip), %rdi       /* dirfd-relative access("sys") */
    lea relative_sys_path(%rip), %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $439, %rax                /* faccessat2 relative directory fd */
    mov dir_fd(%rip), %rdi
    lea relative_sys_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $262, %rax                /* clear an empty dir left by an interrupted run */
    mov dir_fd(%rip), %rdi
    lea dirfd_dir_name(%rip), %rsi
    lea statbuf(%rip), %rdx
    xor %r10, %r10
    syscall
    cmp $-2, %rax
    je 3f
    test %rax, %rax
    jne fail
    mov $263, %rax
    mov dir_fd(%rip), %rdi
    lea dirfd_dir_name(%rip), %rsi
    mov $0x200, %r10
    syscall
    cmp $-2, %rax
    je 3f
    test %rax, %rax
    jne fail
3:
    mov $161, %r12d
    mov $258, %rax                /* mkdirat(rootfd, relative, 0700) */
    mov dir_fd(%rip), %rdi
    lea dirfd_dir_name(%rip), %rsi
    mov $0700, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $162, %r12d
    mov $262, %rax                /* newfstatat(rootfd, relative) */
    mov dir_fd(%rip), %rdi
    lea dirfd_dir_name(%rip), %rsi
    lea statbuf(%rip), %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail
    mov statbuf+24(%rip), %eax
    and $0xf000, %eax
    cmp $0x4000, %eax             /* S_IFDIR */
    jne fail
    mov $163, %r12d
    mov $263, %rax                /* unlinkat(rootfd, relative, AT_REMOVEDIR) */
    mov dir_fd(%rip), %rdi
    lea dirfd_dir_name(%rip), %rsi
    mov $0x200, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $155, %r12d
    mov $269, %rax                /* relative path with a closed fd */
    mov $31, %rdi
    lea relative_sys_path(%rip), %rsi
    xor %rdx, %rdx
    syscall
    cmp $-9, %rax                 /* -EBADF */
    jne fail
    mov $158, %r12d
    mov $257, %rax                /* open regular file for ENOTDIR check */
    mov $-100, %rdi
    lea regular_file_path(%rip), %rsi
    mov $0x42, %rdx              /* O_CREAT | O_RDWR */
    mov $0600, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, file_fd(%rip)
    mov $159, %r12d
    mov $269, %rax
    mov file_fd(%rip), %rdi
    lea relative_sys_path(%rip), %rsi
    xor %rdx, %rdx
    syscall
    cmp $-20, %rax                /* -ENOTDIR */
    jne fail
    mov $160, %r12d
    mov $269, %rax
    mov $-1, %rdi                 /* absolute pathname ignores dirfd */
    lea system_path(%rip), %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov file_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $164, %r12d
    mov $263, %rax
    mov $-100, %rdi
    lea regular_file_path(%rip), %rsi
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $157, %r12d
    mov $3, %rax
    mov dir_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail

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

    /* Legacy open(2) is still emitted by static musl; O_LARGEFILE is a no-op
       on x86-64 and must be accepted alongside ordinary Linux flags. */
    mov $263, %rax                /* clean up after interrupted test run */
    mov $-100, %rdi
    lea legacy_open_path(%rip), %rsi
    xor %rdx, %rdx
    syscall
    mov $2, %rax
    lea legacy_open_path(%rip), %rdi
    mov $0x8042, %rsi             /* O_LARGEFILE|O_CREAT|O_RDWR */
    mov $0600, %rdx
    syscall
    test %rax, %rax
    js fail
    mov %eax, file_fd(%rip)
    mov $5, %rax
    mov file_fd(%rip), %rdi
    lea statbuf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov statbuf+24(%rip), %eax
    and $0777, %eax
    cmp $0600, %eax
    jne fail
    mov $3, %rax
    mov file_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $87, %rax                 /* legacy unlink(path) used by libc */
    lea legacy_open_path(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail

    /* openat preserves Linux access modes, errno and descriptor CLOEXEC. */
    mov $112, %r12d
    mov $263, %rax                /* clean up after interrupted test run */
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    xor %r10, %r10
    syscall
    mov $257, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    mov $0x804c2, %rdx            /* O_CLOEXEC|O_APPEND|O_CREAT|O_EXCL|O_RDWR */
    mov $0600, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, openat_fd(%rip)
    mov $5, %rax                  /* Linux fstat exposes requested mode */
    mov openat_fd(%rip), %rdi
    lea statbuf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov statbuf+24(%rip), %eax
    and $0777, %eax
    cmp $0600, %eax
    jne fail
    mov $165, %r12d
    mov $72, %rax                  /* F_GETFL includes open-time O_APPEND */
    mov openat_fd(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $0x402, %rax               /* O_RDWR | O_APPEND */
    jne fail
    mov $166, %r12d
    mov $72, %rax                  /* regular-file O_NONBLOCK is stored/no-op */
    mov openat_fd(%rip), %rdi
    mov $4, %rsi
    mov $0xc02, %rdx               /* O_RDWR | O_APPEND | O_NONBLOCK */
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $0xc02, %rax               /* O_RDWR | O_APPEND | O_NONBLOCK */
    jne fail
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $4, %rsi
    mov $0x402, %rdx               /* clear O_NONBLOCK, retain O_APPEND */
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $0x402, %rax
    jne fail
    mov $72, %rax                  /* accept/ignore libc's O_LARGEFILE */
    mov openat_fd(%rip), %rdi
    mov $4, %rsi
    mov $0x8402, %rdx             /* O_LARGEFILE | O_RDWR | O_APPEND */
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $0x402, %rax              /* immutable O_LARGEFILE is not status */
    jne fail
    mov $1, %rax
    mov openat_fd(%rip), %rdi
    lea append_initial(%rip), %rsi
    mov $3, %rdx
    syscall
    cmp $3, %rax
    jne fail
    mov $8, %rax                   /* seek to zero; next write still appends */
    mov openat_fd(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $1, %rax
    mov openat_fd(%rip), %rdi
    lea append_fourth(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $1, %rax
    jne fail
    mov $72, %rax                  /* clear O_APPEND with F_SETFL */
    mov openat_fd(%rip), %rdi
    mov $4, %rsi
    mov $2, %rdx                   /* preserve O_RDWR */
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $2, %rax
    jne fail
    mov $170, %r12d
    mov $32, %rax                  /* dup shares the open-file description */
    mov openat_fd(%rip), %rdi
    syscall
    test %rax, %rax
    js fail
    mov %eax, append_dup_fd(%rip)
    mov $72, %rax
    mov append_dup_fd(%rip), %rdi
    mov $4, %rsi                  /* F_SETFL through the duplicate */
    mov $0x402, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $0x402, %rax               /* shared state visible through original */
    jne fail
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $4, %rsi
    mov $2, %rdx                  /* clear it again */
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov append_dup_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $8, %rax
    mov openat_fd(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $1, %rax
    mov openat_fd(%rip), %rdi
    lea append_overwrite(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $1, %rax
    jne fail
    mov $72, %rax                  /* restore append while retaining O_RDWR */
    mov openat_fd(%rip), %rdi
    mov $4, %rsi
    mov $0x402, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $8, %rax
    mov openat_fd(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $1, %rax
    mov openat_fd(%rip), %rdi
    lea append_fifth(%rip), %rsi
    mov $1, %rdx
    syscall
    cmp $1, %rax
    jne fail
    mov $8, %rax
    mov openat_fd(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $0, %rax
    mov openat_fd(%rip), %rdi
    lea append_readback(%rip), %rsi
    mov $5, %rdx
    syscall
    cmp $5, %rax
    jne fail
    mov append_readback(%rip), %eax
    cmpl $0x6463627a, %eax        /* "zbcd" */
    jne fail
    cmpb $'e', append_readback+4(%rip)
    jne fail
    mov $72, %rax                 /* positional writes use their explicit offset */
    mov openat_fd(%rip), %rdi
    mov $4, %rsi
    mov $2, %rdx                  /* clear O_APPEND */
    syscall
    test %rax, %rax
    jne fail
    mov $17, %rax                 /* pread64(fd, buf, 2, 1) */
    mov openat_fd(%rip), %rdi
    lea append_readback(%rip), %rsi
    mov $2, %rdx
    mov $1, %r10
    syscall
    cmp $2, %rax
    jne fail
    cmpw $0x6362, append_readback(%rip) /* "bc" */
    jne fail
    mov $8, %rax                   /* pread64 must preserve the shared offset */
    mov openat_fd(%rip), %rdi
    xor %rsi, %rsi
    mov $1, %rdx                   /* SEEK_CUR */
    syscall
    cmp $5, %rax
    jne fail
    mov $18, %rax                  /* pwrite64(fd, "XY", 2, 1) */
    mov openat_fd(%rip), %rdi
    lea positional_write(%rip), %rsi
    mov $2, %rdx
    mov $1, %r10
    syscall
    cmp $2, %rax
    jne fail
    mov $8, %rax
    mov openat_fd(%rip), %rdi
    xor %rsi, %rsi
    mov $1, %rdx
    syscall
    cmp $5, %rax                  /* pwrite64 also preserves the shared offset */
    jne fail
    mov $17, %rax
    mov openat_fd(%rip), %rdi
    lea append_readback(%rip), %rsi
    mov $2, %rdx
    mov $1, %r10
    syscall
    cmp $2, %rax
    jne fail
    cmpw $0x5958, append_readback(%rip) /* "XY" */
    jne fail
    mov $18, %rax                  /* pwrite past EOF materializes a zero gap */
    mov openat_fd(%rip), %rdi
    lea append_fifth(%rip), %rsi
    mov $1, %rdx
    mov $7, %r10
    syscall
    cmp $1, %rax
    jne fail
    mov $5, %rax                   /* fstat sees the extended file length */
    mov openat_fd(%rip), %rdi
    lea statbuf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    cmpq $8, statbuf+48(%rip)
    jne fail
    mov $17, %rax                  /* the two-byte hole reads back as zeros */
    mov openat_fd(%rip), %rdi
    lea append_readback(%rip), %rsi
    mov $3, %rdx
    mov $5, %r10
    syscall
    cmp $3, %rax
    jne fail
    cmpw $0, append_readback(%rip)
    jne fail
    cmpb $'e', append_readback+2(%rip)
    jne fail
    mov $17, %rax                  /* EOF returns zero without touching buf */
    mov openat_fd(%rip), %rdi
    movabs $0x0000800000000000, %rsi
    mov $1, %rdx
    mov $99, %r10
    syscall
    test %rax, %rax
    jne fail
    mov $18, %rax                  /* invalid source must not create a hole */
    mov openat_fd(%rip), %rdi
    movabs $0x0000800000000000, %rsi
    mov $1, %rdx
    mov $12, %r10
    syscall
    cmp $-14, %rax
    jne fail
    mov $5, %rax
    mov openat_fd(%rip), %rdi
    lea statbuf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    cmpq $8, statbuf+48(%rip)
    jne fail
    mov $17, %rax                  /* unreadable destination -> EFAULT */
    mov openat_fd(%rip), %rdi
    movabs $0x0000800000000000, %rsi
    mov $1, %rdx
    xor %r10, %r10
    syscall
    cmp $-14, %rax
    jne fail
    mov $17, %rax                  /* negative offsets and unreadable buffers fail */
    mov openat_fd(%rip), %rdi
    lea append_readback(%rip), %rsi
    mov $1, %rdx
    mov $-1, %r10
    syscall
    cmp $-22, %rax
    jne fail
    mov $18, %rax
    mov openat_fd(%rip), %rdi
    movabs $0x0000800000000000, %rsi
    mov $1, %rdx
    xor %r10, %r10
    syscall
    cmp $-14, %rax
    jne fail
    mov $3, %rax
    mov openat_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    mov $0xc2, %rdx               /* O_CREAT | O_EXCL | O_RDWR */
    mov $0600, %r10
    syscall
    cmp $-17, %rax                 /* -EEXIST */
    jne fail
    mov $3, %rax
    mov openat_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    mov $0x10000, %rdx            /* O_DIRECTORY */
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, openat_fd(%rip)
    mov $5, %rax
    mov openat_fd(%rip), %rdi
    lea statbuf(%rip), %rsi
    syscall
    test %rax, %rax
    jne fail
    mov statbuf+24(%rip), %eax
    and $0xf000, %eax
    cmp $0x4000, %eax             /* Linux S_IFDIR */
    jne fail
    mov $167, %r12d
    mov $332, %rax                 /* statx basic metadata layout */
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    xor %rdx, %rdx
    mov $0x7ff, %r10               /* STATX_BASIC_STATS */
    lea statxbuf(%rip), %r8
    syscall
    test %rax, %rax
    jne fail
    cmp $0x7ff, statxbuf(%rip)     /* all basic fields available */
    jne fail
    movzwl statxbuf+28(%rip), %eax
    and $0xf000, %eax
    cmp $0x4000, %eax              /* stx_mode is S_IFDIR */
    jne fail
    cmpq $1, statxbuf+140(%rip)    /* synthetic LIWUS device minor */
    jne fail
    mov $332, %rax
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    mov $0x100, %rdx               /* AT_SYMLINK_NOFOLLOW: no symlinks yet */
    xor %r10, %r10
    lea statxbuf(%rip), %r8
    syscall
    test %rax, %rax
    jne fail
    mov $332, %rax                 /* statx relative to an open directory */
    mov openat_fd(%rip), %rdi
    lea open_probe_relative(%rip), %rsi
    xor %rdx, %rdx
    mov $0x7ff, %r10
    lea statxbuf(%rip), %r8
    syscall
    test %rax, %rax
    jne fail
    movzwl statxbuf+28(%rip), %eax
    and $0xf000, %eax
    cmp $0x8000, %eax              /* Linux S_IFREG */
    jne fail
    mov $332, %rax
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    mov $0x200, %rdx               /* unsupported AT_* flag */
    xor %r10, %r10
    lea statxbuf(%rip), %r8
    syscall
    cmp $-22, %rax                 /* -EINVAL */
    jne fail
    mov $332, %rax
    mov $-100, %rdi
    mov $0x1000000, %rsi           /* invalid user pathname */
    xor %rdx, %rdx
    xor %r10, %r10
    lea statxbuf(%rip), %r8
    syscall
    cmp $-14, %rax                 /* -EFAULT */
    jne fail
    mov $332, %rax
    mov $-100, %rdi
    lea missing_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    lea statxbuf(%rip), %r8
    syscall
    cmp $-2, %rax                  /* -ENOENT */
    jne fail
    mov $332, %rax
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    mov $0x1000000, %r8            /* invalid user output */
    syscall
    cmp $-14, %rax                 /* -EFAULT */
    jne fail
    mov $257, %rax
    mov openat_fd(%rip), %rdi      /* relative open via directory descriptor */
    lea open_probe_relative(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, dir_fd(%rip)
    mov $3, %rax
    mov dir_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov openat_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $31, %rdi                  /* invalid fd with relative path */
    lea open_probe_relative(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp $-9, %rax                  /* -EBADF */
    jne fail
    mov $257, %rax
    mov $-1, %rdi                  /* absolute pathname ignores dirfd */
    lea root_path(%rip), %rsi
    mov $0x10000, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, openat_fd(%rip)
    mov $3, %rax
    mov openat_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    mov $0x10000, %rdx            /* O_DIRECTORY on regular file */
    xor %r10, %r10
    syscall
    cmp $-20, %rax                /* -ENOTDIR */
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    mov $0x10001, %rdx            /* O_DIRECTORY | O_WRONLY */
    xor %r10, %r10
    syscall
    cmp $-21, %rax                /* -EISDIR */
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    mov $0x80001, %rdx            /* O_CLOEXEC | O_WRONLY */
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, openat_fd(%rip)
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $3, %rsi                  /* F_GETFL */
    syscall
    cmp $1, %rax                  /* O_WRONLY */
    jne fail
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $1, %rsi                  /* F_GETFD */
    syscall
    cmp $1, %rax                  /* FD_CLOEXEC */
    jne fail
    mov $3, %rax
    mov openat_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    mov $2, %rdx                  /* O_RDWR */
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, openat_fd(%rip)
    mov $72, %rax
    mov openat_fd(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $2, %rax                  /* O_RDWR */
    jne fail
    mov $257, %rax
    mov openat_fd(%rip), %rdi      /* regular-file dirfd cannot resolve child */
    lea open_probe_relative(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    cmp $-20, %rax                 /* -ENOTDIR */
    jne fail
    mov $3, %rax
    mov openat_fd(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    mov $0x800, %rdx              /* O_NONBLOCK is a no-op for VFS files */
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, openat_fd(%rip)
    mov $72, %rax                 /* F_GETFL preserves open-time O_NONBLOCK */
    mov openat_fd(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $0x800, %rax              /* O_RDONLY | O_NONBLOCK */
    jne fail
    mov openat_fd(%rip), %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    mov $0x20000, %rdx           /* O_NOFOLLOW, no symlink nodes exist */
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %rax, %rdi
    mov $3, %rax
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    mov $3, %rdx                  /* invalid O_ACCMODE */
    xor %r10, %r10
    syscall
    cmp $-22, %rax
    jne fail
    mov $263, %rax
    mov $-100, %rdi
    lea open_probe_path(%rip), %rsi
    xor %r10, %r10
    syscall
    test %rax, %rax
    jne fail

    /* fchdir(81) uses a directory open description and generic process CWD. */
    mov $93, %r12d
    mov $80, %rax
    lea relative_sys_path(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $257, %rax                 /* openat(AT_FDCWD, "/", O_RDONLY) */
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, cwd_dir_fd(%rip)
    mov $94, %r12d
    mov $81, %rax
    mov cwd_dir_fd(%rip), %edi
    syscall
    test %rax, %rax
    jne fail
    mov $95, %r12d
    mov $79, %rax
    lea cwd_buf(%rip), %rdi
    mov $16, %rsi
    syscall
    cmp $2, %rax
    jne fail
    mov $96, %r12d
    mov $80, %rax
    lea root_path(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $97, %r12d
    mov $3, %rax
    mov cwd_dir_fd(%rip), %edi
    syscall
    test %rax, %rax
    jne fail
    mov $98, %r12d
    mov $81, %rax
    mov cwd_dir_fd(%rip), %edi     /* closed descriptor */
    syscall
    cmp $-9, %rax                  /* -EBADF */
    jne fail
    mov $99, %r12d
    mov $81, %rax
    mov $1, %edi                   /* stdout is a live non-directory fd */
    syscall
    cmp $-20, %rax                 /* -ENOTDIR */
    je 2f
    mov %eax, %r12d                /* preserve raw error for failure diagnosis */
    jmp fail
2:

    /* pipe(22), then exercise writev(20) and readv(19) over VFS pipe I/O. */
    mov $22, %rax
    mov $0x1000, %rdi                /* invalid pipefd pointer */
    syscall
    cmp $-14, %rax
    jne fail
    mov $22, %rax
    lea pipe_fds(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $32, %rax                    /* dup rejects a free descriptor */
    mov $31, %rdi
    syscall
    cmp $-9, %rax                    /* -EBADF */
    jne fail
    mov $32, %rax                    /* duplicate the write end */
    mov pipe_fds+4(%rip), %edi
    syscall
    test %rax, %rax
    js fail
    mov %eax, dup_fd(%rip)
    mov $33, %rax                    /* dup2 can map to a chosen descriptor */
    mov pipe_fds(%rip), %rdi
    mov $20, %rsi
    syscall
    cmp $20, %rax
    jne fail
    mov $33, %rax                    /* dup2(oldfd, oldfd) is a no-op */
    mov $20, %rdi
    mov $20, %rsi
    syscall
    cmp $20, %rax
    jne fail
    mov $292, %rax                   /* dup3 with no flags */
    mov pipe_fds+4(%rip), %rdi
    mov $21, %rsi
    xor %rdx, %rdx
    syscall
    cmp $21, %rax
    jne fail
    mov $33, %rax                    /* dup2 closes and replaces an occupied fd */
    mov pipe_fds(%rip), %rdi
    mov $21, %rsi
    syscall
    cmp $21, %rax
    jne fail
    mov $100, %r12d
    mov $292, %rax                   /* dup3(O_CLOEXEC) marks new fd only */
    mov pipe_fds+4(%rip), %rdi
    mov $22, %rsi
    mov $0x80000, %rdx
    syscall
    cmp $22, %rax
    jne fail
    mov $72, %rax
    mov $22, %rdi
    mov $1, %rsi
    syscall
    cmp $1, %rax
    jne fail
    mov $33, %rax                    /* invalid source maps to -EBADF */
    mov $31, %rdi
    mov $23, %rsi
    syscall
    cmp $-9, %rax
    jne fail
    mov $72, %rax                    /* fcntl(F_GETFD): no FD_CLOEXEC set */
    mov pipe_fds(%rip), %rdi
    mov $1, %rsi
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax                    /* F_SETFD accepts the no-flags state */
    mov pipe_fds(%rip), %rdi
    mov $2, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $101, %r12d
    mov $72, %rax                    /* F_SETFD sets descriptor-local CLOEXEC */
    mov pipe_fds(%rip), %rdi
    mov $2, %rsi
    mov $1, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov pipe_fds(%rip), %rdi
    mov $1, %rsi
    syscall
    cmp $1, %rax
    jne fail
    mov $72, %rax                    /* F_GETFL: read and write pipe ends */
    mov pipe_fds(%rip), %rdi
    mov $3, %rsi
    syscall
    test %rax, %rax                  /* O_RDONLY */
    jne fail
    mov $72, %rax
    mov pipe_fds+4(%rip), %rdi
    mov $3, %rsi
    syscall
    cmp $1, %rax                     /* O_WRONLY */
    jne fail
    mov $72, %rax                    /* F_DUPFD chooses lowest fd >= 24 */
    mov pipe_fds+4(%rip), %rdi
    xor %rsi, %rsi                   /* F_DUPFD */
    mov $24, %rdx
    syscall
    cmp $24, %rax
    jl fail
    cmp $32, %rax
    jge fail
    mov %eax, fcntl_dup_fd(%rip)
    mov $72, %rax                    /* min beyond LIWUS FD table */
    mov pipe_fds(%rip), %rdi
    xor %rsi, %rsi
    mov $32, %rdx
    syscall
    cmp $-24, %rax                   /* -EMFILE */
    jne fail
    mov $72, %rax                    /* negative min */
    mov pipe_fds(%rip), %rdi
    xor %rsi, %rsi
    mov $-1, %rdx
    syscall
    cmp $-22, %rax                   /* -EINVAL */
    jne fail
    mov $102, %r12d
    mov $72, %rax                    /* F_DUPFD_CLOEXEC sets the new fd flag */
    mov pipe_fds(%rip), %rdi
    mov $1030, %rsi
    mov $5, %rdx
    syscall
    test %rax, %rax
    js fail
    cmp $32, %rax
    jge fail
    mov %eax, cloexec_dup_fd(%rip)
    mov $72, %rax
    mov cloexec_dup_fd(%rip), %rdi
    mov $1, %rsi
    syscall
    cmp $1, %rax
    jne fail
    mov $20, %rax
    mov dup_fd(%rip), %edi
    mov $0x1000, %rsi                /* invalid iovec array */
    mov $2, %rdx
    syscall
    cmp $-14, %rax                   /* -EFAULT; pipe remains empty */
    jne fail
    mov $20, %rax
    mov dup_fd(%rip), %edi
    lea writev_iov(%rip), %rsi
    mov $2, %rdx
    syscall
    cmp $writev_total, %rax
    jne fail
    mov $19, %rax
    mov $21, %edi                    /* use dup2'd read descriptor */
    mov $0x1000, %rsi                /* invalid iovec array */
    mov $2, %rdx
    syscall
    cmp $-14, %rax                   /* -EFAULT */
    jne fail
    lea readv_buf0(%rip), %rax
    mov %rax, readv_iov(%rip)
    movq $4, readv_iov+8(%rip)
    lea readv_buf1(%rip), %rax
    mov %rax, readv_iov+16(%rip)
    movq $4, readv_iov+24(%rip)
    mov $19, %rax
    mov $21, %edi
    lea readv_iov(%rip), %rsi
    mov $1025, %rdx                 /* IOV_MAX bound; no bytes consumed */
    syscall
    cmp $-22, %rax
    jne fail
    mov $3, %rax                    /* dup2'd descriptor retains the pipe */
    mov pipe_fds(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $19, %rax
    mov $21, %edi
    lea readv_iov(%rip), %rsi
    mov $2, %rdx
    syscall
    cmp $8, %rax                    /* both vectors consumed */
    jne fail
    mov $3, %rax
    mov $21, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov $20, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov pipe_fds+4(%rip), %edi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov dup_fd(%rip), %edi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov fcntl_dup_fd(%rip), %edi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov $22, %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax
    mov cloexec_dup_fd(%rip), %edi
    syscall
    test %rax, %rax
    jne fail
    mov $3, %rax

    /* Linux getdents64(217): variable-length, aligned dirent64 records. */
    mov $70, %r12d
    mov $257, %rax
    mov $-100, %rdi               /* AT_FDCWD */
    lea root_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, dir_fd(%rip)
    mov $217, %rax
    mov dir_fd(%rip), %rdi
    lea dirent_buf(%rip), %rsi
    mov $4096, %rdx
    syscall
    mov $71, %r12d
    test %rax, %rax
    jle fail
    mov %rax, %r9                  /* remaining byte count */
    lea dirent_buf(%rip), %r8
    xor %r11d, %r11d               /* number of records */
dirent_check_loop:
    mov $72, %r12d
    test %r9, %r9
    je dirent_check_done
    cmp $19, %r9
    jb fail
    movzwq 16(%r8), %rcx           /* d_reclen */
    cmp $24, %rcx
    jb fail
    cmp %r9, %rcx
    ja fail
    test $7, %cl                   /* records are 8-byte aligned */
    jnz fail
    cmpb $0, 19(%r8)               /* d_name must be nonempty */
    je fail
    mov %rcx, %rdx
    sub $19, %rdx
    lea 19(%r8), %r10
dirent_name_check:
    cmpb $0, (%r10)
    je dirent_name_ok
    inc %r10
    dec %rdx
    jnz dirent_name_check
    jmp fail
dirent_name_ok:
    inc %r11d
    add %rcx, %r8
    sub %rcx, %r9
    jmp dirent_check_loop
dirent_check_done:
    mov $73, %r12d
    test %r11d, %r11d
    jz fail
    mov $217, %rax
    mov dir_fd(%rip), %rdi
    lea dirent_buf(%rip), %rsi
    mov $4096, %rdx
    syscall
    mov $74, %r12d
    test %rax, %rax                /* directory offset reaches EOF */
    jne fail
    mov $8, %rax                   /* SEEK_SET consumes VFS cookie 0 */
    mov dir_fd(%rip), %rdi
    xor %rsi, %rsi
    xor %rdx, %rdx
    syscall
    test %rax, %rax
    jne fail
    mov $217, %rax
    mov dir_fd(%rip), %rdi
    lea dirent_buf(%rip), %rsi
    mov $4096, %rdx
    syscall
    test %rax, %rax                /* rewind makes records available again */
    jle fail
    mov $-1, %rdi                  /* invalid descriptor */
    mov $217, %rax
    lea dirent_buf(%rip), %rsi
    mov $4096, %rdx
    syscall
    mov $75, %r12d
    cmp $-9, %rax                  /* -EBADF */
    jne fail
    mov $78, %r12d
    mov $217, %rax
    mov $1, %rdi                   /* stdout is a valid non-directory fd */
    lea dirent_buf(%rip), %rsi
    mov $4096, %rdx
    syscall
    cmp $-20, %rax                 /* -ENOTDIR */
    jne fail

    /* EFAULT must not consume a directory entry either. */
    mov $257, %rax
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    test %rax, %rax
    js fail
    mov %eax, dir_fd(%rip)
    mov $217, %rax
    mov dir_fd(%rip), %rdi
    mov $0x1000, %rsi
    mov $4096, %rdx
    syscall
    cmp $-14, %rax                 /* -EFAULT */
    jne fail
    mov $217, %rax
    mov dir_fd(%rip), %rdi
    lea dirent_buf(%rip), %rsi
    mov $4096, %rdx
    syscall
    test %rax, %rax
    jle fail

    /* A too-small buffer must fail without consuming the first entry. */
    mov $257, %rax
    mov $-100, %rdi
    lea root_path(%rip), %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    syscall
    mov $76, %r12d
    test %rax, %rax
    js fail
    mov %eax, dir_fd(%rip)
    mov $217, %rax
    mov dir_fd(%rip), %rdi
    lea dirent_buf(%rip), %rsi
    mov $1, %rdx
    syscall
    mov $77, %r12d
    cmp $-22, %rax                 /* -EINVAL */
    jne fail
    mov $217, %rax
    mov dir_fd(%rip), %rdi
    lea dirent_buf(%rip), %rsi
    mov $4096, %rdx
    syscall
    test %rax, %rax
    jle fail

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
    mov $90, %r12d
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

    /* A Linux guard mapping starts PROT_NONE then enables the usable stack. */
    mov $10, %rax                 /* mprotect */
    mov %rbx, %rdi
    mov $4096, %rsi
    xor %rdx, %rdx                /* PROT_NONE */
    syscall
    test %rax, %rax
    jne fail
    mov $10, %rax
    mov %rbx, %rdi
    mov $4096, %rsi
    mov $3, %rdx                  /* PROT_READ | PROT_WRITE */
    syscall
    test %rax, %rax
    jne fail
    movb $0x5a, (%rbx)

    mov $10, %rax
    lea 1(%rbx), %rdi             /* address alignment is mandatory */
    mov $4096, %rsi
    mov $3, %rdx
    syscall
    cmp $-22, %rax                 /* -EINVAL */
    jne fail
    mov $10, %rax
    mov %rbx, %rdi
    xor %rsi, %rsi                 /* length must be nonzero */
    xor %rdx, %rdx
    syscall
    cmp $-22, %rax
    jne fail
    mov $10, %rax
    mov $0x1000, %rdi              /* unmapped guard page */
    mov $4096, %rsi
    mov $3, %rdx
    syscall
    cmp $-12, %rax                 /* -ENOMEM */
    jne fail

    /* Linux getcwd(79): byte count includes NUL; short and bad buffers fail. */
    mov $91, %r12d
    mov $79, %rax
    lea cwd_buf(%rip), %rdi
    mov $16, %rsi
    syscall
    test %rax, %rax
    jle fail
    cmpb $'/', cwd_buf(%rip)
    jne fail
    mov %rax, %rcx
    dec %rcx
    lea cwd_buf(%rip), %rdx
    cmpb $0, (%rdx,%rcx)
    jne fail
    mov $79, %rax
    lea cwd_buf(%rip), %rdi
    mov $1, %rsi
    syscall
    cmp $-34, %rax                 /* -ERANGE */
    jne fail
    mov $79, %rax
    mov $0x1000, %rdi
    mov $16, %rsi
    syscall
    cmp $-14, %rax                 /* -EFAULT */
    jne fail
    mov $79, %rax
    lea cwd_buf(%rip), %rdi
    xor %rsi, %rsi
    syscall
    cmp $-22, %rax                 /* -EINVAL */
    jne fail

    /* chdir(80) shares process CWD with getcwd and native filesystem calls. */
    mov $92, %r12d
    mov $80, %rax
    lea relative_sys_path(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $79, %rax
    lea cwd_buf(%rip), %rdi
    mov $16, %rsi
    syscall
    cmp $5, %rax                   /* "/sys\\0" */
    jne fail
    cmpb $'s', cwd_buf+1(%rip)
    jne fail
    cmpb $'y', cwd_buf+2(%rip)
    jne fail
    cmpb $'s', cwd_buf+3(%rip)
    jne fail
    cmpb $0, cwd_buf+4(%rip)
    jne fail
    mov $80, %rax
    lea missing_path(%rip), %rdi
    syscall
    cmp $-2, %rax                   /* -ENOENT; CWD must remain unchanged */
    jne fail
    mov $79, %rax
    lea cwd_buf(%rip), %rdi
    mov $16, %rsi
    syscall
    cmp $5, %rax
    jne fail
    mov $80, %rax
    lea root_path(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $80, %rax
    mov $0x1000, %rdi              /* bad pathname pointer */
    syscall
    cmp $-14, %rax                 /* -EFAULT */
    jne fail

    /* Private FUTEX_REQUEUE and CMP_REQUEUE validate keys/counts and errors. */
    mov $202, %rax
    lea futex_source(%rip), %rdi
    mov $0x83, %rsi                /* FUTEX_REQUEUE | PRIVATE */
    mov $1, %rdx                   /* wake one */
    mov $1, %r10                   /* requeue one */
    lea futex_destination(%rip), %r8
    syscall
    test %rax, %rax                /* no waiters were present */
    jne fail
    mov $202, %rax
    lea futex_source(%rip), %rdi
    mov $0x84, %rsi                /* FUTEX_CMP_REQUEUE | PRIVATE */
    xor %rdx, %rdx
    mov $1, %r10
    lea futex_destination(%rip), %r8
    mov $1, %r9                    /* expected source word is zero */
    syscall
    cmp $-11, %rax                 /* -EAGAIN */
    jne fail
    mov $202, %rax
    lea futex_source(%rip), %rdi
    mov $0x83, %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    mov $0x1000, %r8               /* invalid destination */
    syscall
    cmp $-14, %rax                 /* -EFAULT */
    jne fail

    /* WAIT_BITSET uses an absolute deadline and nonzero waiter masks. */
    mov $202, %rax
    lea futex_source(%rip), %rdi
    mov $0x89, %rsi                /* WAIT_BITSET | PRIVATE */
    xor %rdx, %rdx
    lea futex_expired(%rip), %r10  /* absolute deadline already expired */
    mov $1, %r9                    /* val3 bitset is in R9 */
    syscall
    cmp $-110, %rax                /* -ETIMEDOUT */
    jne fail
    mov $202, %rax
    lea futex_source(%rip), %rdi
    mov $0x189, %rsi               /* CLOCK_REALTIME | WAIT_BITSET | PRIVATE */
    xor %rdx, %rdx
    lea futex_expired(%rip), %r10
    mov $1, %r9
    syscall
    cmp $-110, %rax
    jne fail
    mov $202, %rax
    lea futex_source(%rip), %rdi
    mov $0x89, %rsi
    xor %rdx, %rdx
    xor %r10, %r10
    xor %r9, %r9                   /* zero bitset is invalid */
    syscall
    cmp $-22, %rax
    jne fail
    mov $202, %rax
    lea futex_source(%rip), %rdi
    mov $0x8a, %rsi                /* WAKE_BITSET | PRIVATE */
    mov $1, %rdx
    xor %r10, %r10
    xor %r9, %r9                   /* zero bitset is invalid */
    syscall
    cmp $-22, %rax
    jne fail
    mov $202, %rax
    lea futex_source(%rip), %rdi
    mov $0x8a, %rsi
    mov $1, %rdx
    xor %r10, %r10
    mov $1, %r9
    syscall
    test %rax, %rax                /* no matching waiters */
    jne fail

    /* Linux munmap(11). */
    mov $11, %rax
    mov %rbx, %rdi
    mov $4096, %rsi
    syscall
    test %rax, %rax
    jne fail

    /* Linux write(1). */
    mov $20, %rax                   /* writev */
    mov $1, %rdi
    mov $0x1000, %rsi               /* invalid iovec array */
    mov $1, %rdx
    syscall
    cmp $-14, %rax                   /* -EFAULT */
    jne fail
    mov $20, %rax                   /* writev */
    mov $1, %rdi
    lea writev_iov(%rip), %rsi
    mov $2, %rdx
    syscall
    cmp $writev_total, %rax
    jne fail

    /* close_range closes descriptors and supports marking them CLOEXEC. */
    mov $22, %rax
    lea pipe_fds(%rip), %rdi
    syscall
    test %rax, %rax
    jne fail
    mov $436, %rax
    mov pipe_fds+4(%rip), %edi
    mov %edi, %esi
    mov $4, %edx                  /* CLOSE_RANGE_CLOEXEC */
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov pipe_fds+4(%rip), %edi
    mov $1, %esi                  /* F_GETFD */
    syscall
    cmp $1, %rax
    jne fail
    mov $3, %rax                  /* leave an already-closed hole in range */
    mov pipe_fds(%rip), %edi
    syscall
    test %rax, %rax
    jne fail
    mov $436, %rax
    mov pipe_fds(%rip), %edi
    mov pipe_fds+4(%rip), %esi
    xor %edx, %edx                /* close every descriptor in range */
    syscall
    test %rax, %rax
    jne fail
    mov $72, %rax
    mov pipe_fds(%rip), %edi
    mov $1, %esi
    syscall
    cmp $-9, %rax
    jne fail
    mov $72, %rax
    mov pipe_fds+4(%rip), %edi
    mov $1, %esi
    syscall
    cmp $-9, %rax
    jne fail
    mov $436, %rax                 /* first > last */
    mov $5, %edi
    mov $4, %esi
    xor %edx, %edx
    syscall
    cmp $-22, %rax
    jne fail
    mov $436, %rax                 /* unsupported flags */
    xor %edi, %edi
    mov $2, %esi
    mov $8, %edx
    syscall
    cmp $-22, %rax
    jne fail

    xor %rdi, %rdi
    jmp exit
.Lexec_child_status_fail:
    mov exec_child_status(%rip), %r12d
    jmp fail
fail:
    mov %r12d, %edi               /* Linux exit truncates status to 8 bits. */
exit:
    mov $60, %rax
    syscall
    ud2

linux_exec_child_call:
    /* Failed exec must leave both the current image and CLOEXEC FDs intact. */
    mov $59, %rax
    lea exec_missing_path(%rip), %rdi
    lea exec_probe_argv(%rip), %rsi
    lea exec_probe_envp(%rip), %rdx
    syscall
    cmp $-2, %rax
    jne linux_exec_child_fail
    mov $72, %rax
    mov $3, %rdi
    mov $1, %rsi
    syscall
    cmp $1, %rax
    jne linux_exec_child_fail
    mov $72, %rax
    mov $4, %rdi
    mov $1, %rsi
    syscall
    cmp $1, %rax
    jne linux_exec_child_fail
    mov $59, %rax                 /* execve("linux_syscall_probe", argv, envp) */
    lea exec_probe_path(%rip), %rdi
    lea exec_probe_argv(%rip), %rsi
    lea exec_probe_envp(%rip), %rdx
    syscall
    mov $60, %rax                 /* execve returned only on failure */
    mov $1, %rdi
    syscall
    ud2

linux_exec_child:
    cmpq $2, (%rsp)
    jne linux_exec_child_fail
    mov 16(%rsp), %rax            /* argv[1] */
    test %rax, %rax
    jz linux_exec_child_fail
    cmpb $'e', (%rax)
    jne linux_exec_child_fail
    /* Linux brk(0) must agree byte-for-byte with AT_BRK. */
    mov $12, %rax
    xor %rdi, %rdi
    syscall
    cmp aux_brk_value(%rip), %rax
    jne linux_exec_child_fail
    /* A request below start_brk fails by returning the current break. */
    mov aux_brk_value(%rip), %rdi
    dec %rdi
    mov $12, %rax
    syscall
    cmp aux_brk_value(%rip), %rax
    jne linux_exec_child_fail
    /* Grow across a page boundary; new brk pages must be zero-filled. */
    mov aux_brk_value(%rip), %r10
    add $4095, %r10
    and $-4096, %r10
    lea 32(%r10), %rdi
    mov $12, %rax
    syscall
    cmp %rdi, %rax
    jne linux_exec_child_fail
    cmpq $0, (%r10)
    jne linux_exec_child_fail
    movabs $0x123456789abcdef0, %rax
    mov %rax, (%r10)
    cmp %rax, (%r10)
    jne linux_exec_child_fail
    /* Shrink back to the exact initial byte break and query it again. */
    mov $12, %rax
    mov aux_brk_value(%rip), %rdi
    syscall
    cmp aux_brk_value(%rip), %rax
    jne linux_exec_child_fail
    mov $12, %rax
    xor %rdi, %rdi
    syscall
    cmp aux_brk_value(%rip), %rax
    jne linux_exec_child_fail
    /* The user ceiling is rejected without changing the current break. */
    mov $12, %rax
    movabs $0x0000800000000000, %rdi
    syscall
    cmp aux_brk_value(%rip), %rax
    jne linux_exec_child_fail
    mov $72, %rax                  /* CLOEXEC ends disappeared across execve */
    mov $3, %rdi
    mov $1, %rsi
    syscall
    cmp $-9, %rax
    jne linux_exec_child_fail
    mov $72, %rax
    mov $4, %rdi
    mov $1, %rsi
    syscall
    cmp $-9, %rax
    jne linux_exec_child_fail
    mov $60, %rax
    mov $42, %rdi
    syscall
    ud2
linux_exec_child_fail:
    mov $60, %rax
    mov $43, %rdi
    syscall
    ud2

message:
    .ascii "Linux SYSCALL ABI: getrandom/close_range/ids/resuid-resgid-groups/umask/rlimit/prlimit/time/sysinfo/sleep/yield/clocks/clock_nanosleep/access/faccessat/faccessat2/open/openat/unlink/mkdir/rmdir/rename/renameat/renameat2/unlinkat/pread64/pwrite64/dirfd-mkdir-stat/statx/fork/execve/wait4/getcwd/chdir/fchdir/getcpu/getpid/uname/fstat-pipe/pipe/dup/dup2/dup3/fcntl/readv/writev/getdents64/mmap/mprotect/futex-bitset/requeue/munmap/FS-TLS/write-errno PASS\n"
message_end:

missing_path:
    .asciz "/liwusos-abi-probe-missing"
open_probe_path:
    .asciz "/abi-linux-open-probe"
open_probe_relative:
    .asciz "abi-linux-open-probe"
legacy_open_path:
    .asciz "/abi-linux-open-legacy-probe"
legacy_dir_path:
    .asciz "/abi-linux-legacy-dir-probe"
rename_source:
    .asciz "/abi-linux-rename-source"
rename_destination:
    .asciz "/abi-linux-rename-destination"
rename_blocker:
    .asciz "/abi-linux-rename-blocker"
rename_at_source:
    .asciz "renameat-source"
rename_at_destination:
    .asciz "renameat-destination"
rename_at_blocker:
    .asciz "renameat-blocker"
system_path:
    .asciz "/sys"
regular_file_path:
    .asciz "/abi-linux-regular-probe"
root_path:
    .asciz "/"
exec_probe_path:
    .asciz "linux_syscall_probe"
exec_missing_path:
    .asciz "/linux-exec-missing"
exec_probe_arg0:
    .asciz "linux_syscall_probe"
exec_probe_arg1:
    .asciz "exec-marker"
exec_probe_argv:
    .quad exec_probe_arg0, exec_probe_arg1, 0
exec_probe_envp:
    .quad 0
external_linux_path:
    .asciz "/linux_glibc_hello"
external_linux_arg0:
    .asciz "/linux_glibc_hello"
external_linux_argv:
    .quad external_linux_arg0, 0
external_linux_envp:
    .quad 0
abi_test_dir:
    .asciz "/abi-linux-dir-probe"
relative_sys_path:
    .asciz "sys"
dirfd_dir_name:
    .asciz "abi-linux-dirfd-probe"
append_initial:
    .ascii "abc"
append_fourth:
    .ascii "d"
append_overwrite:
    .ascii "z"
append_fifth:
    .ascii "e"
positional_write:
    .ascii "XY"
writev_iov:
    .quad writev_part1
    .quad writev_part1_end-writev_part1
    .quad writev_part2
    .quad writev_part2_end-writev_part2
writev_total = (writev_part1_end-writev_part1) + (writev_part2_end-writev_part2)
writev_part1:
    .ascii "Linux ABI writev: "
writev_part1_end:
writev_part2:
    .ascii "two iovecs PASS\n"
writev_part2_end:

.section .bss
.align 16
random_result:
    .skip 16
statbuf:
    .skip 144
statxbuf:
    .skip 256
append_readback:
    .skip 8
utsname_buf:
    .skip 390
timeval_buf:
    .skip 16
time_result:
    .skip 8
sysinfo_buf:
    .skip 112
cwd_buf:
    .skip 16
.align 4
cwd_dir_fd:
    .skip 4
file_fd:
    .skip 4
cpu_result:
    .skip 4
node_result:
    .skip 4
exec_child_pid:
    .skip 4
exec_child_status:
    .skip 4
pipe_inode:
    .skip 8
append_dup_fd:
    .skip 4
exec_pipe_fds:
    .skip 8
exec_nonblock_dup:
    .skip 4
exec_dupfd:
    .skip 4
exec_dup_plain:
    .skip 4
exec_dup_clear:
    .skip 4
.align 4
futex_source:
    .skip 4
futex_destination:
    .skip 4
monotonic_buf:
    .skip 16
realtime_buf:
    .skip 16
resolution_buf:
    .skip 16
dirent_buf:
    .skip 4096
pipe_fill:
    .skip 4096
pipe_readback:
    .skip 4096
fs_result:
    .skip 8
dir_fd:
    .skip 4
pipe_fds:
    .skip 8
dup_fd:
    .skip 4
fcntl_dup_fd:
    .skip 4
cloexec_dup_fd:
    .skip 4
openat_fd:
    .skip 4
readv_iov:
    .skip 32
readv_buf0:
    .skip 4
readv_buf1:
    .skip 4
child_pid:
    .skip 4
aux_brk_value:
    .skip 8
wait_status:
    .skip 4
wait_rusage:
    .skip 144
uid_result:
    .skip 4
gid_result:
    .skip 4
pid_result:
    .skip 4
uid_triplet:
    .skip 12
gid_triplet:
    .skip 12
rlimit_buf:
    .skip 16

.section .data
.align 8
sleep_timespec:
    .quad 0
    .quad 1000000                            /* 1 ms, rounds to one timer tick */
xstate_pattern:
    .quad 0x0123456789abcdef
    .quad 0xfedcba9876543210
    .quad 0x55aa55aaa55aa55a
    .quad 0xdeadbeefcafebabe
xstate_child_pid:
    .long 0
.align 8
prctl_name_input:
    .asciz "liwus-probe-name"
prctl_name_expected:
    .ascii "liwus-probe-nam"
    .byte 0
prctl_name_output:
    .skip 16
bad_sleep_timespec:
    .quad 0
    .quad 1000000000                         /* invalid tv_nsec */
futex_expired:
    .quad 0
    .quad 0
tls_word:
    .quad 0x123456789abcdef0
