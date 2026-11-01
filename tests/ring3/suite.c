typedef unsigned long u64;

#define SYS_EXIT     1
#define SYS_BRK      2
#define SYS_READ     3
#define SYS_WRITE    4
#define SYS_OPEN     5
#define SYS_CLOSE    6
#define SYS_WAITPID   7
#define SYS_TIMER    8
#define SYS_FORK    14
#define SYS_EXECVE  15
#define SYS_GETPID   20
#define SYS_GETTIMEOFDAY 21
#define SYS_LSEEK   19
#define SYS_UNLINK  24
#define SYS_MMAP     35
#define SYS_MUNMAP   36
#define SYS_STAT     22
#define SYS_FSTAT    23
#define SYS_GETCWD   27
#define SYS_PIPE     31
#define EPIPE        32

#define PROT_READ  1
#define PROT_WRITE 2
#define MAP_PRIVATE   0x02
#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10
#define MAP_FAILED ((u64)-1)

static volatile u64 isolation_value = 0x1122334455667788UL;
static const char readonly_probe[] = "ring3 readonly probe";

static long syscall6(long number, long a1, long a2, long a3,
                     long a4, long a5, long a6) {
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "0"(number), "D"(a1), "S"(a2), "d"(a3),
                       "c"(a4), "r"(r8), "r"(r9)
                     : "memory");
    return result;
}

static long sc0(long n) { return syscall6(n, 0, 0, 0, 0, 0, 0); }
static long sc1(long n, long a) { return syscall6(n, a, 0, 0, 0, 0, 0); }
static long sc2(long n, long a, long b) { return syscall6(n, a, b, 0, 0, 0, 0); }
static long sc3(long n, long a, long b, long c) { return syscall6(n, a, b, c, 0, 0, 0); }

static void report(const char *text) {
    u64 len = 0;
    while (text[len]) len++;
    sc3(SYS_WRITE, 1, (long)text, (long)len);
}

static void fail(const char *text) {
    report("[RING3 FAIL] ");
    report(text);
    report("\n");
    sc1(SYS_EXIT, 1);
    for (;;) __asm__ volatile("hlt");
}

static void pass(const char *text) {
    report("[RING3 PASS] ");
    report(text);
    report("\n");
}

static int wait_for(int pid, int expected_status) {
    int status = -1;
    long result = sc3(SYS_WAITPID, pid, (long)&status, 0);
    return result == pid && status == expected_status;
}

static void fd_to_text(int fd, char *out) {
    char reversed[12];
    int n = 0;
    do {
        reversed[n++] = (char)('0' + fd % 10);
        fd /= 10;
    } while (fd && n < (int)sizeof(reversed));
    int i = 0;
    while (n) out[i++] = reversed[--n];
    out[i] = '\0';
}

void _start(void) {
    long pid = sc0(SYS_GETPID);
    if (pid <= 0) fail("getpid syscall");
    if (sc0(SYS_TIMER) < 0) fail("timer syscall");
    pass("int 0x80 syscalls and serial write");

    u64 old_brk = (u64)sc1(SYS_BRK, 0);
    if (!old_brk || sc1(SYS_BRK, (long)(old_brk + 4096)) < (long)(old_brk + 4096))
        fail("brk allocation");
    volatile u64 *heap_word = (volatile u64 *)old_brk;
    *heap_word = 0xA55AA55A;
    if (*heap_word != 0xA55AA55A) fail("brk mapped memory");
    pass("brk allocates writable user memory");

    volatile u64 *mapped = (volatile u64 *)syscall6(
        SYS_MMAP, 0, 4096, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((u64)mapped == MAP_FAILED) fail("mmap allocation");
    *mapped = 0xCAFEBABE;
    if (*mapped != 0xCAFEBABE || sc2(SYS_MUNMAP, (long)mapped, 4096) != 0)
        fail("mmap write or munmap");
    pass("mmap, user write, and munmap");

    long child = sc0(SYS_FORK);
    if (child < 0) fail("fork for memory isolation");
    if (child == 0) {
        isolation_value = 0xDEADBEEF;
        sc1(SYS_EXIT, 23);
        for (;;) __asm__ volatile("hlt");
    }
    if (!wait_for((int)child, 23)) fail("waitpid normal child status");
    if (isolation_value != 0x1122334455667788UL)
        fail("fork address-space isolation");
    pass("fork deep copy, child exit, and waitpid");

    /* Regression reproducer: waitpid must reject a read-only status pointer.
     * Before the usercopy fix, copy_to_user accepted the leaf PTE and wrote
     * the child status into this ELF read-only string. */
    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for read-only syscall destination repro");
    if (child == 0) {
        sc1(SYS_EXIT, 0x12345678);
        for (;;) __asm__ volatile("hlt");
    }
    volatile unsigned char *ro_bytes = (volatile unsigned char *)readonly_probe;
    unsigned char ro_before[4];
    for (int i = 0; i < 4; i++) ro_before[i] = ro_bytes[i];
    long ro_wait = sc3(SYS_WAITPID, child, (long)readonly_probe, 0);
    int ro_unchanged = 1;
    for (int i = 0; i < 4; i++)
        if (ro_bytes[i] != ro_before[i]) ro_unchanged = 0;
    if (ro_wait != -14 || !ro_unchanged)
        fail("waitpid accepted or modified read-only syscall destination");
    if (!wait_for((int)child, 0x12345678))
        fail("waitpid did not preserve child after rejected destination");
    pass("read-only waitpid destination is rejected without modification");

    volatile unsigned char *stat_page = (volatile unsigned char *)syscall6(
        SYS_MMAP, 0, 4096, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if ((u64)stat_page == MAP_FAILED) fail("mmap for syscall output tests");
    if (sc2(SYS_STAT, (long)"/", (long)stat_page) != 0)
        fail("stat into writable user memory");
    if (sc2(SYS_FSTAT, 0, (long)stat_page) != 0)
        fail("fstat into writable user memory");
    if (sc1(SYS_GETTIMEOFDAY, (long)stat_page) != 0)
        fail("gettimeofday into writable user memory");
    if (sc2(SYS_GETCWD, (long)stat_page, 64) != 0)
        fail("getcwd into writable user memory");
    if (sc2(SYS_STAT, (long)"/", (long)readonly_probe) != -14 ||
        sc2(SYS_FSTAT, 0, (long)readonly_probe) != -14 ||
        sc1(SYS_GETTIMEOFDAY, (long)readonly_probe) != -14 ||
        sc2(SYS_GETCWD, (long)readonly_probe, sizeof(readonly_probe)) != -14 ||
        sc1(SYS_PIPE, (long)readonly_probe) != -14)
        fail("stat-family syscall accepted read-only output");
    for (int i = 0; i < 4; i++)
        if (ro_bytes[i] != ro_before[i]) fail("syscall modified read-only ELF bytes");
    pass("stat, fstat, gettimeofday, and getcwd enforce writable outputs");

    /* First page writable, second page read-only: an output spanning both
     * must be rejected before changing even the writable prefix. */
    const u64 mixed_base = 0x80000000UL;
    void *mixed_rw = (void *)syscall6(SYS_MMAP, mixed_base, 4096,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    void *mixed_ro = (void *)syscall6(SYS_MMAP, mixed_base + 4096, 4096,
        PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if ((u64)mixed_rw == MAP_FAILED || (u64)mixed_ro == MAP_FAILED)
        fail("fixed mappings for mixed-permission output test");
    volatile unsigned char *mixed_tail = (volatile unsigned char *)mixed_base + 4096 - 16;
    for (int i = 0; i < 16; i++) mixed_tail[i] = (unsigned char)(0xA0 + i);
    if (sc2(SYS_STAT, (long)"/", (long)mixed_tail) != -14)
        fail("cross-page syscall output was accepted");
    for (int i = 0; i < 16; i++)
        if (mixed_tail[i] != (unsigned char)(0xA0 + i))
            fail("cross-page rejected output partially modified writable page");
    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for read-only mmap hardware protection");
    if (child == 0) {
        *(volatile unsigned char *)mixed_ro = 'X';
        sc1(SYS_EXIT, 77);
        for (;;) __asm__ volatile("hlt");
    }
    if (!wait_for((int)child, 142))
        fail("read-only mmap page allowed user write under CR0.WP");
    if (sc2(SYS_MUNMAP, mixed_base, 4096) != 0 ||
        sc2(SYS_MUNMAP, mixed_base + 4096, 4096) != 0)
        fail("munmap mixed-permission test pages");
    pass("mixed-permission range is atomic and read-only mmap protection holds");

    int pipe_fds[2] = { -1, -1 };
    if (sc1(SYS_PIPE, (long)pipe_fds) != 0) fail("pipe for protected read test");
    const char pipe_byte = 'Q';
    if (sc3(SYS_WRITE, pipe_fds[1], (long)&pipe_byte, 1) != 1)
        fail("write byte to protected read test pipe");
    if (sc3(SYS_READ, pipe_fds[0], (long)readonly_probe, 1) != -14)
        fail("pipe read accepted read-only destination");
    if (sc3(SYS_READ, pipe_fds[0], (long)stat_page, 1) != 1 || stat_page[0] != 'Q')
        fail("rejected pipe read consumed data or writable read failed");
    if (sc1(SYS_CLOSE, pipe_fds[0]) != 0 || sc1(SYS_CLOSE, pipe_fds[1]) != 0)
        fail("close protected read test pipe");

    long file_fd = sc2(SYS_OPEN, (long)"/usercopy-regression.tmp", 0x0600);
    if (file_fd < 0) fail("create file for protected read test");
    const char file_byte = 'F';
    if (sc3(SYS_WRITE, file_fd, (long)&file_byte, 1) != 1 ||
        sc3(SYS_LSEEK, file_fd, 0, 0) != 0)
        fail("prepare file for protected read test");
    if (sc3(SYS_READ, file_fd, (long)readonly_probe, 1) != -14)
        fail("file read accepted read-only destination");
    if (sc3(SYS_READ, file_fd, (long)stat_page, 1) != 1 || stat_page[0] != 'F')
        fail("rejected file read consumed data or writable read failed");
    if (sc1(SYS_CLOSE, file_fd) != 0 ||
        sc1(SYS_UNLINK, (long)"/usercopy-regression.tmp") != 0)
        fail("cleanup protected file read test");
    if (sc2(SYS_MUNMAP, (long)stat_page, 4096) != 0)
        fail("munmap syscall output page");
    pass("pipe and file reads reject protected output without consuming data");

    int lifecycle_create = (int)sc2(SYS_OPEN, (long)"/fd-lifecycle.tmp", 0x0600);
    if (lifecycle_create < 0 || sc1(SYS_CLOSE, lifecycle_create) != 0)
        fail("create descriptor lifecycle file");
    for (int round = 0; round < 24; round++) {
        int transient = (int)sc2(SYS_OPEN, (long)"/fd-lifecycle.tmp", 0);
        if (transient < 0 || sc1(SYS_CLOSE, transient) != 0)
            fail("repeated file open/close");
    }
    int shared_fd = (int)sc2(SYS_OPEN, (long)"/fd-lifecycle.tmp", 0x0600);
    if (shared_fd < 0 || sc3(SYS_WRITE, shared_fd, (long)"AB", 2) != 2 ||
        sc3(SYS_LSEEK, shared_fd, 0, 0) != 0)
        fail("prepare shared file description test");
    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for shared file description");
    if (child == 0) {
        char first = 0;
        long n = sc3(SYS_READ, shared_fd, (long)&first, 1);
        sc1(SYS_EXIT, n == 1 && first == 'A' ? 56 : 57);
        for (;;) __asm__ volatile("hlt");
    }
    char second = 0;
    if (!wait_for((int)child, 56) ||
        sc3(SYS_READ, shared_fd, (long)&second, 1) != 1 || second != 'B')
        fail("fork did not share the open file offset");
    if (sc1(SYS_CLOSE, shared_fd) != 0 ||
        sc1(SYS_UNLINK, (long)"/fd-lifecycle.tmp") != 0)
        fail("cleanup shared file description");
    pass("repeated open/close and shared file offset across fork");

    int exec_pipe[2] = { -1, -1 };
    if (sc1(SYS_PIPE, (long)exec_pipe) != 0) fail("pipe for exec descriptor inheritance");
    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for exec descriptor inheritance");
    if (child == 0) {
        char fd_arg[12];
        fd_to_text(exec_pipe[1], fd_arg);
        char *pipe_argv[] = { "c_example", "pipe", fd_arg, 0 };
        if (sc1(SYS_CLOSE, exec_pipe[0]) != 0) sc1(SYS_EXIT, 76);
        sc3(SYS_EXECVE, (long)"c_example", (long)pipe_argv, 0);
        sc1(SYS_EXIT, 77);
        for (;;) __asm__ volatile("hlt");
    }
    if (sc1(SYS_CLOSE, exec_pipe[1]) != 0 || !wait_for((int)child, 74))
        fail("execve did not preserve the inherited pipe descriptor");
    char received = 0;
    if (sc3(SYS_READ, exec_pipe[0], (long)&received, 1) != 1 || received != 'P' ||
        sc3(SYS_READ, exec_pipe[0], (long)&received, 1) != 0 ||
        sc1(SYS_CLOSE, exec_pipe[0]) != 0)
        fail("pipe data or EOF after the final writer closed");
    pass("pipe survives fork/exec and returns EOF after final writer closes");

    int broken_pipe[2] = { -1, -1 };
    if (sc1(SYS_PIPE, (long)broken_pipe) != 0) fail("pipe without readers");
    int barrier_pipe[2] = { -1, -1 };
    if (sc1(SYS_PIPE, (long)barrier_pipe) != 0) fail("pipe EPIPE synchronization");
    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for pipe without readers");
    if (child == 0) {
        char byte = 'X';
        if (sc1(SYS_CLOSE, broken_pipe[0]) != 0) sc1(SYS_EXIT, 78);
        if (sc1(SYS_CLOSE, barrier_pipe[1]) != 0) sc1(SYS_EXIT, 81);
        char gate = 0;
        if (sc3(SYS_READ, barrier_pipe[0], (long)&gate, 1) != 1 || gate != 'G')
            sc1(SYS_EXIT, 82);
        sc1(SYS_CLOSE, barrier_pipe[0]);
        long result = sc3(SYS_WRITE, broken_pipe[1], (long)&byte, 1);
        sc1(SYS_EXIT, result == -EPIPE ? 79 : 80);
        for (;;) __asm__ volatile("hlt");
    }
    char gate = 'G';
    if (sc1(SYS_CLOSE, broken_pipe[0]) != 0 ||
        sc1(SYS_CLOSE, barrier_pipe[0]) != 0 ||
        sc3(SYS_WRITE, barrier_pipe[1], (long)&gate, 1) != 1 ||
        sc1(SYS_CLOSE, barrier_pipe[1]) != 0 ||
        !wait_for((int)child, 79) ||
        sc1(SYS_CLOSE, broken_pipe[1]) != 0)
        fail("pipe write without readers did not return -EPIPE");
    if (sc3(SYS_READ, 31, (long)&received, 1) != -1 ||
        sc3(SYS_WRITE, 31, (long)&received, 1) != -1 ||
        sc1(SYS_CLOSE, 31) != -1)
        fail("invalid file descriptors were accepted");
    pass("pipe write without readers returns -EPIPE; invalid descriptors fail");

    static char *exec_argv[] = { "exec_target", "alpha", "beta", 0 };
    static char *exec_envp[] = { "MODE=ring3", "COUNT=2", 0 };
    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for execve");
    if (child == 0) {
        sc3(SYS_EXECVE, (long)"exec_target", (long)exec_argv,
            (long)exec_envp);
        sc1(SYS_EXIT, 61);
        for (;;) __asm__ volatile("hlt");
    }
    if (!wait_for((int)child, 37)) fail("fork execve waitpid or argv/envp");
    pass("fork followed by ELF64 execve, argc, argv, envp, and waitpid");

    static char *c_argv[] = { "c_example", "alpha", "beta", 0 };
    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for standalone C program");
    if (child == 0) {
        sc3(SYS_EXECVE, (long)"c_example", (long)c_argv, 0);
        sc1(SYS_EXIT, 72);
        for (;;) __asm__ volatile("hlt");
    }
    if (!wait_for((int)child, 73)) fail("standalone C program startup or exit");
    pass("independent freestanding C ELF runs through execve and waitpid");

    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for invalid ELF execve");
    if (child == 0) {
        long result = sc3(SYS_EXECVE, (long)"invalid_elf", 0, 0);
        if (result != -8 || sc0(SYS_GETPID) <= 0) sc1(SYS_EXIT, 62);
        sc1(SYS_EXIT, 41);
        for (;;) __asm__ volatile("hlt");
    }
    if (!wait_for((int)child, 41)) fail("invalid ELF damaged original process");
    pass("invalid ELF returns error and original image keeps running");

    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for invalid user pointers");
    if (child == 0) {
        long a = sc3(SYS_EXECVE, 1, 0, 0);
        long b = sc3(SYS_EXECVE, (long)"exec_target", 1, 0);
        long c = sc3(SYS_EXECVE, (long)"exec_target", 0, 1);
        if (a != -14 || b != -14 || c != -14) sc1(SYS_EXIT, 63);
        sc1(SYS_EXIT, 42);
        for (;;) __asm__ volatile("hlt");
    }
    if (!wait_for((int)child, 42)) fail("invalid execve user pointers");
    pass("invalid filename, argv, and envp pointers are rejected safely");

    for (int round = 0; round < 12; round++) {
        child = sc0(SYS_FORK);
        if (child < 0) fail("fork for repeated execve");
        if (child == 0) {
            static char *repeat_argv[] = { "exec_target", "alpha", "beta", 0 };
            static char *repeat_envp[] = { "MODE=ring3", "COUNT=2", 0 };
            sc3(SYS_EXECVE, (long)"exec_target", (long)repeat_argv,
                (long)repeat_envp);
            sc1(SYS_EXIT, 64);
            for (;;) __asm__ volatile("hlt");
        }
        if (!wait_for((int)child, 37)) fail("repeated execve did not complete");
    }
    pass("12 repeated execve image replacements complete");

    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for read-only protection");
    if (child == 0) {
        *(volatile char *)readonly_probe = 'X';
        sc1(SYS_EXIT, 24);
        for (;;) __asm__ volatile("hlt");
    }
    if (!wait_for((int)child, 142)) fail("read-only write did not kill child");
    if (sc0(SYS_GETPID) != pid) fail("parent did not survive read-only fault");
    pass("write to read-only ELF segment kills only child");

    child = sc0(SYS_FORK);
    if (child < 0) fail("fork for supervisor protection");
    if (child == 0) {
        volatile u64 value = *(volatile u64 *)0x01000000UL;
        (void)value;
        sc1(SYS_EXIT, 25);
        for (;;) __asm__ volatile("hlt");
    }
    if (!wait_for((int)child, 142)) fail("supervisor access did not kill child");
    if (sc0(SYS_GETPID) != pid) fail("parent did not survive supervisor fault");
    pass("supervisor page access kills only child");

    report("[RING3 RESULT] ALL PASS\n");
    sc1(SYS_EXIT, 0);
    for (;;) __asm__ volatile("hlt");
}
