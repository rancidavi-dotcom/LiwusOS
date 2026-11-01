#include "isr.h"
#include "task.h"
#include "syscall.h"
#include "usercopy.h"
#include <stdint.h>

#define LINUX_ENOSYS 38
#define LINUX_ENOMEM 12
#define LINUX_EINVAL 22

extern int sys_read(int fd, void *buf, uint32_t count);
extern int sys_write(int fd, const void *buf, uint32_t count);
extern int sys_close(int fd);
extern uint64_t sys_mmap(void *addr, uint64_t length, int prot, int flags,
                         int fd, uint64_t offset);
extern int sys_munmap(void *addr, uint64_t length);
extern uint64_t sys_brk(uint64_t addr);
extern int sys_getpid(void);

/* Linux x86-64 syscall numbers and register ordering are translated here.
 * Internal services retain their native interface and return conventions. */
void linux_syscall_handler(registers_t *regs) {
  if (!regs || !current_task || current_task->abi != TASK_ABI_LINUX_X86_64) {
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
      regs->rax = (uint64_t)(int64_t)sys_close((int)regs->rdi);
      break;
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
    case 60: /* exit */
    case 231: /* exit_group (single-threaded process model for now) */
      sys_exit_process((int)regs->rdi);
      break;
    default:
      regs->rax = (uint64_t)-(int64_t)LINUX_ENOSYS;
      break;
  }
}
