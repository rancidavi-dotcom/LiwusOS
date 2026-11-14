#include "liwus/unistd.h"
#include "liwus/syscall.h"
#include "liwus/fcntl.h"
#include "liwus/errno.h"
#include "liwus/stat.h"
#include "liwus/mman.h"

ssize_t read(int fd, void *buf, size_t count) {
    long r = liw_read(fd, buf, count);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (ssize_t)r;
}

ssize_t write(int fd, const void *buf, size_t count) {
    long r = liw_write(fd, buf, count);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (ssize_t)r;
}

int open(const char *pathname, int flags, ...) {
    int r = liw_open(pathname, flags);
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return r;
}

int close(int fd) {
    int r = liw_close(fd);
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return 0;
}

off_t lseek(int fd, off_t offset, int whence) {
    long r = liw_lseek(fd, (int32_t)offset, whence);
    if (r < 0) {
        errno = (int)-r;
        return (off_t)-1;
    }
    return (off_t)r;
}

int unlink(const char *pathname) {
    long r = liw_unlink(pathname);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int remove(const char *pathname) {
    return unlink(pathname);
}

int getpid(void) {
    return liw_getpid();
}

char *getcwd(char *buf, size_t size) {
    long r = liw_getcwd(buf, size);
    if (r < 0) {
        errno = (int)-r;
        return NULL;
    }
    return buf;
}

int chdir(const char *path) {
    long r = liw_chdir(path);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int isatty(int fd) {
    if (fd >= 0 && fd <= 2) return 1;
    return 0;
}

int access(const char *pathname, int mode) {
    (void)mode;
    struct liw_stat st;
    long r = liw_stat(pathname, &st);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int dup(int oldfd) {
    long r = liw_dup(oldfd);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}

int dup2(int oldfd, int newfd) {
    long r = liw_dup2(oldfd, newfd);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}

int execvp(const char *file, char *const argv[]) {
    (void)file;
    (void)argv;
    errno = ENOSYS;
    return -1;
}

int stat(const char *pathname, struct liw_stat *buf) {
    long r = liw_stat(pathname, buf);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int fstat(int fd, struct liw_stat *buf) {
    long r = liw_fstat(fd, buf);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    void *r = liw_mmap(addr, length, prot, flags, fd, offset);
    if (r == LIW_MAP_FAILED) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return r;
}

int munmap(void *addr, size_t length) {
    int r = liw_munmap(addr, length);
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return 0;
}

int mprotect(void *addr, size_t len, int prot) {
    (void)addr;
    (void)len;
    (void)prot;
    return 0;
}

int tcgetattr(int fd, void *termios_p) {
    long r = liw_syscall2(16, (long)fd, (long)termios_p);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int tcsetattr(int fd, int optional_actions, const void *termios_p) {
    long r = liw_syscall3(17, (long)fd, (long)optional_actions, (long)termios_p);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int ioctl(int fd, unsigned long request, ...) {
    __builtin_va_list ap;
    __builtin_va_start(ap, request);
    void *argp = __builtin_va_arg(ap, void *);
    __builtin_va_end(ap);
    long r = liw_syscall3(18, (long)fd, (long)request, (long)argp);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return (int)r;
}

int rename(const char *oldpath, const char *newpath) {
    long r = liw_rename(oldpath, newpath);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int mkdir(const char *pathname, uint32_t mode) {
    long r = liw_mkdir(pathname, mode);
    if (r < 0) {
        errno = (int)-r;
        return -1;
    }
    return 0;
}

int system(const char *command) {
    (void)command;
    errno = ENOSYS;
    return -1;
}
