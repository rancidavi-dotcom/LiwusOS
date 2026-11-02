#ifndef LIWUS_SYSCALL_H
#define LIWUS_SYSCALL_H

#include <stddef.h>
#include <stdint.h>
#include "liwus/dirent.h"
#include "liwus/stat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Native LiwusOS syscalls - use int $0x80 (see docs/ABI.md) */
long liw_syscall0(long num);
long liw_syscall1(long num, long a1);
long liw_syscall2(long num, long a1, long a2);
long liw_syscall3(long num, long a1, long a2, long a3);
long liw_syscall4(long num, long a1, long a2, long a3, long a4);
long liw_syscall5(long num, long a1, long a2, long a3, long a4, long a5);
long liw_syscall6(long num, long a1, long a2, long a3, long a4, long a5, long a6);

/* Convenience wrappers for common syscalls */
long liw_write(int fd, const void *buf, size_t count);
long liw_read(int fd, void *buf, size_t count);
int liw_open(const char *path, int flags);
int liw_close(int fd);
void liw_exit(int status) __attribute__((noreturn));
int liw_getpid(void);
int liw_fork(void);
int liw_execve(const char *path, char *const argv[], char *const envp[]);
int liw_waitpid(int pid, int *status, int options);
void *liw_brk(void *addr);
void *liw_mmap(void *addr, size_t len, int prot, int flags, int fd, long off);
int liw_munmap(void *addr, size_t len);

/* Additional v0.1 wrappers. Return values preserve the native ABI result. */
long liw_lseek(int fd, int32_t offset, int whence);
long liw_stat(const char *path, struct liw_stat *out);
long liw_fstat(int fd, struct liw_stat *out);
long liw_getdents(unsigned int fd, struct liw_dirent *buffer, size_t count);
long liw_gettimeofday(struct liw_timeval *out, void *timezone);
long liw_unlink(const char *path);
long liw_mkdir(const char *path, uint32_t mode);
long liw_rmdir(const char *path);
long liw_rename(const char *old_path, const char *new_path);
long liw_chdir(const char *path);
long liw_getcwd(char *buffer, size_t size);
long liw_pipe(int descriptors[2]);
long liw_dup(int fd);
long liw_dup2(int oldfd, int newfd);

/* Writes until complete; returns 0 or the first negative native error. */
long liw_write_all(int fd, const void *buffer, size_t count);

#ifdef __cplusplus
}
#endif

#endif /* LIWUS_SYSCALL_H */
