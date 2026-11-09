# Native Liwus ABI - Syscall Reference

This documents the current native ABI as implemented. See `docs/ABI.md` for full details. Numbers/semantics are LiwusOS-specific.

## Entry
- `int $0x80`, vector 128 DPL3
- RAX = syscall number
- Args: RDI, RSI, RDX, RCX, R8, R9
- Return: RAX

## Syscalls (current)
| # | Name | Args | Return | Notes |
|---|---|---|---|---|
| 1 | exit | int status | does not return | |
| 2 | brk | uint64_t addr | new break | |
| 3 | read | int fd, void* buf, uint32_t count | bytes read or < 0 on error | |
| 4 | write | int fd, const void* buf, uint32_t count | bytes written or < 0 on error | |
| 5 | open | const char* path, int flags | fd or -1 | flags differ from Linux |
| 6 | close | int fd | 0 or -1 | |
| 7 | waitpid | int pid, int* status, int options | child pid or -1 | blocks; returns raw exit code not POSIX status |
| 8 | timer | - | ticks (100Hz) | |
| 10 | key_event | struct* | 1 if consumed else 0 | |
| 11 | key_press | uint8_t scancode | 0/1 | |
| 14 | fork | - | child pid in parent, 0 in child | copies address space (no COW currently) |
| 15 | execve | const char* path, char*const argv[], char*const envp[] | replaces image or -errno/-1 | |
| 16 | tcgetattr | int fd, struct termios* | 0 or -1 | |
| 17 | tcsetattr | int fd, int action, const struct termios* | 0 or -1 | |
| 18 | ioctl | int fd, unsigned long req, void* arg | 0 or -1 | limited |
| 19 | lseek | int fd, int64_t off, int whence | new offset or -1 | |
| 20 | getpid | - | pid | |
| 21 | gettimeofday | struct{int64_t tv_sec,tv_usec}*, void* tz | 0 or -1 | |
| 22 | stat | const char* path, struct kernel_stat* | 0 or -1 | Liwus stat layout |
| 23 | fstat | int fd, struct kernel_stat* | 0 or -1 | |
| 24 | unlink | const char* path | 0 or -1 | |
| 25 | mkdir | const char* path, uint32_t mode | 0 or -1 | |
| 26 | chdir | const char* path | 0 or -1 | |
| 27 | getcwd | char* buf, uint32_t size | 0 or -1 | |
| 28 | kill | int pid, int sig | 0 or -1 | terminates target; sig ignored |
| 29 | rmdir | const char* path | 0 or -1 | |
| 30 | getdents | uint32_t fd, void* buf, uint32_t count | bytes written or -1 | Liwus dirent layout |
| 31 | pipe | int fds[2] | 0 or -1 | blocking via wait queues |
| 32 | dup | int fd | new fd or -1 | shares file description |
| 33 | dup2 | int oldfd, int newfd | newfd or -1 | |
| 35 | mmap | void* addr, uint64_t len, int prot, int flags, int fd, uint64_t off | addr or MAP_FAILED | anonymous only currently |
| 36 | munmap | void* addr, uint64_t len | 0 or -1 | |
| 37 | mouse_get | mouse state* | 0 or -1 | |
| 38 | rename | const char* old, const char* new | 0 or -1 | |
| 39 | set_foreground | int pid | 0 or -1 | |
| 40 | job_create | - | job id or -1 | |
| 41 | job_foreground | int job | 0 or -1 | |

Notes 9,12,13 unused. See `include/uapi/syscall_nums.h` and `docs/ABI.md` for full details on flags/structures/errors.
