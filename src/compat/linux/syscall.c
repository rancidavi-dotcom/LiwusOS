#include "isr.h"
#include "task.h"
#include "syscall.h"
#include "kstat.h"
#include "string.h"
#include "usercopy.h"
#include <stdint.h>

#define LINUX_ENOSYS 38
#define LINUX_ENOMEM 12
#define LINUX_EINVAL 22
#define LINUX_EBADF 9
#define LINUX_ENOENT 2
#define LINUX_EOVERFLOW 75
#define LINUX_ARCH_SET_GS 0x1001
#define LINUX_ARCH_SET_FS 0x1002
#define LINUX_ARCH_GET_FS 0x1003
#define LINUX_ARCH_GET_GS 0x1004
#define LINUX_AT_FDCWD (-100)
#define LINUX_O_CREAT 0x40
#define LINUX_O_TRUNC 0x200
#define LINUX_O_APPEND 0x400

extern int sys_read(int fd, void *buf, uint32_t count);
extern int sys_write(int fd, const void *buf, uint32_t count);
extern int sys_close(int fd);
extern uint64_t sys_mmap(void *addr, uint64_t length, int prot, int flags,
                         int fd, uint64_t offset);
extern int sys_munmap(void *addr, uint64_t length);
extern uint64_t sys_brk(uint64_t addr);
extern int sys_getpid(void);
extern int sys_open(const char *filename, int flags);
extern int sys_lseek(int fd, int offset, int whence);
extern int sys_stat(const char *path, struct kernel_stat *st);
extern int sys_fstat(int fd, struct kernel_stat *st);

typedef struct {
  uint64_t st_dev;
  uint64_t st_ino;
  uint64_t st_nlink;
  uint32_t st_mode;
  uint32_t st_uid;
  uint32_t st_gid;
  int32_t pad0;
  uint64_t st_rdev;
  int64_t st_size;
  int64_t st_blksize;
  int64_t st_blocks;
  int64_t st_atime;
  uint64_t st_atime_nsec;
  int64_t st_mtime;
  uint64_t st_mtime_nsec;
  int64_t st_ctime;
  uint64_t st_ctime_nsec;
  int64_t reserved[3];
} linux_stat_t;

_Static_assert(sizeof(linux_stat_t) == 144, "Linux x86-64 stat layout");

static int linux_copy_stat(const void *user_out) {
  struct kernel_stat native;
  if (copy_from_user(&native, user_out, sizeof(native)) < 0) return -14;
  linux_stat_t converted;
  memset(&converted, 0, sizeof(converted));
  converted.st_dev = native.st_dev;
  converted.st_ino = native.st_ino;
  converted.st_nlink = native.st_nlink;
  converted.st_mode = native.st_mode;
  converted.st_uid = native.st_uid;
  converted.st_gid = native.st_gid;
  converted.st_rdev = native.st_rdev;
  converted.st_size = (int64_t)native.st_size;
  converted.st_blksize = native.st_blksize;
  converted.st_blocks = native.st_blocks;
  converted.st_atime = native.st_atime;
  converted.st_atime_nsec = (uint64_t)native.st_atimensec;
  converted.st_mtime = native.st_mtime;
  converted.st_mtime_nsec = (uint64_t)native.st_mtimensec;
  converted.st_ctime = native.st_ctime;
  converted.st_ctime_nsec = (uint64_t)native.st_ctimensec;
  if (copy_to_user((void *)user_out, &converted, sizeof(converted)) < 0)
    return -14;
  return 0;
}

/* Linux x86-64 syscall numbers and register ordering are translated here.
 * Internal services retain their native interface and return conventions. */
void linux_syscall_handler(registers_t *regs) {
  if (!regs || !current_task || current_task->process->abi != TASK_ABI_LINUX_X86_64) {
    if (regs) regs->rax = (uint64_t)-(int64_t)LINUX_ENOSYS;
    return;
  }

  switch (regs->rax) {
    case 0: /* read */
      if (regs->rdx > UINT32_MAX) regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
      else regs->rax = (uint64_t)(int64_t)sys_read((int)regs->rdi,
                                                  (void *)regs->rsi,
                                                  (uint32_t)regs->rdx);
      break;
    case 1: /* write */
      if (regs->rdx > UINT32_MAX) regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
      else regs->rax = (uint64_t)(int64_t)sys_write((int)regs->rdi,
                                                    (const void *)regs->rsi,
                                                    (uint32_t)regs->rdx);
      break;
    case 3: /* close */
      {
        int rc = sys_close((int)regs->rdi);
        regs->rax = rc == -1 ? (uint64_t)-(int64_t)LINUX_EBADF
                             : (uint64_t)(int64_t)rc;
      }
      break;
    case 4: /* stat */
    case 6: { /* lstat (symlinks are not implemented by this VFS) */
      int rc = sys_stat((const char *)regs->rdi,
                        (struct kernel_stat *)regs->rsi);
      if (rc == -1) regs->rax = (uint64_t)-(int64_t)LINUX_ENOENT;
      else if (rc < 0) regs->rax = (uint64_t)(int64_t)rc;
      else regs->rax = (uint64_t)(int64_t)linux_copy_stat((void *)regs->rsi);
      break;
    }
    case 5: { /* fstat */
      int rc = sys_fstat((int)regs->rdi,
                         (struct kernel_stat *)regs->rsi);
      if (rc == -1) regs->rax = (uint64_t)-(int64_t)LINUX_EBADF;
      else if (rc < 0) regs->rax = (uint64_t)(int64_t)rc;
      else regs->rax = (uint64_t)(int64_t)linux_copy_stat((void *)regs->rsi);
      break;
    }
    case 8: { /* lseek */
      int64_t offset = (int64_t)regs->rsi;
      if (offset > INT32_MAX || offset < INT32_MIN) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EOVERFLOW;
      } else {
        int rc = sys_lseek((int)regs->rdi, (int)offset, (int)regs->rdx);
        regs->rax = rc == -1 ? (uint64_t)-(int64_t)LINUX_EINVAL
                             : (uint64_t)(int64_t)rc;
      }
      break;
    }
    case 257: { /* openat */
      if ((int64_t)regs->rdi != LINUX_AT_FDCWD) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EBADF;
        break;
      }
      int native_flags = 0;
      if (regs->rdx & LINUX_O_CREAT) native_flags |= 0x0200;
      if (regs->rdx & LINUX_O_TRUNC) native_flags |= 0x0400;
      if (regs->rdx & LINUX_O_APPEND) native_flags |= 0x0008;
      int rc = sys_open((const char *)regs->rsi, native_flags);
      regs->rax = rc < 0 ? (uint64_t)-(int64_t)LINUX_ENOENT : (uint64_t)rc;
      break;
    }
    case 262: { /* newfstatat, relative dirfds are not supported */
      if ((int64_t)regs->rdi != LINUX_AT_FDCWD || regs->r10 != 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      int rc = sys_stat((const char *)regs->rsi,
                        (struct kernel_stat *)regs->rdx);
      if (rc == -1) regs->rax = (uint64_t)-(int64_t)LINUX_ENOENT;
      else if (rc < 0) regs->rax = (uint64_t)(int64_t)rc;
      else regs->rax = (uint64_t)(int64_t)linux_copy_stat((void *)regs->rdx);
      break;
    }
    case 9: { /* mmap: arg4 is in R10, not RCX */
      uint64_t result = sys_mmap((void *)regs->rdi, regs->rsi,
                                 (int)regs->rdx, (int)regs->r10,
                                 (int)regs->r8, regs->r9);
      regs->rax = result == UINT64_MAX
          ? (uint64_t)-(int64_t)LINUX_ENOMEM : result;
      break;
    }
    case 11: /* munmap */
      regs->rax = (uint64_t)(int64_t)sys_munmap((void *)regs->rdi, regs->rsi);
      break;
    case 12: /* brk */
      regs->rax = sys_brk(regs->rdi);
      break;
    case 39: /* getpid */
      regs->rax = (uint64_t)sys_getpid();
      break;
    case 158: { /* arch_prctl: per-thread TLS bases */
      uint64_t base = regs->rsi;
      switch (regs->rdi) {
        case LINUX_ARCH_SET_FS:
          if (base && !is_user_addr((void *)base)) {
            regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
            break;
          }
          current_task->fs_base = base;
          write_msr_qword(0xC0000100U, base);
          regs->rax = 0;
          break;
        case LINUX_ARCH_SET_GS:
          if (base && !is_user_addr((void *)base)) {
            regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
            break;
          }
          current_task->user_gs_base = base;
          write_msr_qword(0xC0000102U, base);
          regs->rax = 0;
          break;
        case LINUX_ARCH_GET_FS:
          base = current_task->fs_base;
          regs->rax = copy_to_user((void *)regs->rsi, &base, sizeof(base)) < 0
              ? (uint64_t)-(int64_t)14 : 0;
          break;
        case LINUX_ARCH_GET_GS:
          base = current_task->user_gs_base;
          regs->rax = copy_to_user((void *)regs->rsi, &base, sizeof(base)) < 0
              ? (uint64_t)-(int64_t)14 : 0;
          break;
        default:
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
      }
      break;
    }
    case 60: /* exit */
    case 231: /* exit_group (single-threaded process model for now) */
      sys_exit_process((int)regs->rdi);
      break;
    default:
      regs->rax = (uint64_t)-(int64_t)LINUX_ENOSYS;
      break;
  }
}
