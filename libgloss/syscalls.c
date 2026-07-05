#define __SSP_FORTIFY_LEVEL 0

#include <_ansi.h>
#include <_syslist.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <stddef.h>
#include <stdint.h>
#include <reent.h>

static inline long __liw_syscall(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory", "rcx", "r11");
    return ret;
}

void _exit(int status) {
    __liw_syscall(1, status, 0, 0);
    while (1);
}

int _close(int fd) {
    return __liw_syscall(6, fd, 0, 0);
}

int _execve(const char *name, char *const argv[], char *const envp[]) {
    return __liw_syscall(15, (long)name, (long)argv, (long)envp);
}

int _fork(void) {
    errno = ENOSYS;
    return -1;
}

int _fstat(int fd, struct stat *st) {
    if (fd < 3) {
        st->st_mode = S_IFCHR;
        st->st_blksize = 0;
        return 0;
    }
    errno = EBADF;
    return -1;
}

int _getpid(void) {
    return 1;
}

int _isatty(int fd) {
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

int _kill(int pid, int sig) {
    (void)pid; (void)sig;
    errno = ENOSYS;
    return -1;
}

int _link(const char *old, const char *new) {
    (void)old; (void)new;
    errno = ENOSYS;
    return -1;
}

off_t _lseek(int fd, off_t offset, int whence) {
    return __liw_syscall(19, fd, offset, whence);
}

int _open(const char *file, int flags, ...) {
    return __liw_syscall(5, (long)file, flags, 0);
}

ssize_t _read(int fd, void *buf, size_t nbytes) {
    return __liw_syscall(3, fd, (long)buf, nbytes);
}

void *_sbrk(ptrdiff_t incr) {
    static char *heap_end = (char *)0x40000000;
    char *prev = heap_end;
    long ret = __liw_syscall(2, (long)(heap_end + incr), 0, 0);
    if (ret == -1) return (void *)-1;
    heap_end += incr;
    return prev;
}

int _stat(const char *file, struct stat *st) {
    (void)file; (void)st;
    errno = ENOSYS;
    return -1;
}

int _unlink(const char *name) {
    (void)name;
    errno = ENOSYS;
    return -1;
}

int _wait(int *status) {
    (void)status;
    errno = ENOSYS;
    return -1;
}

ssize_t _write(int fd, const void *buf, size_t nbytes) {
    return __liw_syscall(4, fd, (long)buf, nbytes);
}

int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tv; (void)tz;
    errno = ENOSYS;
    return -1;
}

clock_t _times(struct tms *buf) {
    (void)buf;
    errno = ENOSYS;
    return -1;
}

int _readlink(const char *__restrict path, char *__restrict buf, size_t bufsize) {
    (void)path; (void)buf; (void)bufsize;
    errno = ENOSYS;
    return -1;
}

int _symlink(const char *path1, const char *path2) {
    (void)path1; (void)path2;
    errno = ENOSYS;
    return -1;
}

/* Stubs for TCC */
long sysconf(int name) {
    (void)name;
    return 4096; /* PAGE_SIZE */
}

int mprotect(void *addr, size_t len, int prot) {
    (void)addr; (void)len; (void)prot;
    return 0; /* Success, all memory is executable anyway */
}

char *getcwd(char *buf, size_t size) {
    if (buf && size > 1) {
        buf[0] = '/';
        buf[1] = '\0';
        return buf;
    }
    return NULL;
}

void *dlopen(const char *filename, int flag) {
    (void)filename; (void)flag;
    return NULL;
}

char *dlerror(void) {
    return "Dynamic loading not supported";
}

void *dlsym(void *handle, const char *symbol) {
    (void)handle; (void)symbol;
    return NULL;
}

int dlclose(void *handle) {
    (void)handle;
    return -1;
}

int execvp(const char *file, char *const argv[]) {
    (void)file; (void)argv;
    return -1;
}

char *realpath(const char *path, char *resolved_path) {
    if (resolved_path) {
        char *p = resolved_path;
        while (*path) *p++ = *path++;
        *p = '\0';
        return resolved_path;
    }
    return NULL;
}

/* Wrappers sem underscore para chamadas diretas dos aplicativos. */
int close(int fd) { return _close(fd); }
int fstat(int fd, struct stat *st) { return _fstat(fd, st); }
int getpid(void) { return _getpid(); }
int isatty(int fd) { return _isatty(fd); }
int kill(int pid, int sig) { return _kill(pid, sig); }
int link(const char *old, const char *new) { return _link(old, new); }
off_t lseek(int fd, off_t offset, int whence) { return _lseek(fd, offset, whence); }
int open(const char *file, int flags, ...) { return _open(file, flags); }
ssize_t read(int fd, void *buf, size_t nbytes) { return _read(fd, buf, nbytes); }
void *sbrk(ptrdiff_t incr) { return _sbrk(incr); }
int stat(const char *file, struct stat *st) { return _stat(file, st); }
int unlink(const char *name) { return _unlink(name); }
int wait(int *status) { return _wait(status); }
ssize_t write(int fd, const void *buf, size_t nbytes) { return _write(fd, buf, nbytes); }
int gettimeofday(struct timeval *tv, void *tz) { return _gettimeofday(tv, tz); }
clock_t times(struct tms *buf) { return _times(buf); }

/* Stubs LFS. Newlib usa 64-bit offsets no target x86_64. */
long long __sseek64(struct _reent *ptr, void *cookie, long long pos, int whence) {
    (void)ptr;
    return (long long)_lseek((int)(intptr_t)cookie, (off_t)pos, whence);
}
int __swrite64(struct _reent *ptr, void *cookie, const char *buf, int len) {
    (void)ptr;
    return _write((int)(intptr_t)cookie, buf, len);
}

/* Syscalls LiwusOS adicionais */
int tcgetattr(int fd, void *term) {
    return __liw_syscall(16, fd, (long)term, 0);
}
int tcsetattr(int fd, int action, const void *term) {
    return __liw_syscall(17, fd, action, (long)term);
}
int ioctl(int fd, int request, void *argp) {
    return __liw_syscall(18, fd, request, (long)argp);
}
int fork(void) {
    return __liw_syscall(14, 0, 0, 0);
}
int execve(const char *name, char *const argv[], char *const envp[]) {
    return __liw_syscall(15, (long)name, (long)argv, (long)envp);
}
int waitpid(int pid, int *status, int options) {
    return __liw_syscall(7, pid, (long)status, options);
}
unsigned int sleep(unsigned int seconds) {
    unsigned long start = __liw_syscall(8, 0, 0, 0);
    while (__liw_syscall(8, 0, 0, 0) - start < seconds * 100);
    return 0;
}
