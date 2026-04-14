#ifndef LIWLIB_UNISTD_H
#define LIWLIB_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int open(const char *path, int flags, ...);
int close(int fd);
long lseek(int fd, long offset, int whence);
int execve(const char *path, char *const argv[], char *const envp[]);
pid_t fork(void);
pid_t waitpid(pid_t pid, int *status, int options);
int isatty(int fd);
void _exit(int status);
int brk(void *addr);
void *sbrk(ptrdiff_t increment);

#endif
