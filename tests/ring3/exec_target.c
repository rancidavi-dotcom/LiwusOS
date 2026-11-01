typedef unsigned long u64;

#define SYS_EXIT  1
#define SYS_WRITE 4
#define SYS_GETPID 20

static long syscall3(long n, long a, long b, long c) {
    long result;
    __asm__ volatile("int $0x80" : "=a"(result)
                     : "0"(n), "D"(a), "S"(b), "d"(c) : "memory");
    return result;
}

static int same(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void write_text(const char *s) {
    u64 len = 0;
    while (s[len]) len++;
    syscall3(SYS_WRITE, 1, (long)s, (long)len);
}

long exec_target_check(const u64 *initial_stack) {
    u64 argc = initial_stack[0];
    char *const *argv = (char *const *)&initial_stack[1];
    char *const *envp = (char *const *)&argv[argc + 1];
    if (argc != 3 || !same(argv[0], "exec_target") ||
        !same(argv[1], "alpha") || !same(argv[2], "beta") ||
        !same(envp[0], "MODE=ring3") || !same(envp[1], "COUNT=2") ||
        envp[2] != 0 || syscall3(SYS_GETPID, 0, 0, 0) <= 0) {
        write_text("[EXECVE FAIL] argc/argv/envp or PID\n");
        return 90;
    }
    write_text("[EXECVE PASS] argc, argv, envp, and preserved PID\n");
    return 37;
}
