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
#include <stdarg.h>
#include <reent.h>

/* __errno function (expected by newlib's errno.h) - weak to avoid conflicts */
__attribute__((weak)) int *__errno(void) {
    static int __errno_val = 0;
    return &__errno_val;
}

/* Forward declaration */
static inline long __liw_syscall6(long num, long a1, long a2, long a3,
                                   long a4, long a5, long a6);

/* syscall wrapper (glibc style) */
long syscall(long num, ...) {
    va_list ap;
    va_start(ap, num);
    long a1 = va_arg(ap, long);
    long a2 = va_arg(ap, long);
    long a3 = va_arg(ap, long);
    long a4 = va_arg(ap, long);
    long a5 = va_arg(ap, long);
    long a6 = va_arg(ap, long);
    va_end(ap);
    return __liw_syscall6(num, a1, a2, a3, a4, a5, a6);
}

/* Dummy libc init array (prevents NULL call from static linking) */
void __libc_init_array(void) {}

/* Trampoline: if linker chooses __libc_start_main as entry point,
   jump to _start which expects argc/argv on stack per ABI */
__attribute__((naked)) void __libc_start_main(void) {
    asm("jmp _start");
}

/* Simple memcpy/memset for malloc implementation */
static void *memcpy(void *dst, const void *src, size_t n) {
    char *d = (char *)dst;
    const char *s = (const char *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void *memset(void *s, int c, size_t n) {
    char *p = (char *)s;
    while (n--) *p++ = (char)c;
    return s;
}

static inline long __liw_syscall(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile ("int $0x80"
        : "=a"(ret)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory", "rcx", "r11");
    return ret;
}

/* Variante com 6 argumentos (usada por mmap). Passa:
   rdi=a1, rsi=a2, rdx=a3, rcx=a4, r8=a5, r9=a6.
   O kernel (via PUSH_ALL/POP_ALL) preserva todos os registradores, então
   não é preciso declarar clobber além de "memory". */
static inline long __liw_syscall6(long num, long a1, long a2, long a3,
                                  long a4, long a5, long a6) {
    register long out __asm__("rax");
    register long a5r __asm__("r8") = a5;
    register long a6r __asm__("r9") = a6;
    __asm__ volatile ("int $0x80"
        : "=a"(out)
        : "a"(num), "D"(a1), "S"(a2), "d"(a3), "c"(a4),
          "r"(a5r), "r"(a6r)
        : "memory");
    return out;
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
    return __liw_syscall(14, 0, 0, 0);
}

#undef st_atime
#undef st_mtime
#undef st_ctime

struct kernel_stat {
  uint64_t st_dev;
  uint64_t st_ino;
  uint32_t st_mode;
  uint64_t st_nlink;
  uint32_t st_uid;
  uint32_t st_gid;
  uint64_t st_rdev;
  uint64_t st_size;
  int64_t  st_atime;
  int64_t  st_atimensec;
  int64_t  st_mtime;
  int64_t  st_mtimensec;
  int64_t  st_ctime;
  int64_t  st_ctimensec;
  int64_t  st_blksize;
  int64_t  st_blocks;
  int64_t  st_spare4[2];
};

int _fstat(int fd, struct stat *st) {
    struct kernel_stat kst;
    long ret = __liw_syscall(23, fd, (long)&kst, 0);
    if (ret == 0) {
        st->st_dev = kst.st_dev;
        st->st_ino = kst.st_ino;
        st->st_mode = kst.st_mode;
        st->st_nlink = kst.st_nlink;
        st->st_uid = kst.st_uid;
        st->st_gid = kst.st_gid;
        st->st_rdev = kst.st_rdev;
        st->st_size = kst.st_size;
        st->st_atim.tv_sec = kst.st_atime;
        st->st_atim.tv_nsec = kst.st_atimensec;
        st->st_mtim.tv_sec = kst.st_mtime;
        st->st_mtim.tv_nsec = kst.st_mtimensec;
        st->st_ctim.tv_sec = kst.st_ctime;
        st->st_ctim.tv_nsec = kst.st_ctimensec;
        st->st_blksize = kst.st_blksize;
        st->st_blocks = kst.st_blocks;
    }
    return ret;
}

int _getpid(void) {
    return __liw_syscall(20, 0, 0, 0);
}

int _isatty(int fd) {
    return (fd >= 0 && fd <= 2) ? 1 : 0;
}

int _kill(int pid, int sig) {
    return __liw_syscall(28, pid, sig, 0);
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

/* Simple malloc/free implementation using _sbrk */
typedef struct mem_block {
    size_t size;
    struct mem_block *next;
    int free;
} mem_block_t;

static mem_block_t *heap_head = NULL;

static void *malloc_impl(size_t size) {
    if (size == 0) return NULL;
    size = (size + 7) & ~7; /* align to 8 bytes */
    
    mem_block_t *curr = heap_head;
    while (curr) {
        if (curr->free && curr->size >= size) {
            curr->free = 0;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }
    
    /* Allocate new block */
    size_t total_size = sizeof(mem_block_t) + size;
    mem_block_t *block = (mem_block_t *)_sbrk(total_size);
    if (block == (void *)-1) return NULL;
    
    block->size = size;
    block->next = NULL;
    block->free = 0;
    
    if (!heap_head) {
        heap_head = block;
    } else {
        curr = heap_head;
        while (curr->next) curr = curr->next;
        curr->next = block;
    }
    return (void *)(block + 1);
}

static void free_impl(void *ptr) {
    if (!ptr) return;
    mem_block_t *block = (mem_block_t *)ptr - 1;
    block->free = 1;
    
    /* Coalesce adjacent free blocks */
    mem_block_t *curr = heap_head;
    while (curr && curr->next) {
        if (curr->free && curr->next->free) {
            curr->size += sizeof(mem_block_t) + curr->next->size;
            curr->next = curr->next->next;
        } else {
            curr = curr->next;
        }
    }
}

/* Public malloc/free (without underscore) */
void *malloc(size_t size) { return malloc_impl(size); }
void free(void *ptr) { free_impl(ptr); }

/* Underscore versions for compatibility */
void *_malloc(size_t size) { return malloc_impl(size); }
void _free(void *ptr) { free_impl(ptr); }

/* realloc/calloc stubs */
void *realloc(void *ptr, size_t size) {
    if (!ptr) return malloc(size);
    if (size == 0) { free(ptr); return NULL; }
    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    /* Note: we don't know the old size, so this is a best-effort copy */
    mem_block_t *block = (mem_block_t *)ptr - 1;
    size_t copy_size = block->size < size ? block->size : size;
    memcpy(new_ptr, ptr, copy_size);
    free(ptr);
    return new_ptr;
}

void *calloc(size_t nmemb, size_t size) {
    size_t total = nmemb * size;
    void *ptr = malloc(total);
    if (ptr) memset(ptr, 0, total);
    return ptr;
}

int _stat(const char *file, struct stat *st) {
    struct kernel_stat kst;
    long ret = __liw_syscall(22, (long)file, (long)&kst, 0);
    if (ret == 0) {
        st->st_dev = kst.st_dev;
        st->st_ino = kst.st_ino;
        st->st_mode = kst.st_mode;
        st->st_nlink = kst.st_nlink;
        st->st_uid = kst.st_uid;
        st->st_gid = kst.st_gid;
        st->st_rdev = kst.st_rdev;
        st->st_size = kst.st_size;
        st->st_atim.tv_sec = kst.st_atime;
        st->st_atim.tv_nsec = kst.st_atimensec;
        st->st_mtim.tv_sec = kst.st_mtime;
        st->st_mtim.tv_nsec = kst.st_mtimensec;
        st->st_ctim.tv_sec = kst.st_ctime;
        st->st_ctim.tv_nsec = kst.st_ctimensec;
        st->st_blksize = kst.st_blksize;
        st->st_blocks = kst.st_blocks;
    }
    return ret;
}

int _unlink(const char *name) {
    return __liw_syscall(24, (long)name, 0, 0);
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
    return __liw_syscall(21, (long)tv, (long)tz, 0);
}

clock_t _times(struct tms *buf) {
    (void)buf;
    long ticks = __liw_syscall(8, 0, 0, 0);
    if (buf) {
        buf->tms_utime = ticks;
        buf->tms_stime = 0;
        buf->tms_cutime = 0;
        buf->tms_cstime = 0;
    }
    return ticks;
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

/* Wrappers sem underscore para chamadas diretas dos aplicativos.
   Note: _READ_WRITE_RETURN_TYPE is defined as int by newlib on this target. */
int close(int fd) { return _close(fd); }
int fstat(int fd, struct stat *st) { return _fstat(fd, st); }
int getpid(void) { return _getpid(); }
int isatty(int fd) { return _isatty(fd); }
int kill(int pid, int sig) { return _kill(pid, sig); }
int link(const char *old, const char *new) { return _link(old, new); }
off_t lseek(int fd, off_t offset, int whence) { return _lseek(fd, offset, whence); }
int open(const char *file, int flags, ...) { return _open(file, flags); }
_READ_WRITE_RETURN_TYPE read(int fd, void *buf, size_t nbytes) { return _read(fd, buf, nbytes); }
void *sbrk(ptrdiff_t incr) { return _sbrk(incr); }
int stat(const char *file, struct stat *st) { return _stat(file, st); }
int unlink(const char *name) { return _unlink(name); }
int wait(int *status) { return _wait(status); }
_READ_WRITE_RETURN_TYPE write(int fd, const void *buf, size_t nbytes) { return _write(fd, buf, nbytes); }
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

/* ============================================================
 * Novas syscalls POSIX
 * ============================================================ */

int _mkdir(const char *path, mode_t mode) {
    return __liw_syscall(25, (long)path, (long)mode, 0);
}

int _chdir(const char *path) {
    return __liw_syscall(26, (long)path, 0, 0);
}

char *_getcwd(char *buf, size_t size) {
    long ret = __liw_syscall(27, (long)buf, (long)size, 0);
    if (ret < 0) return NULL;
    return buf;
}

int _rmdir(const char *path) {
    return __liw_syscall(29, (long)path, 0, 0);
}

int getdents(int fd, void *buf, unsigned int count) {
    return __liw_syscall(30, fd, (long)buf, count);
}

/* Non-underscore wrappers */
int mkdir(const char *path, mode_t mode) { return _mkdir(path, mode); }
int chdir(const char *path) { return _chdir(path); }
char *getcwd(char *buf, size_t size) { return _getcwd(buf, size); }
int rmdir(const char *path) { return _rmdir(path); }

/* Dirent operations.
   Avoid including unistd.h here to prevent clashes with SSP redefinitions.
   open/close/lseek are already declared in this file. */
#include <dirent.h>
#include <sys/types.h>

extern void *malloc(size_t);
extern void free(void *);

DIR *opendir(const char *path) {
    int fd = open(path, 0);
    if (fd < 0) return NULL;
    DIR *dir = (DIR *)malloc(sizeof(DIR));
    if (!dir) { close(fd); return NULL; }
    dir->dd_fd = fd;
    dir->dd_index = 0;
    return dir;
}

struct dirent *readdir(DIR *dirp) {
    if (!dirp) return NULL;
    struct dirent tmp;
    int ret = getdents(dirp->dd_fd, &tmp, sizeof(struct dirent));
    if (ret <= 0) return NULL;
    dirp->dd_entry = tmp;
    return &dirp->dd_entry;
}

int closedir(DIR *dirp) {
    if (!dirp) return -1;
    close(dirp->dd_fd);
    free(dirp);
    return 0;
}

void rewinddir(DIR *dirp) {
    if (!dirp) return;
    dirp->dd_index = 0;
    lseek(dirp->dd_fd, 0, 0); /* SEEK_SET = 0 */
}

int pipe(int pipefd[2]) {
    return __liw_syscall(31, (long)pipefd, 0, 0);
}

int dup(int oldfd) {
    return __liw_syscall(32, oldfd, 0, 0);
}

int dup2(int oldfd, int newfd) {
    return __liw_syscall(33, oldfd, newfd, 0);
}


#include <sys/mman.h>
void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    long ret = __liw_syscall6(35, (long)addr, (long)length, prot,
                              flags, fd, (long)offset);
    if (ret == -1) {
        errno = ENOMEM;
        return MAP_FAILED;
    }
    return (void *)ret;
}

int munmap(void *addr, size_t length) {
    return (int)__liw_syscall(36, (long)addr, (long)length, 0);
}

int fcntl(int fd, int cmd, ...) {
    return 0; // Fake success for F_SETFD/F_GETFL
}

struct sigaction;
int sigaction(int signum, const struct sigaction *act, struct sigaction *oldact) {
    return 0; // Fake success
}

struct rlimit;
int getrlimit(int resource, struct rlimit *rlim) { return 0; }
int setrlimit(int resource, const struct rlimit *rlim) { return 0; }
mode_t umask(mode_t mask) { return 022; }

struct pollfd;
int poll(struct pollfd *fds, unsigned long nfds, int timeout) {
    errno = ENOSYS;
    return -1;
}

typedef unsigned long sigset_t;
int sigprocmask(int how, const sigset_t *set, sigset_t *oldset) {
    return 0;
}

unsigned int geteuid(void) { return 0; }
unsigned int getppid(void) { return 0; }
int sigsuspend(const sigset_t *mask) { return -1; }

#include <glob.h>
int glob(const char *pattern, int flags, int (*errfunc)(const char *, int), glob_t *pglob) { return GLOB_NOMATCH; }
void globfree(glob_t *pglob) {}

struct passwd;
struct passwd *getpwnam(const char *name) { return (void*)0; }
int fnmatch(const char *pattern, const char *string, int flags) { return 1; /* FNM_NOMATCH */ }
