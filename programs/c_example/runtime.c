#include "liwus.h"
#include "syscall_nums.h"

extern int main(int argc, char **argv, char **envp);

static long syscall3(long number, long a1, long a2, long a3) {
    long result;
    __asm__ volatile("int $0x80"
                     : "=a"(result)
                     : "0"(number), "D"(a1), "S"(a2), "d"(a3)
                     : "memory");
    return result;
}

long liw_write(long fd, const void *buffer, unsigned long count) {
    return syscall3(SYS_WRITE, fd, (long)buffer, (long)count);
}

long liw_read(long fd, void *buffer, unsigned long count) {
    return syscall3(SYS_READ, fd, (long)buffer, (long)count);
}

long liw_fork(void) {
    return syscall3(SYS_FORK, 0, 0, 0);
}

long liw_execve(const char *filename, char *const argv[], char *const envp[]) {
    return syscall3(SYS_EXECVE, (long)filename, (long)argv, (long)envp);
}

long liw_waitpid(int pid, int *status, int options) {
    return syscall3(SYS_WAITPID, pid, (long)status, options);
}

long liw_open(const char *path, int flags) {
    return syscall3(SYS_OPEN, (long)path, flags, 0);
}

long liw_close(int fd) {
    return syscall3(SYS_CLOSE, fd, 0, 0);
}

long liw_lseek(int fd, long offset, int whence) {
    return syscall3(SYS_LSEEK, fd, offset, whence);
}

long liw_mkdir(const char *path, unsigned int mode) {
    return syscall3(SYS_MKDIR, (long)path, mode, 0);
}

long liw_chdir(const char *path) {
    return syscall3(SYS_CHDIR, (long)path, 0, 0);
}

long liw_getcwd(char *buffer, unsigned long size) {
    return syscall3(SYS_GETCWD, (long)buffer, size, 0);
}

long liw_unlink(const char *path) {
    return syscall3(SYS_UNLINK, (long)path, 0, 0);
}

long liw_rmdir(const char *path) {
    return syscall3(SYS_RMDIR, (long)path, 0, 0);
}

long liw_rename(const char *old_path, const char *new_path) {
    return syscall3(SYS_RENAME, (long)old_path, (long)new_path, 0);
}

long liw_set_foreground(int pid) {
    return syscall3(SYS_SET_FOREGROUND, pid, 0, 0);
}

long liw_job_create(void) {
    return syscall3(SYS_JOB_CREATE, 0, 0, 0);
}

long liw_job_set_foreground(int job_id) {
    return syscall3(SYS_JOB_FOREGROUND, job_id, 0, 0);
}

long liw_ioctl(int fd, unsigned long request, void *argument) {
    return syscall3(SYS_IOCTL, fd, (long)request, (long)argument);
}

long liw_getdents(unsigned int fd, void *buffer, unsigned int count) {
    return syscall3(SYS_GETDENTS, fd, (long)buffer, count);
}

long liw_dup2(int oldfd, int newfd) {
    return syscall3(SYS_DUP2, oldfd, newfd, 0);
}

long liw_pipe(int descriptors[2]) {
    return syscall3(SYS_PIPE, (long)descriptors, 0, 0);
}

__attribute__((noreturn)) void liw_exit(int status) {
    syscall3(SYS_EXIT, status, 0, 0);
    for (;;) __asm__ volatile("pause");
}

void liw_crt_start(const unsigned long *initial_stack) {
    int argc = (int)initial_stack[0];
    char **argv = (char **)&initial_stack[1];
    char **envp = argv + argc + 1;
    liw_exit(main(argc, argv, envp));
}
