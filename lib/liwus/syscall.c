#include "liwus/syscall.h"
#include "uapi/syscall_nums.h"

long liw_syscall0(long num) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num) : "rcx", "r11", "memory");
    return ret;
}

long liw_syscall1(long num, long a1) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "D"(a1) : "rcx", "r11", "memory");
    return ret;
}

long liw_syscall2(long num, long a1, long a2) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "D"(a1), "S"(a2) : "rcx", "r11", "memory");
    return ret;
}

long liw_syscall3(long num, long a1, long a2, long a3) {
    long ret;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "D"(a1), "S"(a2), "d"(a3) : "rcx", "r11", "memory");
    return ret;
}

long liw_syscall4(long num, long a1, long a2, long a3, long a4) {
    long ret;
    register long r10 __asm__("r10") = a4;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10) : "rcx", "r11", "memory");
    return ret;
}

long liw_syscall5(long num, long a1, long a2, long a3, long a4, long a5) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8) : "rcx", "r11", "memory");
    return ret;
}

long liw_syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6) {
    long ret;
    register long r10 __asm__("r10") = a4;
    register long r8 __asm__("r8") = a5;
    register long r9 __asm__("r9") = a6;
    __asm__ volatile("int $0x80" : "=a"(ret) : "0"(num), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
    return ret;
}

long liw_write(int fd, const void *buf, size_t count) {
    return liw_syscall3(SYS_WRITE, fd, (long)buf, (long)count);
}

long liw_read(int fd, void *buf, size_t count) {
    return liw_syscall3(SYS_READ, fd, (long)buf, (long)count);
}

int liw_open(const char *path, int flags) {
    return (int)liw_syscall2(SYS_OPEN, (long)path, (long)flags);
}

int liw_close(int fd) {
    return (int)liw_syscall1(SYS_CLOSE, (long)fd);
}

void liw_exit(int status) {
    liw_syscall1(SYS_EXIT, (long)status);
    for (;;) __asm__ volatile("pause");
}

int liw_getpid(void) {
    return (int)liw_syscall0(SYS_GETPID);
}

int liw_fork(void) {
    return (int)liw_syscall0(SYS_FORK);
}

int liw_execve(const char *path, char *const argv[], char *const envp[]) {
    return (int)liw_syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
}

int liw_waitpid(int pid, int *status, int options) {
    return (int)liw_syscall3(SYS_WAITPID, (long)pid, (long)status, (long)options);
}

void *liw_brk(void *addr) {
    return (void *)liw_syscall1(SYS_BRK, (long)addr);
}

void *liw_mmap(void *addr, size_t len, int prot, int flags, int fd, long off) {
    return (void *)liw_syscall6(SYS_MMAP, (long)addr, (long)len, (long)prot, (long)flags, (long)fd, (long)off);
}

int liw_munmap(void *addr, size_t len) {
    return (int)liw_syscall2(SYS_MUNMAP, (long)addr, (long)len);
}
