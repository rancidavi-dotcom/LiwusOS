#ifndef LIWUS_USER_H
#define LIWUS_USER_H

/* Minimal freestanding user ABI for the LIWUS x86_64 int 0x80 interface. */
long liw_write(long fd, const void *buffer, unsigned long count);
long liw_read(long fd, void *buffer, unsigned long count);
long liw_fork(void);
long liw_execve(const char *filename, char *const argv[], char *const envp[]);
long liw_waitpid(int pid, int *status, int options);
long liw_open(const char *path, int flags);
long liw_close(int fd);
long liw_lseek(int fd, long offset, int whence);
long liw_mkdir(const char *path, unsigned int mode);
long liw_chdir(const char *path);
long liw_getcwd(char *buffer, unsigned long size);
long liw_unlink(const char *path);
long liw_rmdir(const char *path);
long liw_rename(const char *old_path, const char *new_path);
long liw_set_foreground(int pid);
long liw_job_create(void);
long liw_job_set_foreground(int job_id);
long liw_ioctl(int fd, unsigned long request, void *argument);
long liw_getdents(unsigned int fd, void *buffer, unsigned int count);
long liw_dup2(int oldfd, int newfd);
long liw_pipe(int descriptors[2]);
__attribute__((noreturn)) void liw_exit(int status);

#endif
