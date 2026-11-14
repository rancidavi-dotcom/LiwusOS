#ifndef LIWUS_UNISTD_H
#define LIWUS_UNISTD_H

#include <stddef.h>
#include <stdint.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
typedef long off_t;
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#ifndef STDIN_FILENO
#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#endif

#ifdef __cplusplus
extern "C" {
#endif

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int open(const char *pathname, int flags, ...);
int close(int fd);
off_t lseek(int fd, off_t offset, int whence);
int unlink(const char *pathname);
int remove(const char *pathname);
int getpid(void);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
int isatty(int fd);
int access(const char *pathname, int mode);
int dup(int oldfd);
int dup2(int oldfd, int newfd);
int execvp(const char *file, char *const argv[]);

#ifdef __cplusplus
}
#endif

#endif /* LIWUS_UNISTD_H */
