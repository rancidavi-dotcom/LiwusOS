#include "isr.h"
#include "task.h"
#include "syscall.h"
#include "kstat.h"
#include "pmm.h"
#include "string.h"
#include "usercopy.h"
#include "timer.h"
#include "vfs.h"
#include "kheap.h"
#include "vma.h"
#include "vmm.h"
#include <stdint.h>

#define LINUX_ENOSYS 38
#define LINUX_ENOMEM 12
#define LINUX_EINVAL 22
#define LINUX_EBADF 9
#define LINUX_ESRCH 3
#define LINUX_EMFILE 24
#define LINUX_ECHILD 10
#define LINUX_ENOENT 2
#define LINUX_EOVERFLOW 75
#define LINUX_EFAULT 14
#define LINUX_EISDIR 21
#define LINUX_EINTR 4
#define LINUX_ENOTDIR 20
#define LINUX_ENOSPC 28
#define LINUX_EFBIG 27
#define LINUX_ERANGE 34
#define LINUX_ESPIPE 29
#define LINUX_EOPNOTSUPP 95
#define LINUX_EAGAIN 11
#define LINUX_EIO 5
#define LINUX_ETIMEDOUT 110
#define LINUX_CLOSE_RANGE_CLOEXEC 4U
#define LINUX_PR_SET_NAME 15U
#define LINUX_PR_GET_NAME 16U
#define LINUX_TASK_COMM_LEN 16U
#define LINUX_TIMER_ABSTIME 1
#define LINUX_RLIMIT_NOFILE 7
#define LINUX_RLIMIT_NLIMITS 16
#define LINUX_FUTEX_WAIT 0
#define LINUX_FUTEX_WAKE 1
#define LINUX_FUTEX_REQUEUE 3
#define LINUX_FUTEX_CMP_REQUEUE 4
#define LINUX_FUTEX_WAIT_BITSET 9
#define LINUX_FUTEX_WAKE_BITSET 10
#define LINUX_FUTEX_PRIVATE_FLAG 128
#define LINUX_FUTEX_CLOCK_REALTIME 256
#define LINUX_F_DUPFD 0
#define LINUX_F_GETFD 1
#define LINUX_F_SETFD 2
#define LINUX_F_GETFL 3
#define LINUX_F_SETFL 4
#define LINUX_F_DUPFD_CLOEXEC 1030
#define LINUX_FD_CLOEXEC 1
#define LINUX_O_WRONLY 1
#define LINUX_O_RDWR 2
#define LINUX_O_ACCMODE 3
#define LINUX_ARCH_SET_GS 0x1001
#define LINUX_ARCH_SET_FS 0x1002
#define LINUX_ARCH_GET_FS 0x1003
#define LINUX_ARCH_GET_GS 0x1004
#define LINUX_AT_FDCWD (-100)
#define LINUX_AT_REMOVEDIR 0x200
#define LINUX_GRND_NONBLOCK 0x0001
#define LINUX_GRND_RANDOM   0x0002
#define LINUX_O_CREAT 0x40
#define LINUX_O_EXCL 0x80
#define LINUX_O_DIRECTORY 0x10000
#define LINUX_O_NOFOLLOW 0x20000
#define LINUX_O_LARGEFILE 0x8000
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100
#define LINUX_STATX_BASIC_STATS 0x07ff
#define LINUX_O_TRUNC 0x200
#define LINUX_O_APPEND 0x400
#define LINUX_O_NONBLOCK 0x800
#define LINUX_O_CLOEXEC 0x80000
#define LINUX_O_LARGEFILE 0x8000
#define LINUX_PIPE_CLOEXEC 0x80000
#define LINUX_CLONE_VM       0x00000100
#define LINUX_CLONE_FS       0x00000200
#define LINUX_CLONE_FILES    0x00000400
#define LINUX_CLONE_SIGHAND  0x00000800
#define LINUX_CLONE_THREAD   0x00010000
#define LINUX_CLONE_SYSVSEM  0x00040000
#define LINUX_CLONE_SETTLS   0x00080000
#define LINUX_CLONE_PARENT_SETTID 0x00100000
#define LINUX_CLONE_CHILD_SETTID  0x01000000
#define LINUX_CLONE_CHILD_CLEARTID 0x00200000
#define LINUX_CLONE_DETACHED 0x00400000
#define LINUX_CLONE_REQUIRED (LINUX_CLONE_VM | LINUX_CLONE_FS | \
                              LINUX_CLONE_FILES | LINUX_CLONE_SIGHAND | \
                              LINUX_CLONE_THREAD)
#define LIWUS_FD_TYPE_FILE 1
#define LIWUS_FD_TYPE_PIPE 3
#define LIWUS_FD_ACCESS_READ 0x01
#define LIWUS_FD_ACCESS_WRITE 0x02

extern int sys_read(int fd, void *buf, uint32_t count);
extern int sys_write(int fd, const void *buf, uint32_t count);
extern int sys_close(int fd);
extern int sys_dup(int oldfd);
extern int sys_dup2(int oldfd, int newfd);
extern int sys_pipe(int pipefd[2]);
extern uint64_t sys_mmap(void *addr, uint64_t length, int prot, int flags,
                         int fd, uint64_t offset);
extern int sys_munmap(void *addr, uint64_t length);
extern uint64_t sys_brk(uint64_t addr);
extern int sys_getpid(void);
extern int sys_open(const char *filename, int flags);
extern int sys_lseek(int fd, int offset, int whence);
extern int sys_access(const char *path, int mode);
extern int sys_chdir(const char *path);
extern int sys_fchdir(int fd);
extern int sys_mkdir(const char *path, uint32_t mode);
extern int sys_unlink(const char *path);
extern int sys_rmdir(const char *path);
extern int sys_rename(const char *old_path, const char *new_path);
extern int sys_rename_at(int olddirfd, int oldat_cwd, const char *old_path,
                         int newdirfd, int newat_cwd, const char *new_path);
extern int sys_stat(const char *path, struct kernel_stat *st);
extern int sys_fstat(int fd, struct kernel_stat *st);
extern int sys_file_sync(int fd, int data_only);
extern int sys_file_advice(int fd, int64_t offset, int64_t length,
                           int advice);

/* Generic process creation; the child inherits its process ABI personality. */
extern int fork_process(registers_t *regs);

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

/* Linux x86-64 struct utsname (sysname, nodename, release, version,
 * machine, domainname). Keep this personality-specific; it is not native UAPI. */
typedef struct {
  char sysname[65];
  char nodename[65];
  char release[65];
  char version[65];
  char machine[65];
  char domainname[65];
} linux_utsname_t;

_Static_assert(sizeof(linux_utsname_t) == 390,
               "Linux x86-64 utsname layout");

typedef struct {
  int64_t tv_sec;
  int64_t tv_nsec;
} linux_timespec64_t;

_Static_assert(sizeof(linux_timespec64_t) == 16,
               "Linux x86-64 timespec layout");

typedef struct {
  int64_t tv_sec;
  uint32_t tv_nsec;
  int32_t reserved;
} linux_statx_timestamp_t;

typedef struct {
  uint32_t mask;
  uint32_t blksize;
  uint64_t attributes;
  uint32_t nlink;
  uint32_t uid;
  uint32_t gid;
  uint16_t mode;
  uint16_t spare0;
  uint64_t ino;
  uint64_t size;
  uint64_t blocks;
  uint64_t attributes_mask;
  linux_statx_timestamp_t atime;
  linux_statx_timestamp_t btime;
  linux_statx_timestamp_t ctime;
  linux_statx_timestamp_t mtime;
  uint32_t rdev_major;
  uint32_t rdev_minor;
  uint32_t dev_major;
  uint32_t dev_minor;
  uint64_t mnt_id;
  uint32_t dio_mem_align;
  uint32_t dio_offset_align;
  uint64_t spare3[12];
} linux_statx_t;

_Static_assert(sizeof(linux_statx_t) == 256, "Linux x86-64 statx layout");

typedef struct {
  int64_t tv_sec;
  int64_t tv_usec;
} linux_timeval64_t;

_Static_assert(sizeof(linux_timeval64_t) == 16,
               "Linux x86-64 timeval layout");

typedef struct {
  linux_timeval64_t ru_utime;
  linux_timeval64_t ru_stime;
  int64_t counters[14];
} linux_rusage64_t;

_Static_assert(sizeof(linux_rusage64_t) == 144,
               "Linux x86-64 rusage layout");

typedef struct {
  uint64_t base;
  uint64_t length;
} linux_iovec64_t;

_Static_assert(sizeof(linux_iovec64_t) == 16,
               "Linux x86-64 iovec layout");

typedef struct {
  uint64_t current;
  uint64_t maximum;
} linux_rlimit64_t;

_Static_assert(sizeof(linux_rlimit64_t) == 16,
               "Linux x86-64 rlimit layout");

typedef struct {
  int64_t uptime;
  uint64_t loads[3];
  uint64_t totalram;
  uint64_t freeram;
  uint64_t sharedram;
  uint64_t bufferram;
  uint64_t totalswap;
  uint64_t freeswap;
  uint16_t procs;
  uint16_t pad;
  uint64_t totalhigh;
  uint64_t freehigh;
  uint32_t mem_unit;
  uint32_t reserved;
} linux_sysinfo64_t;

_Static_assert(sizeof(linux_sysinfo64_t) == 112,
               "Linux x86-64 sysinfo layout");

extern long time(long *out);

static void linux_utsname_set(char dst[65], const char *src) {
  size_t n = strlen(src);
  if (n > 64) n = 64;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

/* FUTEX_WAIT uses a relative timespec. The Liwus scheduler has 10 ms ticks,
 * so round a valid duration upward and reject values outside its safe
 * wrap-comparison window rather than timing out early. */
static int linux_futex_timeout_ticks(uint64_t user_timespec,
                                     uint32_t *out_ticks) {
  linux_timespec64_t ts;
  if (!out_ticks || copy_from_user(&ts, (const void *)user_timespec,
                                   sizeof(ts)) < 0)
    return -LINUX_EFAULT;
  if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL)
    return -LINUX_EINVAL;

  uint64_t fractional_ticks =
      ((uint64_t)ts.tv_nsec + 9999999ULL) / 10000000ULL;
  if ((uint64_t)ts.tv_sec > (uint64_t)INT32_MAX / 100ULL)
    return -LINUX_EINVAL;
  uint64_t ticks = (uint64_t)ts.tv_sec * 100ULL + fractional_ticks;
  if (ticks > INT32_MAX) return -LINUX_EINVAL;
  *out_ticks = (uint32_t)ticks;
  return 0;
}

/* FUTEX_WAIT_BITSET takes an absolute deadline. The scheduler stores relative
 * 100 Hz deadlines, so convert against the selected LIWUS clock and round up
 * to its tick resolution. */
static int linux_futex_absolute_timeout_ticks(uint64_t user_timespec,
                                               bool realtime,
                                               uint32_t *out_ticks) {
  linux_timespec64_t deadline;
  if (!out_ticks || copy_from_user(&deadline, (const void *)user_timespec,
                                   sizeof(deadline)) < 0)
    return -LINUX_EFAULT;
  if (deadline.tv_sec < 0 || deadline.tv_nsec < 0 ||
      deadline.tv_nsec >= 1000000000LL)
    return -LINUX_EINVAL;

  uint64_t ticks = timer_monotonic_ticks;
  int64_t now_sec = realtime ? (int64_t)time(NULL) : (int64_t)(ticks / 100ULL);
  int64_t now_nsec = (int64_t)((ticks % 100ULL) * 10000000ULL);
  if (now_sec < 0) return -LINUX_EINVAL;
  if (deadline.tv_sec < now_sec ||
      (deadline.tv_sec == now_sec && deadline.tv_nsec <= now_nsec))
    return -LINUX_ETIMEDOUT;

  uint64_t delta_sec = (uint64_t)(deadline.tv_sec - now_sec);
  int64_t delta_nsec = deadline.tv_nsec - now_nsec;
  if (delta_nsec < 0) {
    if (delta_sec == 0) return -LINUX_ETIMEDOUT;
    delta_sec--;
    delta_nsec += 1000000000LL;
  }
  if (delta_sec > (uint64_t)INT32_MAX / 100ULL)
    return -LINUX_EINVAL;
  uint64_t relative_ticks =
      delta_sec * 100ULL + ((uint64_t)delta_nsec + 9999999ULL) / 10000000ULL;
  if (relative_ticks == 0 || relative_ticks > INT32_MAX)
    return relative_ticks == 0 ? -LINUX_ETIMEDOUT : -LINUX_EINVAL;
  *out_ticks = (uint32_t)relative_ticks;
  return 0;
}

static int linux_nanosleep_milliseconds(uint64_t user_timespec,
                                        uint32_t *out_ms) {
  linux_timespec64_t ts;
  if (!out_ms || copy_from_user(&ts, (const void *)user_timespec,
                                sizeof(ts)) < 0)
    return -LINUX_EFAULT;
  if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000LL)
    return -LINUX_EINVAL;
  if ((uint64_t)ts.tv_sec > UINT32_MAX / 1000ULL)
    return -LINUX_EINVAL;
  uint64_t fractional_ms = ((uint64_t)ts.tv_nsec + 999999ULL) / 1000000ULL;
  uint64_t milliseconds = (uint64_t)ts.tv_sec * 1000ULL + fractional_ms;
  if (milliseconds > UINT32_MAX) return -LINUX_EINVAL;
  *out_ms = (uint32_t)milliseconds;
  return 0;
}

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

static int linux_statx(int dirfd, const char *user_path, uint32_t flags,
                       uint64_t user_out) {
  if (flags & ~LINUX_AT_SYMLINK_NOFOLLOW) return -LINUX_EINVAL;
  if (!user_out) return -LINUX_EFAULT;

  char path[256];
  if (strncpy_from_user(path, user_path, sizeof(path) - 1) < 0)
    return -LINUX_EFAULT;
  path[sizeof(path) - 1] = '\0';
  char resolved[512];
  int rc = sys_resolve_path_at(dirfd, dirfd == LINUX_AT_FDCWD, path,
                               resolved, sizeof(resolved));
  if (rc < 0) return rc;
  process_t *process = current_task && current_task->process
      ? current_task->process : NULL;
  fs_node_t *node = NULL;
  rc = vfs_open_access(resolved, process ? process->euid : 0,
                       process ? process->egid : 0, 0, &node);
  if (rc < 0) return rc;

  linux_statx_t out;
  memset(&out, 0, sizeof(out));
  out.mask = LINUX_STATX_BASIC_STATS;
  out.blksize = 4096;
  out.nlink = 1;
  out.uid = node->uid;
  out.gid = node->gid;
  if (node->flags & FS_DIRECTORY)
    out.mode = (uint16_t)(0040000u | (node->mask & 0777u));
  else if (node->flags & FS_CHARDEVICE)
    out.mode = (uint16_t)(0020000u | (node->mask & 0777u));
  else
    out.mode = (uint16_t)(0100000u | (node->mask & 0777u));
  out.ino = node->inode;
  out.size = node->length;
  out.blocks = ((uint64_t)node->length + 511u) / 512u;
  out.dev_minor = 1; /* LIWUS currently exposes one synthetic VFS device. */
  int64_t now_sec = (int64_t)time(NULL);
  out.atime.tv_sec = now_sec;
  out.atime.tv_nsec = 0;
  out.ctime.tv_sec = now_sec;
  out.ctime.tv_nsec = 0;
  out.mtime.tv_sec = now_sec;
  out.mtime.tv_nsec = 0;
  vfs_node_put(node);
  return copy_to_user((void *)(uintptr_t)user_out, &out, sizeof(out)) < 0
      ? -LINUX_EFAULT : 0;
}

static int linux_validate_io_fd(int fd, int writing) {
  if (fd < 0 || fd >= 32) return -LINUX_EBADF;
  kfile_t *file = &current_task->process->file_descriptors[fd];
  if (file->type == 0) {
    if ((!writing && fd == 0) || (writing && (fd == 1 || fd == 2)))
      return 0; /* LIWUS standard streams use the implicit terminal. */
    return -LINUX_EBADF;
  }
  if (file->type == LIWUS_FD_TYPE_PIPE) {
    if ((!writing && file->base_addr == 1) ||
        (writing && file->base_addr == 2)) return 0;
    return -LINUX_EBADF;
  }
  if (file->type == LIWUS_FD_TYPE_FILE) {
    if (file->description &&
        !(file->description->access &
          (writing ? LIWUS_FD_ACCESS_WRITE : LIWUS_FD_ACCESS_READ)))
      return -LINUX_EBADF;
    return 0;
  }
  return -LINUX_EBADF;
}

static int linux_getrlimit(uint64_t resource, uint64_t user_out) {
  if (resource >= LINUX_RLIMIT_NLIMITS) return -LINUX_EINVAL;
  /* The process table has exactly 32 descriptor slots (0 through 31). Other
   * limits are not reported as infinity because LIWUS does not enforce them. */
  if (resource != LINUX_RLIMIT_NOFILE) return -LINUX_ENOSYS;
  linux_rlimit64_t limit = { .current = 32, .maximum = 32 };
  if (!user_out || copy_to_user((void *)(uintptr_t)user_out, &limit,
                                sizeof(limit)) < 0)
    return -LINUX_EFAULT;
  return 0;
}

static int linux_translate_open_flags(uint64_t flags, int *native_flags,
                                      uint32_t *descriptor_flags,
                                      int *require_directory,
                                      uint32_t *status_flags) {
  /* O_NOFOLLOW is safely a no-op while the LIWUS VFS has no symlink nodes. */
  const uint64_t supported = LINUX_O_ACCMODE | LINUX_O_CREAT |
      LINUX_O_EXCL | LINUX_O_TRUNC | LINUX_O_APPEND | LINUX_O_NONBLOCK |
      LINUX_O_CLOEXEC | LINUX_O_DIRECTORY | LINUX_O_NOFOLLOW |
      LINUX_O_LARGEFILE;
  if ((flags & ~supported) != 0 ||
      (flags & LINUX_O_ACCMODE) == LINUX_O_ACCMODE ||
      ((flags & LINUX_O_EXCL) && !(flags & LINUX_O_CREAT)))
    return -LINUX_EINVAL;

  int translated = (int)(flags & LINUX_O_ACCMODE);
  if (flags & LINUX_O_CREAT) translated |= 0x0200;
  if (flags & LINUX_O_EXCL) translated |= 0x0800;
  if (flags & LINUX_O_TRUNC) translated |= 0x0400;
  if (flags & LINUX_O_APPEND) translated |= 0x0008;
  *native_flags = translated;
  *descriptor_flags = (flags & LINUX_O_CLOEXEC) ? KFILE_FLAG_CLOEXEC : 0;
  *require_directory = (flags & LINUX_O_DIRECTORY) != 0;
  *status_flags = (flags & LINUX_O_NONBLOCK)
      ? KFILE_STATUS_NONBLOCK : 0;
  return 0;
}

/* Linux brk exposes the exact program-break byte address, while its mapping
 * changes are page-granular. Keep the exact value in process state and use
 * the shared LIWUS VMA/PMM/VMM mechanisms underneath. */
static uint64_t linux_sys_brk(uint64_t requested) {
  if (!current_task || !current_task->process || !current_task->process->mm)
    return 0;
  process_t *process = current_task->process;
  page_directory_t *mm = process->mm;
  if (!process->linux_brk_start) {
    process->linux_brk_start = process->heap_start;
    process->linux_brk_current = process->heap_end;
  }
  uint64_t old_break = process->linux_brk_current;
  if (!requested) return old_break;
  if (requested < process->linux_brk_start || requested >= USER_ADDR_MAX ||
      requested > UINT64_MAX - (PAGE_SIZE - 1))
    return old_break;
  if (requested == old_break) return old_break;

  uint64_t heap_start_page = process->linux_brk_start + PAGE_SIZE - 1;
  heap_start_page &= ~((uint64_t)PAGE_SIZE - 1);
  uint64_t old_end_page = old_break + PAGE_SIZE - 1;
  old_end_page &= ~((uint64_t)PAGE_SIZE - 1);
  uint64_t new_end_page = requested + PAGE_SIZE - 1;
  new_end_page &= ~((uint64_t)PAGE_SIZE - 1);
  if (new_end_page < heap_start_page || old_end_page < heap_start_page)
    return old_break;

  if (new_end_page > old_end_page) {
    uint64_t page_count = (new_end_page - old_end_page) / PAGE_SIZE;
    if (page_count > pmm_get_free_memory() / PAGE_SIZE) return old_break;
    uint64_t page = old_end_page;
    for (; page < new_end_page; page += PAGE_SIZE) {
      if (vma_find(mm, page) || vmm_page_present_in(mm, page)) break;
      void *frame = pmm_alloc_block();
      if (!frame) break;
      memset(frame, 0, PAGE_SIZE);
      if (vmm_map_page_in(mm, frame, (void *)page,
                          PTE_P | PTE_W | PTE_U | PTE_NX) < 0) {
        pmm_free_block(frame);
        break;
      }
    }
    if (page != new_end_page) {
      for (uint64_t rollback = old_end_page; rollback < page;
           rollback += PAGE_SIZE)
        vmm_unmap_page_in(mm, (void *)rollback);
      return old_break;
    }
    if (new_end_page > heap_start_page &&
        vma_resize_heap(mm, heap_start_page, new_end_page) < 0) {
      for (uint64_t rollback = old_end_page; rollback < new_end_page;
           rollback += PAGE_SIZE)
        vmm_unmap_page_in(mm, (void *)rollback);
      return old_break;
    }
  } else if (new_end_page < old_end_page) {
    for (uint64_t page = new_end_page; page < old_end_page;
         page += PAGE_SIZE)
      if (!vmm_page_present_in(mm, page)) return old_break;
    if (vma_resize_heap(mm, heap_start_page, new_end_page) < 0)
      return old_break;
    for (uint64_t page = new_end_page; page < old_end_page;
         page += PAGE_SIZE)
      if (vmm_unmap_page_in(mm, (void *)page) < 0)
        return old_break;
  }

  process->linux_brk_current = requested;
  return requested;
}

typedef struct __attribute__((packed)) {
  uint64_t ino;
  int64_t off;
  uint16_t reclen;
  uint8_t type;
} linux_dirent64_header_t;

_Static_assert(sizeof(linux_dirent64_header_t) == 19,
               "Linux x86-64 dirent64 header layout");

/* Linux getdents64 returns variable-length, 8-byte-aligned records. Keep the
 * Linux layout in this personality and consume the generic VFS directory API. */
static int64_t linux_getdents64(int fd, void *user_buffer, uint64_t count) {
  if (!current_task || !current_task->process || fd < 0 || fd >= 32)
    return -LINUX_EBADF;
  /* Standard streams are valid character devices in the Linux personality,
   * though they have no file_description object in the LIWUS process table. */
  if (fd <= 2) return -LINUX_ENOTDIR;

  kfile_t *file = &current_task->process->file_descriptors[fd];
  if (file->type != LIWUS_FD_TYPE_FILE) return -LINUX_EBADF;
  fs_node_t *directory = file->description ? file->description->node
                                            : (fs_node_t *)file->socket_ptr;
  if (!directory || !(directory->flags & FS_DIRECTORY) || !directory->readdir)
    return -LINUX_ENOTDIR;
  if (count == 0) return 0;
  if (!user_buffer) return -LINUX_EFAULT;

  uint32_t *offset = file->description ? &file->description->offset
                                       : &file->offset;
  uint32_t index = *offset;
  uint64_t bytes_written = 0;
  for (;;) {
    struct dirent *entry = readdir_fs(directory, index);
    if (!entry) break;

    char name[sizeof(entry->name)];
    size_t name_length = 0;
    while (name_length < sizeof(entry->name) && entry->name[name_length]) {
      name[name_length] = entry->name[name_length];
      name_length++;
    }
    if (name_length == 0 || name_length == sizeof(entry->name)) {
      if (bytes_written) { *offset = index; return (int64_t)bytes_written; }
      return -LINUX_EOVERFLOW;
    }
    name[name_length] = '\0';
    if (index == UINT32_MAX) {
      if (bytes_written) { *offset = index; return (int64_t)bytes_written; }
      return -LINUX_EOVERFLOW;
    }

    size_t raw_length = sizeof(linux_dirent64_header_t) + name_length + 1;
    size_t record_length = (raw_length + 7U) & ~(size_t)7U;
    if (record_length > UINT16_MAX || record_length > count - bytes_written) {
      if (bytes_written) { *offset = index; return (int64_t)bytes_written; }
      return -LINUX_EINVAL;
    }

    uint8_t record[sizeof(linux_dirent64_header_t) + sizeof(name) + 8];
    memset(record, 0, record_length);
    linux_dirent64_header_t header = {
      .ino = entry->ino,
      .off = (int64_t)index + 1,
      .reclen = (uint16_t)record_length,
      .type = 0
    };
    memcpy(record, &header, sizeof(header));
    memcpy(record + sizeof(header), name, name_length);

    fs_node_t *child = finddir_fs(directory, name);
    if (child) {
      if (child->flags & FS_DIRECTORY) header.type = 4; /* DT_DIR */
      else header.type = 8; /* DT_REG */
      vfs_node_put(child);
      memcpy(record, &header, sizeof(header));
    }

    if (copy_to_user((uint8_t *)user_buffer + bytes_written,
                     record, record_length) < 0) {
      if (bytes_written) { *offset = index; return (int64_t)bytes_written; }
      return -LINUX_EFAULT;
    }

    bytes_written += record_length;
    index++;
  }

  *offset = index;
  return (int64_t)bytes_written;
}

static int64_t linux_lseek(int fd, int64_t offset, int whence) {
  if (!current_task || !current_task->process || fd < 0 || fd >= 32)
    return -LINUX_EBADF;
  if (fd <= 2) return -LINUX_ESPIPE;

  kfile_t *file = &current_task->process->file_descriptors[fd];
  if (file->type != LIWUS_FD_TYPE_FILE) return -LINUX_EBADF;
  fs_node_t *node = file->description ? file->description->node
                                      : (fs_node_t *)file->socket_ptr;
  if (node && (node->flags & FS_DIRECTORY)) {
    if (whence != 0 && whence != 1) return -LINUX_EINVAL;
    uint32_t *cursor = file->description ? &file->description->offset
                                         : &file->offset;
    int64_t base = whence == 0 ? 0 : *cursor;
    if ((offset > 0 && base > INT64_MAX - offset) ||
        (offset < 0 && base < INT64_MIN - offset))
      return -LINUX_EOVERFLOW;
    int64_t next = base + offset;
    if (next < 0) return -LINUX_EINVAL;
    if ((uint64_t)next > UINT32_MAX) return -LINUX_EOVERFLOW;
    *cursor = (uint32_t)next;
    file->offset = *cursor;
    return next;
  }

  if (offset > INT32_MAX || offset < INT32_MIN) return -LINUX_EOVERFLOW;
  int result = sys_lseek(fd, (int)offset, whence);
  return result == -1 ? -LINUX_EINVAL : result;
}

/* Positional I/O is implemented at the Linux boundary so it never borrows or
 * changes the shared open-file offset used by read/write/dup/fork. The VFS
 * node operations remain the LIWUS implementation underneath. */
static int64_t linux_preadwrite(int fd, uint64_t user_buffer, uint64_t count,
                                int64_t offset, int writing) {
  if (offset < 0) return -LINUX_EINVAL;
  int fd_rc = linux_validate_io_fd(fd, writing);
  if (fd_rc < 0) return fd_rc;
  if (fd <= 2 || fd >= 32 ||
      current_task->process->file_descriptors[fd].type != LIWUS_FD_TYPE_FILE)
    return -LINUX_ESPIPE;
  if (count > 0x7ffff000ULL) count = 0x7ffff000ULL;
  if (count == 0) return 0;

  kfile_t *file = &current_task->process->file_descriptors[fd];
  fs_node_t *node = file->description ? file->description->node
                                      : (fs_node_t *)file->socket_ptr;
  if (node && (node->flags & FS_DIRECTORY)) return -LINUX_EISDIR;
  if (!node || !(node->flags & FS_FILE))
    return writing ? -LINUX_EBADF : -LINUX_ESPIPE;
  if (writing && (!node->write ||
      (file->description &&
       !(file->description->access & LIWUS_FD_ACCESS_WRITE))))
    return -LINUX_EBADF;
  if (!writing && (!node->read ||
      (file->description &&
       !(file->description->access & LIWUS_FD_ACCESS_READ))))
    return -LINUX_EBADF;

  if (writing && (file->description &&
      (file->description->status_flags & KFILE_STATUS_APPEND)))
    offset = node->length; /* Linux currently honors O_APPEND for pwrite. */
  if ((uint64_t)offset > UINT32_MAX ||
      (writing && count > UINT32_MAX - (uint64_t)offset))
    return writing ? -LINUX_EFBIG : 0;

  /* Validate a pwrite source before extending the file with a zero-filled
   * hole. An invalid user buffer must not leave a partially modified file. */
  if (writing && validate_user_pointer((const void *)(uintptr_t)user_buffer,
                                       (size_t)count) < 0)
    return -LINUX_EFAULT;

  if (writing && (uint32_t)offset > node->length) {
    uint8_t zeros[256];
    memset(zeros, 0, sizeof(zeros));
    uint32_t gap = (uint32_t)offset - node->length;
    while (gap) {
      uint32_t chunk = gap > sizeof(zeros) ? sizeof(zeros) : gap;
      uint32_t amount = write_fs(node, node->length, chunk, zeros);
      if (file->description) file->description->size = node->length;
      file->size = node->length;
      if (!amount) return -LINUX_ENOSPC;
      gap -= amount;
    }
  }

  uint32_t position = (uint32_t)offset;
  /* Linux returns EOF without touching the destination, even if it is bad. */
  if (!writing && position >= node->length) return 0;
  if (!writing && count > node->length - position)
    count = node->length - position;
  if (!writing && validate_user_write((void *)(uintptr_t)user_buffer,
                                      (size_t)count) < 0) {
    return -LINUX_EFAULT;
  }

  uint8_t chunk_buffer[256];
  uint64_t transferred = 0;
  while (transferred < count) {
    uint32_t chunk = (uint32_t)(count - transferred);
    if (chunk > sizeof(chunk_buffer)) chunk = sizeof(chunk_buffer);
    uint32_t amount;
    if (writing) {
      if (copy_from_user(chunk_buffer,
                         (const void *)(uintptr_t)(user_buffer + transferred),
                         chunk) < 0)
        return transferred ? (int64_t)transferred : -LINUX_EFAULT;
      amount = write_fs(node, position + (uint32_t)transferred, chunk,
                        chunk_buffer);
      if (file->description) file->description->size = node->length;
      file->size = node->length;
    } else {
      amount = read_fs(node, position + (uint32_t)transferred, chunk,
                       chunk_buffer);
      if (amount && copy_to_user(
              (void *)(uintptr_t)(user_buffer + transferred), chunk_buffer,
              amount) < 0)
        return transferred ? (int64_t)transferred : -LINUX_EFAULT;
    }
    if (amount == 0) {
      if (writing && transferred == 0) return -LINUX_ENOSPC;
      break;
    }
    transferred += amount;
    if (amount < chunk) break;
  }
  return (int64_t)transferred;
}

/* Linux getrandom personality: use the CPU RNG only when CPUID advertises
 * RDRAND. This intentionally has no insecure PRNG fallback. RDRAND failure
 * after bounded retries returns EIO (or a short count after partial output). */
static int64_t linux_getrandom(uint64_t user_buffer, uint64_t count,
                              uint32_t flags) {
  if (flags & ~(LINUX_GRND_NONBLOCK | LINUX_GRND_RANDOM))
    return -LINUX_EINVAL;
  if (count == 0) return 0;
  /* A short read is valid for getrandom; bound syscall latency and usercopy. */
  if (count > 256) count = 256;
  if (validate_user_write((void *)(uintptr_t)user_buffer, (size_t)count) < 0)
    return -LINUX_EFAULT;

  uint32_t eax = 1, ebx, ecx, edx;
  asm volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  if (!(ecx & (1u << 30))) return -LINUX_EIO;

  uint64_t transferred = 0;
  while (transferred < count) {
    uint64_t word = 0;
    uint8_t success = 0;
    for (int attempt = 0; attempt < 10 && !success; attempt++)
      asm volatile("rdrand %0; setc %1"
                   : "=r"(word), "=qm"(success));
    if (!success) return transferred ? (int64_t)transferred : -LINUX_EIO;

    uint64_t remaining = count - transferred;
    size_t chunk = remaining < sizeof(word) ? (size_t)remaining
                                             : sizeof(word);
    if (copy_to_user((void *)(uintptr_t)(user_buffer + transferred), &word,
                     chunk) < 0)
      return transferred ? (int64_t)transferred : -LINUX_EFAULT;
    transferred += chunk;
  }
  return (int64_t)transferred;
}

static int64_t linux_close_range(uint32_t first, uint32_t last,
                                 uint32_t flags) {
  if (first > last || (flags & ~LINUX_CLOSE_RANGE_CLOEXEC))
    return -LINUX_EINVAL;
  if (first >= 32) return 0;
  uint32_t end = last < 31 ? last : 31;
  for (uint32_t fd = first; fd <= end; fd++) {
    kfile_t *file = &current_task->process->file_descriptors[fd];
    if (file->type == 0) continue; /* close_range ignores holes */
    if (flags & LINUX_CLOSE_RANGE_CLOEXEC)
      file->descriptor_flags |= KFILE_FLAG_CLOEXEC;
    else
      (void)sys_close((int)fd); /* Linux ignores per-descriptor close errors */
  }
  return 0;
}

/* Linux prctl subset for the calling thread's 16-byte comm name. */
static int64_t linux_prctl(uint64_t option, uint64_t argument) {
  if (option == LINUX_PR_SET_NAME) {
    char name[LINUX_TASK_COMM_LEN] = {0};
    for (size_t i = 0; i < sizeof(name); i++) {
      if (copy_from_user(&name[i],
                         (const void *)(uintptr_t)(argument + i), 1) < 0)
        return -LINUX_EFAULT;
      if (name[i] == '\0') break;
    }
    name[sizeof(name) - 1] = '\0';
    memset(current_task->name, 0, sizeof(current_task->name));
    memcpy(current_task->name, name, sizeof(name));
    return 0;
  }
  if (option == LINUX_PR_GET_NAME) {
    char name[LINUX_TASK_COMM_LEN] = {0};
    memcpy(name, current_task->name, sizeof(name));
    name[sizeof(name) - 1] = '\0';
    return copy_to_user((void *)(uintptr_t)argument, name, sizeof(name)) < 0
        ? -LINUX_EFAULT : 0;
  }
  return -LINUX_EINVAL;
}

static int64_t linux_wait4(int pid, void *user_status, uint32_t options,
                           void *user_rusage) {
  if ((options & ~1U) != 0) return -LINUX_EINVAL; /* WNOHANG only */
  if (pid == 0 || pid < -1) return -LINUX_ECHILD; /* no process-group model */
  if (user_status && validate_user_write(user_status, sizeof(int)) < 0)
    return -LINUX_EFAULT;
  /* LIWUS has no per-process accounting; preserve the Linux layout with zeros. */
  if (user_rusage && validate_user_write(user_rusage,
                                          sizeof(linux_rusage64_t)) < 0)
    return -LINUX_EFAULT;

  int native_status = 0;
  int result = task_waitpid_kernel(pid, &native_status, (int)(options & 1U));
  if (result == -1) return -LINUX_ECHILD;
  if (result < 0) return result;
  if (result == 0) return 0; /* WNOHANG: leave outputs untouched */

  /* LIWUS currently has only exit-code termination, not Linux signal state. */
  int linux_status = (native_status & 0xff) << 8;
  if (user_status && copy_to_user(user_status, &linux_status,
                                  sizeof(linux_status)) < 0)
    return -LINUX_EFAULT;
  if (user_rusage) {
    linux_rusage64_t zero_usage = {0};
    if (copy_to_user(user_rusage, &zero_usage, sizeof(zero_usage)) < 0)
      return -LINUX_EFAULT;
  }
  return result;
}

static int64_t linux_readv_writev(int fd, uint64_t user_iov, uint64_t iov_count,
                                  int is_write) {
  int fd_rc = linux_validate_io_fd(fd, is_write);
  if (fd_rc < 0) return fd_rc;
  if (iov_count > 1024) return -LINUX_EINVAL;
  if (iov_count == 0) return 0;

  size_t bytes = (size_t)iov_count * sizeof(linux_iovec64_t);
  linux_iovec64_t *iov = (linux_iovec64_t *)kmalloc(bytes);
  if (!iov) return -LINUX_ENOMEM;
  if (copy_from_user(iov, (const void *)user_iov, bytes) < 0) {
    kfree(iov);
    return -LINUX_EFAULT;
  }

  uint64_t total_length = 0;
  for (uint64_t i = 0; i < iov_count; i++) {
    if (iov[i].length > INT64_MAX - total_length) {
      kfree(iov);
      return -LINUX_EINVAL;
    }
    total_length += iov[i].length;
  }

  int64_t total = 0;
  for (uint64_t i = 0; i < iov_count; i++) {
    if (iov[i].length == 0) continue;
    if (iov[i].length > UINT32_MAX) {
      kfree(iov);
      return total ? total : -LINUX_EINVAL;
    }
    int result = is_write
        ? sys_write(fd, (const void *)(uintptr_t)iov[i].base,
                    (uint32_t)iov[i].length)
        : sys_read(fd, (void *)(uintptr_t)iov[i].base,
                   (uint32_t)iov[i].length);
    if (result < 0) {
      kfree(iov);
      if (total) return total;
      return result == -1 ? -LINUX_EINTR : result;
    }
    total += result;
    if ((uint32_t)result < (uint32_t)iov[i].length) break;
  }

  kfree(iov);
  return total;
}

/* x86-64 preadv/pwritev pass the positional offset in two registers. Keep
 * each vector positional so the shared open-file-description offset remains
 * unchanged, matching pread64/pwrite64. */
static int64_t linux_preadv_writev(int fd, uint64_t user_iov,
                                   uint64_t iov_count, int64_t offset,
                                   int is_write) {
  if (offset < 0) return -LINUX_EINVAL;
  int fd_rc = linux_validate_io_fd(fd, is_write);
  if (fd_rc < 0) return fd_rc;
  if (iov_count > 1024) return -LINUX_EINVAL;
  if (iov_count == 0) return 0;

  size_t bytes = (size_t)iov_count * sizeof(linux_iovec64_t);
  linux_iovec64_t *iov = (linux_iovec64_t *)kmalloc(bytes);
  if (!iov) return -LINUX_ENOMEM;
  if (copy_from_user(iov, (const void *)user_iov, bytes) < 0) {
    kfree(iov);
    return -LINUX_EFAULT;
  }

  int64_t total = 0;
  for (uint64_t i = 0; i < iov_count; i++) {
    if (iov[i].length > (uint64_t)(INT64_MAX - total) ||
        (uint64_t)offset > INT64_MAX - (uint64_t)total) {
      kfree(iov);
      return -LINUX_EINVAL;
    }
    if (iov[i].length == 0) continue;
    if (iov[i].length > UINT32_MAX) {
      kfree(iov);
      return total ? total : -LINUX_EINVAL;
    }
    int64_t result = linux_preadwrite(fd, iov[i].base, iov[i].length,
                                      offset + total, is_write);
    if (result < 0) {
      kfree(iov);
      return total ? total : result;
    }
    total += result;
    if ((uint64_t)result < iov[i].length) break;
  }

  kfree(iov);
  return total;
}

/* Linux x86-64 syscall numbers and register ordering are translated here.
 * Internal services retain their native interface and return conventions. */
void linux_syscall_handler(registers_t *regs) {
  if (!regs || !current_task || current_task->process->abi != TASK_ABI_LINUX_X86_64) {
    if (regs) regs->rax = (uint64_t)-(int64_t)LINUX_ENOSYS;
    return;
  }

  switch (regs->rax) {
    case 2: { /* open: legacy x86-64 syscall retained by Linux */
      int native_flags = 0;
      uint32_t descriptor_flags = 0;
      uint32_t status_flags = 0;
      int require_directory = 0;
      int rc = linux_translate_open_flags(regs->rsi, &native_flags,
                                          &descriptor_flags,
                                          &require_directory, &status_flags);
      if (rc == 0) {
        rc = sys_openat_mode(LINUX_AT_FDCWD, 1, (const char *)regs->rdi,
                             native_flags, (uint32_t)regs->rdx,
                             descriptor_flags, require_directory,
                             status_flags);
        if (rc == -2) rc = -LINUX_ENOENT;
        else if (rc == -1) rc = -LINUX_ENOMEM;
      }
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 82: { /* rename: legacy pathname form backed by generic VFS */
      int rc = sys_rename((const char *)regs->rdi,
                          (const char *)regs->rsi);
      if (rc == -1) rc = -LINUX_ENOENT;
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 264: { /* renameat: translate both directory-fd path bases */
      int olddirfd = (int)regs->rdi;
      int newdirfd = (int)regs->rdx;
      int rc = sys_rename_at(olddirfd, olddirfd == LINUX_AT_FDCWD,
                             (const char *)regs->rsi,
                             newdirfd, newdirfd == LINUX_AT_FDCWD,
                             (const char *)regs->r10);
      if (rc == -1) rc = -LINUX_ENOENT;
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 316: { /* renameat2: only the flags=0 renameat subset exists */
      if (regs->r8 != 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      int olddirfd = (int)regs->rdi;
      int newdirfd = (int)regs->rdx;
      int rc = sys_rename_at(olddirfd, olddirfd == LINUX_AT_FDCWD,
                             (const char *)regs->rsi,
                             newdirfd, newdirfd == LINUX_AT_FDCWD,
                             (const char *)regs->r10);
      if (rc == -1) rc = -LINUX_ENOENT;
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 83: { /* mkdir: legacy pathname form of mkdirat */
      int rc = sys_mkdir_at(LINUX_AT_FDCWD, 1,
                            (const char *)regs->rdi, (uint32_t)regs->rsi);
      regs->rax = rc == -1 ? (uint64_t)-(int64_t)LINUX_ENOENT
                           : (uint64_t)(int64_t)rc;
      break;
    }
    case 84: { /* rmdir: legacy pathname form of unlinkat(AT_REMOVEDIR) */
      int rc = sys_unlink_at(LINUX_AT_FDCWD, 1,
                             (const char *)regs->rdi, 1);
      regs->rax = rc == -1 ? (uint64_t)-(int64_t)LINUX_ENOENT
                           : (uint64_t)(int64_t)rc;
      break;
    }
    case 87: { /* unlink: legacy pathname form of unlinkat */
      int rc = sys_unlink_at(LINUX_AT_FDCWD, 1,
                             (const char *)regs->rdi, 0);
      regs->rax = rc == -1 ? (uint64_t)-(int64_t)LINUX_ENOENT
                           : (uint64_t)(int64_t)rc;
      break;
    }
    case 318: /* getrandom(buffer, count, GRND_NONBLOCK|GRND_RANDOM) */
      regs->rax = (uint64_t)linux_getrandom(regs->rdi, regs->rsi,
                                            (uint32_t)regs->rdx);
      break;
    case 436: /* close_range: close or mark CLOEXEC, bounded by LIWUS fd table */
      regs->rax = (uint64_t)linux_close_range((uint32_t)regs->rdi,
          (uint32_t)regs->rsi, (uint32_t)regs->rdx);
      break;
    case 157: /* prctl: per-thread PR_SET_NAME/PR_GET_NAME subset */
      regs->rax = (uint64_t)linux_prctl(regs->rdi, regs->rsi);
      break;
    case 21: { /* access: generic VFS permissions with real credentials */
      int rc = sys_access((const char *)regs->rdi, (int)regs->rsi);
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 269: { /* faccessat: legacy interface (flags are not present) */
      int dirfd = (int)regs->rdi;
      int rc = sys_access_at(dirfd, dirfd == LINUX_AT_FDCWD,
                             (const char *)regs->rsi, (int)regs->rdx);
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 439: { /* faccessat2: current VFS contract supports flags=0 only */
      if (regs->r10 != 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      int dirfd = (int)regs->rdi;
      int rc = sys_access_at(dirfd, dirfd == LINUX_AT_FDCWD,
                             (const char *)regs->rsi, (int)regs->rdx);
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 22: { /* pipe */
      int rc = sys_pipe((int *)regs->rdi);
      regs->rax = rc == -1 ? (uint64_t)-(int64_t)LINUX_ENOMEM
                           : (uint64_t)(int64_t)rc;
      break;
    }
    case 293: { /* pipe2 supports atomic CLOEXEC and nonblocking endpoints */
      if (regs->rsi & ~(LINUX_PIPE_CLOEXEC | LINUX_O_NONBLOCK)) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      int rc = sys_pipe_with_options((int *)regs->rdi,
          (regs->rsi & LINUX_PIPE_CLOEXEC) ? KFILE_FLAG_CLOEXEC : 0,
          (regs->rsi & LINUX_O_NONBLOCK)
              ? KFILE_STATUS_NONBLOCK : 0);
      if (rc < 0) {
        regs->rax = rc == -1 ? (uint64_t)-(int64_t)LINUX_ENOMEM
                             : (uint64_t)(int64_t)rc;
        break;
      }
      regs->rax = 0;
      break;
    }
    case 17: /* pread64: positional read, offset is in R10 on x86-64 */
      regs->rax = (uint64_t)linux_preadwrite(
          (int)regs->rdi, regs->rsi, regs->rdx, (int64_t)regs->r10, 0);
      break;
    case 18: /* pwrite64 */
      regs->rax = (uint64_t)linux_preadwrite(
          (int)regs->rdi, regs->rsi, regs->rdx, (int64_t)regs->r10, 1);
      break;
    case 19: /* readv */
    case 20: /* writev */
      regs->rax = (uint64_t)linux_readv_writev((int)regs->rdi, regs->rsi,
                                               regs->rdx, regs->rax == 20);
      break;
    case 295: /* preadv: pos_l in R10, pos_h in R8 */
    case 296: /* pwritev */
    case 327: /* preadv2; flags are checked below */
    case 328: { /* pwritev2 */
      if ((regs->rax == 327 || regs->rax == 328) && regs->r9 != 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EOPNOTSUPP;
        break;
      }
      uint64_t raw_offset = ((uint64_t)(uint32_t)regs->r8 << 32) |
                            (uint32_t)regs->r10;
      if ((regs->rax == 327 || regs->rax == 328) &&
          raw_offset == UINT64_MAX) {
        /* Linux preadv2/pwritev2 use offset -1 for the shared file offset. */
        regs->rax = (uint64_t)linux_readv_writev(
            (int)regs->rdi, regs->rsi, regs->rdx,
            regs->rax == 328);
        break;
      }
      regs->rax = (uint64_t)linux_preadv_writev(
          (int)regs->rdi, regs->rsi, regs->rdx, (int64_t)raw_offset,
          regs->rax == 296 || regs->rax == 328);
      break;
    }
    case 0: /* read */
      if (regs->rdx > UINT32_MAX) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
      } else {
        int fd_rc = linux_validate_io_fd((int)regs->rdi, 0);
        if (fd_rc < 0) regs->rax = (uint64_t)(int64_t)fd_rc;
        else {
          int rc = sys_read((int)regs->rdi, (void *)regs->rsi,
                            (uint32_t)regs->rdx);
          regs->rax = (uint64_t)(int64_t)(rc == -1 ? -LINUX_EINTR : rc);
        }
      }
      break;
    case 1: /* write */
      if (regs->rdx > UINT32_MAX) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
      } else {
        int fd_rc = linux_validate_io_fd((int)regs->rdi, 1);
        if (fd_rc < 0) regs->rax = (uint64_t)(int64_t)fd_rc;
        else {
          int rc = sys_write((int)regs->rdi, (const void *)regs->rsi,
                             (uint32_t)regs->rdx);
          regs->rax = (uint64_t)(int64_t)(rc == -1 ? -LINUX_EINTR : rc);
        }
      }
      break;
    case 3: /* close */
      {
        int rc = sys_close((int)regs->rdi);
        regs->rax = rc == -1 ? (uint64_t)-(int64_t)LINUX_EBADF
                             : (uint64_t)(int64_t)rc;
      }
      break;
    case 32: { /* dup: share the generic LIWUS open-file description */
      int oldfd = (int)regs->rdi;
      if (oldfd < 0 || oldfd >= 32 ||
          current_task->process->file_descriptors[oldfd].type == 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EBADF;
        break;
      }
      int rc = sys_dup(oldfd);
      regs->rax = rc < 0 ? (uint64_t)-(int64_t)LINUX_EMFILE
                         : (uint64_t)(int64_t)rc;
      break;
    }
    case 33: { /* dup2: atomically replace the destination descriptor */
      int oldfd = (int)regs->rdi;
      int newfd = (int)regs->rsi;
      if (oldfd < 0 || oldfd >= 32 || newfd < 0 || newfd >= 32 ||
          current_task->process->file_descriptors[oldfd].type == 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EBADF;
        break;
      }
      int rc = sys_dup2(oldfd, newfd);
      regs->rax = rc < 0 ? (uint64_t)-(int64_t)LINUX_EBADF
                         : (uint64_t)(int64_t)rc;
      break;
    }
    case 292: { /* dup3 supports flags=0 or O_CLOEXEC */
      int oldfd = (int)regs->rdi;
      int newfd = (int)regs->rsi;
      if ((regs->rdx & ~LINUX_O_CLOEXEC) != 0 || oldfd == newfd) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      if (oldfd < 0 || oldfd >= 32 || newfd < 0 || newfd >= 32 ||
          current_task->process->file_descriptors[oldfd].type == 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EBADF;
        break;
      }
      int rc = sys_dup2_with_flags(oldfd, newfd,
          (regs->rdx & LINUX_O_CLOEXEC) ? KFILE_FLAG_CLOEXEC : 0);
      regs->rax = rc < 0 ? (uint64_t)-(int64_t)LINUX_EBADF
                         : (uint64_t)(int64_t)rc;
      break;
    }
    case 72: { /* fcntl: descriptor queries and lowest-free F_DUPFD subset */
      int fd = (int)regs->rdi;
      int command = (int)regs->rsi;
      int argument = (int)regs->rdx;
      if (fd < 0 || fd >= 32 ||
          current_task->process->file_descriptors[fd].type == 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EBADF;
        break;
      }
      if (command == LINUX_F_DUPFD) {
        if (argument < 0) {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
        }
        int duplicate = -1;
        for (int candidate = argument; candidate < 32; candidate++) {
          if (current_task->process->file_descriptors[candidate].type == 0) {
            duplicate = sys_dup2(fd, candidate);
            break;
          }
        }
        regs->rax = duplicate < 0 ? (uint64_t)-(int64_t)LINUX_EMFILE
                                 : (uint64_t)(int64_t)duplicate;
      } else if (command == LINUX_F_GETFD) {
        regs->rax = current_task->process->file_descriptors[fd].descriptor_flags &
                    LINUX_FD_CLOEXEC;
      } else if (command == LINUX_F_SETFD) {
        kfile_t *file = &current_task->process->file_descriptors[fd];
        file->descriptor_flags = (file->descriptor_flags &
                                  ~LINUX_FD_CLOEXEC) |
                                 ((uint32_t)argument & LINUX_FD_CLOEXEC);
        regs->rax = 0;
      } else if (command == LINUX_F_GETFL) {
        kfile_t *file = &current_task->process->file_descriptors[fd];
        uint64_t access = file->description ? file->description->access : 0;
        if (fd == 0 ||
            (file->type == LIWUS_FD_TYPE_PIPE && file->base_addr == 1))
          regs->rax = 0; /* O_RDONLY */
        else if (fd == 1 || fd == 2 ||
                 (file->type == LIWUS_FD_TYPE_PIPE &&
                  file->base_addr == 2) ||
                 (access == 2))
          regs->rax = LINUX_O_WRONLY;
        else if (access == 3)
          regs->rax = LINUX_O_RDWR;
        else
          regs->rax = 0;
        if (file->description &&
            (file->description->status_flags & KFILE_STATUS_APPEND))
          regs->rax |= LINUX_O_APPEND;
        if (file->description &&
            (file->description->status_flags & KFILE_STATUS_NONBLOCK))
          regs->rax |= LINUX_O_NONBLOCK;
        else if (file->type == LIWUS_FD_TYPE_PIPE && file->socket_ptr) {
          pipe_t *pipe = (pipe_t *)file->socket_ptr;
          uint64_t lock_flags = task_scheduler_lock();
          uint32_t status = file->base_addr == 1
              ? pipe->read_status_flags : pipe->write_status_flags;
          task_scheduler_unlock(lock_flags);
          if (status & KFILE_STATUS_NONBLOCK)
            regs->rax |= LINUX_O_NONBLOCK;
        }
      } else if (command == LINUX_F_SETFL) {
        /* musl adds O_LARGEFILE to F_SETFL on x86-64; it is an immutable
         * open flag, so accept and ignore it as Linux does. */
        const int supported = LINUX_O_ACCMODE | LINUX_O_APPEND |
                              LINUX_O_NONBLOCK | LINUX_O_LARGEFILE;
        if (argument & ~supported) {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
        }
        kfile_t *file = &current_task->process->file_descriptors[fd];
        if (file->type == LIWUS_FD_TYPE_PIPE) {
          pipe_t *pipe = (pipe_t *)file->socket_ptr;
          if (!pipe) {
            regs->rax = (uint64_t)-(int64_t)LINUX_EBADF;
          } else {
            uint64_t lock_flags = task_scheduler_lock();
            uint32_t *status = file->base_addr == 1
                ? &pipe->read_status_flags : &pipe->write_status_flags;
            if (argument & LINUX_O_NONBLOCK)
              *status |= KFILE_STATUS_NONBLOCK;
            else
              *status &= ~KFILE_STATUS_NONBLOCK;
            task_scheduler_unlock(lock_flags);
            regs->rax = 0; /* O_APPEND has no effect on a pipe. */
          }
        } else if (file->type == LIWUS_FD_TYPE_FILE && file->description) {
          if (argument & LINUX_O_APPEND)
            file->description->status_flags |= KFILE_STATUS_APPEND;
          else
            file->description->status_flags &= ~KFILE_STATUS_APPEND;
          if (argument & LINUX_O_NONBLOCK)
            file->description->status_flags |= KFILE_STATUS_NONBLOCK;
          else
            file->description->status_flags &= ~KFILE_STATUS_NONBLOCK;
          regs->rax = 0;
        } else {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        }
      } else if (command == LINUX_F_DUPFD_CLOEXEC) {
        if (argument < 0) {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
        }
        int duplicate = -1;
        for (int candidate = argument; candidate < 32; candidate++) {
          if (current_task->process->file_descriptors[candidate].type == 0) {
            duplicate = sys_dup2_with_flags(fd, candidate,
                                             KFILE_FLAG_CLOEXEC);
            break;
          }
        }
        regs->rax = duplicate < 0 ? (uint64_t)-(int64_t)LINUX_EMFILE
                                 : (uint64_t)(int64_t)duplicate;
      } else {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
      }
      break;
    }
    case 217: { /* getdents64 */
      int64_t result = linux_getdents64((int)regs->rdi,
                                        (void *)regs->rsi, regs->rdx);
      regs->rax = (uint64_t)result;
      break;
    }
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
    case 74: /* fsync */
      regs->rax = (uint64_t)(int64_t)sys_file_sync((int)regs->rdi, 0);
      break;
    case 75: /* fdatasync: filesystem may use the same durable flush */
      regs->rax = (uint64_t)(int64_t)sys_file_sync((int)regs->rdi, 1);
      break;
    case 306: /* syncfs: current VFS sync callbacks flush the whole backing FS */
      regs->rax = (uint64_t)(int64_t)sys_file_sync((int)regs->rdi, 0);
      break;
    case 8: { /* lseek */
      int64_t offset = (int64_t)regs->rsi;
      regs->rax = (uint64_t)linux_lseek((int)regs->rdi, offset,
                                        (int)regs->rdx);
      break;
    }
    case 257: { /* openat */
      int native_flags = 0;
      uint32_t descriptor_flags = 0;
      uint32_t status_flags = 0;
      int require_directory = 0;
      int rc = linux_translate_open_flags(regs->rdx, &native_flags,
                                          &descriptor_flags,
                                          &require_directory, &status_flags);
      if (rc < 0) {
        regs->rax = (uint64_t)(int64_t)rc;
        break;
      }
      int dirfd = (int)regs->rdi;
      rc = sys_openat_mode(dirfd, dirfd == LINUX_AT_FDCWD,
                               (const char *)regs->rsi, native_flags,
                               (uint32_t)regs->r10,
                               descriptor_flags, require_directory,
                               status_flags);
      if (rc == -2) rc = -LINUX_ENOENT; /* VFS missing-node sentinel */
      else if (rc == -1) rc = -LINUX_ENOMEM; /* legacy allocator/fallback */
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 258: { /* mkdirat: generic CWD or directory-fd path lookup */
      int dirfd = (int)regs->rdi;
      int rc = sys_mkdir_at(dirfd, dirfd == LINUX_AT_FDCWD,
                            (const char *)regs->rsi, (uint32_t)regs->rdx);
      regs->rax = rc == -1
          ? (uint64_t)-(int64_t)LINUX_ENOENT
          : (uint64_t)(int64_t)rc;
      break;
    }
    case 263: { /* unlinkat: flags=0 or AT_REMOVEDIR */
      if (regs->r10 != 0 && regs->r10 != LINUX_AT_REMOVEDIR) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      int dirfd = (int)regs->rdi;
      int rc = sys_unlink_at(dirfd, dirfd == LINUX_AT_FDCWD,
                             (const char *)regs->rsi,
                             regs->r10 == LINUX_AT_REMOVEDIR);
      regs->rax = rc == -1
          ? (uint64_t)-(int64_t)LINUX_ENOENT
          : (uint64_t)(int64_t)rc;
      break;
    }
    case 262: { /* newfstatat, flags limited to zero (no symlink support) */
      if (regs->r10 != 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      int dirfd = (int)regs->rdi;
      int rc = sys_stat_at(dirfd, dirfd == LINUX_AT_FDCWD,
                           (const char *)regs->rsi,
                           (struct kernel_stat *)regs->rdx);
      if (rc == -1) regs->rax = (uint64_t)-(int64_t)LINUX_ENOENT;
      else if (rc < 0) regs->rax = (uint64_t)(int64_t)rc;
      else regs->rax = (uint64_t)(int64_t)linux_copy_stat((void *)regs->rdx);
      break;
    }
    case 332: { /* statx: basic metadata; no symlinks or mount namespaces */
      int rc = linux_statx((int)regs->rdi, (const char *)regs->rsi,
                           (uint32_t)regs->rdx, regs->r8);
      regs->rax = (uint64_t)(int64_t)rc;
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
    case 10: { /* mprotect */
      const uint64_t prot_mask = 0x7;
      if (!regs->rsi || (regs->rdi & (PAGE_SIZE - 1)) ||
          (regs->rdx & ~prot_mask) ||
          ((regs->rdx & 0x6) == 0x6) ||
          regs->rsi > UINT64_MAX - (PAGE_SIZE - 1)) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      uint64_t length = (regs->rsi + PAGE_SIZE - 1) &
                        ~((uint64_t)PAGE_SIZE - 1);
      uint64_t start = regs->rdi;
      if (!is_user_addr((void *)start) || length > USER_ADDR_MAX - start) {
        regs->rax = (uint64_t)-(int64_t)LINUX_ENOMEM;
        break;
      }
      uint32_t vma_prot = 0;
      if (regs->rdx & 0x1) vma_prot |= VMA_PROT_READ;
      if (regs->rdx & 0x2) vma_prot |= VMA_PROT_WRITE;
      if (regs->rdx & 0x4) vma_prot |= VMA_PROT_EXEC;
      int rc = vma_protect(current_task->process->mm, start,
                           start + length, vma_prot);
      regs->rax = rc < 0 ? (uint64_t)-(int64_t)LINUX_ENOMEM : 0;
      break;
    }
    case 12: /* brk */
      regs->rax = linux_sys_brk(regs->rdi);
      break;
    case 39: /* getpid */
      regs->rax = (uint64_t)sys_getpid();
      break;
    case 79: { /* getcwd returns the byte count including the trailing NUL */
      if (!regs->rdi) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      if (!regs->rsi) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      const char *cwd = current_task->process->cwd;
      size_t length = strlen(cwd) + 1;
      if (length > regs->rsi) {
        regs->rax = (uint64_t)-(int64_t)LINUX_ERANGE;
        break;
      }
      if (copy_to_user((void *)regs->rdi, cwd, length) < 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      regs->rax = length;
      break;
    }
    case 80: { /* chdir uses the generic process CWD and VFS permissions */
      int rc = sys_chdir((const char *)regs->rdi);
      /* sys_chdir's legacy path-resolution failure is -1; translate it to
       * the Linux result for a missing path. VFS permission errors already
       * use the shared negative errno values. */
      regs->rax = rc == -1
          ? (uint64_t)-(int64_t)LINUX_ENOENT
          : (uint64_t)(int64_t)rc;
      break;
    }
    case 81: { /* fchdir uses the path retained by the generic open description */
      int rc = sys_fchdir((int)regs->rdi);
      regs->rax = rc == -1
          ? (uint64_t)-(int64_t)LINUX_EBADF
          : (uint64_t)(int64_t)rc;
      break;
    }
    case 309: { /* getcpu: LIWUS exposes the current scheduler CPU, one NUMA node */
      uint32_t cpu = (uint32_t)get_cpu_id();
      uint32_t node = 0;
      if (regs->rdi && copy_to_user((void *)regs->rdi, &cpu, sizeof(cpu)) < 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      if (regs->rsi && copy_to_user((void *)regs->rsi, &node, sizeof(node)) < 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      regs->rax = 0;
      break;
    }
    case 110: /* getppid */
      regs->rax = current_task && current_task->process &&
                  current_task->process->parent
          ? (uint64_t)current_task->process->parent->pid : 0;
      break;
    case 204: { /* sched_getaffinity: user tasks currently run on CPU 0 only */
      int32_t pid = (int32_t)regs->rdi;
      if (pid < 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      if (pid > 0 && pid != current_task->id &&
          pid != current_task->process->pid) {
        regs->rax = (uint64_t)-(int64_t)LINUX_ESRCH;
        break;
      }
      if (regs->rsi < sizeof(uint64_t)) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      if (!regs->rdx || validate_user_write((void *)regs->rdx,
                                            sizeof(uint64_t)) < 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      const uint64_t allowed_cpus = 1;
      if (copy_to_user((void *)regs->rdx, &allowed_cpus,
                       sizeof(allowed_cpus)) < 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      regs->rax = sizeof(uint64_t);
      break;
    }
    case 221: { /* fadvise64: VFS has no page cache, valid advice is a no-op */
      regs->rax = (uint64_t)(int64_t)sys_file_advice(
          (int)regs->rdi, (int64_t)regs->rsi, (int64_t)regs->rdx,
          (int)regs->r10);
      break;
    }
    case 102: /* getuid */
      regs->rax = current_task && current_task->process
          ? current_task->process->uid : 0;
      break;
    case 104: /* getgid */
      regs->rax = current_task && current_task->process
          ? current_task->process->gid : 0;
      break;
    case 107: /* geteuid */
      regs->rax = current_task && current_task->process
          ? current_task->process->euid : 0;
      break;
    case 108: /* getegid */
      regs->rax = current_task && current_task->process
          ? current_task->process->egid : 0;
      break;
    case 115: /* getgroups: LIWUS currently provisions no supplementary groups */
      if ((int64_t)regs->rdi < 0)
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
      else
        regs->rax = 0;
      break;
    case 118: /* getresuid: nullable output pointers follow Linux ABI */
    case 120: { /* getresgid */
      process_t *process = current_task->process;
      uint32_t real_id = regs->rax == 118 ? process->uid : process->gid;
      uint32_t effective_id = regs->rax == 118 ? process->euid : process->egid;
      uint32_t *outputs[3] = {
        (uint32_t *)(uintptr_t)regs->rdi,
        (uint32_t *)(uintptr_t)regs->rsi,
        (uint32_t *)(uintptr_t)regs->rdx
      };
      uint32_t ids[3] = { real_id, effective_id, effective_id };
      int rc = 0;
      for (size_t i = 0; i < 3; i++) {
        if (outputs[i] && copy_to_user(outputs[i], &ids[i], sizeof(ids[i])) < 0) {
          rc = -LINUX_EFAULT;
          break;
        }
      }
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 95: { /* umask: generic process state, limited to permission bits */
      process_t *process = current_task->process;
      uint32_t old_mask = process->umask & 0777U;
      process->umask = (uint32_t)regs->rdi & 0777U;
      regs->rax = old_mask;
      break;
    }
    case 97: { /* getrlimit: report only the enforced descriptor limit */
      int rc = linux_getrlimit(regs->rdi, regs->rsi);
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 302: { /* prlimit64 query for this process; no limit changes yet */
      int pid = (int)regs->rdi;
      if (pid != 0 && pid != current_task->process->pid) {
        regs->rax = (uint64_t)-(int64_t)LINUX_ESRCH;
        break;
      }
      if (regs->rdx != 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_ENOSYS;
        break;
      }
      int rc;
      if (regs->r10 == 0) {
        rc = regs->rsi >= LINUX_RLIMIT_NLIMITS ? -LINUX_EINVAL
            : regs->rsi == LINUX_RLIMIT_NOFILE ? 0 : -LINUX_ENOSYS;
      } else {
        rc = linux_getrlimit(regs->rsi, regs->r10);
      }
      regs->rax = (uint64_t)(int64_t)rc;
      break;
    }
    case 63: { /* uname: report LIWUS identity in Linux's structure layout */
      linux_utsname_t name;
      memset(&name, 0, sizeof(name));
      /* Match the Linux personality's expected sysname while making the
       * actual implementation identity explicit in release/version. */
      linux_utsname_set(name.sysname, "Linux");
      linux_utsname_set(name.nodename, "liwus");
      linux_utsname_set(name.release, "LiwusOS-2.0");
      linux_utsname_set(name.version, "LIWUS x86_64");
      linux_utsname_set(name.machine, "x86_64");
      linux_utsname_set(name.domainname, "(none)");
      regs->rax = copy_to_user((void *)regs->rdi, &name, sizeof(name)) < 0
          ? (uint64_t)-(int64_t)LINUX_EFAULT : 0;
      break;
    }
    case 96: { /* gettimeofday: Linux timeval layout */
      if (!regs->rdi) {
        regs->rax = 0;
        break;
      }
      uint64_t ticks = timer_monotonic_ticks;
      linux_timeval64_t value = {
        .tv_sec = (int64_t)time(NULL),
        .tv_usec = (int64_t)((ticks % 100ULL) * 10000ULL)
      };
      regs->rax = copy_to_user((void *)regs->rdi, &value, sizeof(value)) < 0
          ? (uint64_t)-(int64_t)LINUX_EFAULT : 0;
      break;
    }
    case 99: { /* sysinfo: translate generic LIWUS memory/task accounting */
      if (!regs->rdi) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      linux_sysinfo64_t info;
      memset(&info, 0, sizeof(info));
      info.uptime = (int64_t)(timer_monotonic_ticks / 100ULL);
      info.totalram = pmm_get_total_memory();
      info.freeram = pmm_get_free_memory();
      info.mem_unit = 1;

      uint32_t processes = task_process_count_active();
      if (processes > UINT16_MAX) processes = UINT16_MAX;
      info.procs = (uint16_t)processes;
      regs->rax = copy_to_user((void *)regs->rdi, &info, sizeof(info)) < 0
          ? (uint64_t)-(int64_t)LINUX_EFAULT : 0;
      break;
    }
    case 201: { /* time: Linux x86-64 time_t is signed 64-bit */
      int64_t seconds = (int64_t)time(NULL);
      if (regs->rdi &&
          copy_to_user((void *)regs->rdi, &seconds, sizeof(seconds)) < 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      regs->rax = (uint64_t)seconds;
      break;
    }
    case 228: { /* clock_gettime: realtime and monotonic clocks */
      int clock_id = (int)regs->rdi;
      if (clock_id != 0 && clock_id != 1) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      if (!regs->rsi) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EFAULT;
        break;
      }
      uint64_t ticks = timer_monotonic_ticks;
      linux_timespec64_t value;
      if (clock_id == 0) { /* CLOCK_REALTIME */
        value.tv_sec = (int64_t)time(NULL);
        value.tv_nsec = (int64_t)((ticks % 100ULL) * 10000000ULL);
      } else { /* CLOCK_MONOTONIC */
        value.tv_sec = (int64_t)(ticks / 100ULL);
        value.tv_nsec = (int64_t)((ticks % 100ULL) * 10000000ULL);
      }
      regs->rax = copy_to_user((void *)regs->rsi, &value, sizeof(value)) < 0
          ? (uint64_t)-(int64_t)LINUX_EFAULT : 0;
      break;
    }
    case 229: { /* clock_getres: the generic timer currently ticks at 100 Hz */
      int clock_id = (int)regs->rdi;
      if (clock_id != 0 && clock_id != 1) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      if (!regs->rsi) {
        regs->rax = 0;
        break;
      }
      linux_timespec64_t resolution = { .tv_sec = 0, .tv_nsec = 10000000 };
      regs->rax = copy_to_user((void *)regs->rsi, &resolution,
                               sizeof(resolution)) < 0
          ? (uint64_t)-(int64_t)LINUX_EFAULT : 0;
      break;
    }
    case 230: { /* clock_nanosleep: only realtime/monotonic, no signals yet */
      int clock_id = (int)regs->rdi;
      uint64_t flags = regs->rsi;
      if ((clock_id != 0 && clock_id != 1) ||
          (flags & ~LINUX_TIMER_ABSTIME) != 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }

      uint32_t milliseconds = 0;
      int rc;
      if (flags & LINUX_TIMER_ABSTIME) {
        uint32_t ticks = 0;
        rc = linux_futex_absolute_timeout_ticks(regs->rdx, clock_id == 0,
                                                &ticks);
        /* An absolute sleep whose deadline has passed completes immediately;
         * the same condition is ETIMEDOUT for FUTEX_WAIT_BITSET. */
        if (rc == -LINUX_ETIMEDOUT) rc = 0;
        if (rc == 0) {
          uint64_t duration_ms = (uint64_t)ticks * 10ULL;
          if (duration_ms > UINT32_MAX)
            rc = -LINUX_EINVAL;
          else
            milliseconds = (uint32_t)duration_ms;
        }
      } else {
        rc = linux_nanosleep_milliseconds(regs->rdx, &milliseconds);
      }

      if (rc < 0) {
        regs->rax = (uint64_t)(int64_t)rc;
        break;
      }
      if (milliseconds) task_sleep_ms(milliseconds);
      /* LIWUS has no Linux signal delivery yet; rem is not modified. */
      regs->rax = 0;
      break;
    }
    case 24: /* sched_yield */
      task_sleep_ms(0);
      regs->rax = 0;
      break;
    case 35: { /* nanosleep: relative timespec; no signal interruption yet */
      uint32_t milliseconds = 0;
      int rc = linux_nanosleep_milliseconds(regs->rdi, &milliseconds);
      if (rc < 0) {
        regs->rax = (uint64_t)(int64_t)rc;
        break;
      }
      if (milliseconds) task_sleep_ms(milliseconds);
      /* There is no signal delivery in this personality yet, so rem stays
       * untouched and a valid request always completes normally. */
      regs->rax = 0;
      break;
    }
    case 56: { /* clone: initial support for shared-process threads */
      uint64_t flags = regs->rdi;
      uint64_t supported = LINUX_CLONE_REQUIRED | LINUX_CLONE_SYSVSEM |
          LINUX_CLONE_SETTLS | LINUX_CLONE_PARENT_SETTID |
          LINUX_CLONE_CHILD_SETTID | LINUX_CLONE_CHILD_CLEARTID |
          LINUX_CLONE_DETACHED;
      if ((flags & LINUX_CLONE_REQUIRED) != LINUX_CLONE_REQUIRED ||
          (flags & ~supported) != 0 ||
          (!(flags & LINUX_CLONE_PARENT_SETTID) && regs->rdx != 0) ||
          (!(flags & (LINUX_CLONE_CHILD_SETTID |
                      LINUX_CLONE_CHILD_CLEARTID)) && regs->r10 != 0)) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      int tid = task_clone_thread(
          regs, regs->rsi, regs->r8,
          (flags & LINUX_CLONE_SETTLS) != 0,
          regs->rdx, (flags & LINUX_CLONE_PARENT_SETTID) != 0,
          regs->r10, (flags & LINUX_CLONE_CHILD_SETTID) != 0,
          (flags & LINUX_CLONE_CHILD_CLEARTID) != 0);
      regs->rax = tid < 0 ? (uint64_t)(int64_t)tid : (uint64_t)tid;
      break;
    }
    case 59: { /* execve: shared ELF loader, preserve Linux process personality */
      int rc = sys_execve_linux(regs, (const char *)regs->rdi,
                                (char *const *)regs->rsi,
                                (char *const *)regs->rdx);
      regs->rax = rc == -1
          ? (uint64_t)-(int64_t)LINUX_ENOENT
          : (uint64_t)(int64_t)rc;
      break;
    }
    case 57: { /* fork: copy-on-fork path provided by the generic process core */
      int pid = fork_process(regs);
      regs->rax = pid < 0 ? (uint64_t)(int64_t)pid : (uint64_t)pid;
      break;
    }
    case 61: /* wait4: WNOHANG, encoded exit status; no process groups/signals */
      regs->rax = (uint64_t)linux_wait4((int)regs->rdi,
                                        (void *)regs->rsi,
                                        (uint32_t)regs->rdx,
                                        (void *)regs->r10);
      break;
    case 186: /* gettid */
      regs->rax = current_task ? (uint64_t)current_task->id : 0;
      break;
    case 218: { /* set_tid_address: generic per-thread clear_child_tid */
      int tid = task_set_clear_child_tid(regs->rdi);
      regs->rax = (uint64_t)(int64_t)tid;
      break;
    }
    case 202: { /* futex operations translate to generic private wait queues */
      uint64_t operation = regs->rsi;
      uint64_t allowed = LINUX_FUTEX_PRIVATE_FLAG |
                         LINUX_FUTEX_CLOCK_REALTIME | 0x7f;
      uint32_t command = (uint32_t)(operation & 0x7f);
      if ((operation & ~allowed) != 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      if ((operation & LINUX_FUTEX_CLOCK_REALTIME) != 0 &&
          command != LINUX_FUTEX_WAIT_BITSET) {
        regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
        break;
      }
      if ((operation & LINUX_FUTEX_PRIVATE_FLAG) == 0) {
        regs->rax = (uint64_t)-(int64_t)LINUX_ENOSYS;
        break;
      }
      if (command == LINUX_FUTEX_WAIT) {
        if (((uintptr_t)regs->rdi & 3U) != 0) {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
        }
        int rc;
        if (regs->r10 != 0) {
          uint32_t timeout_ticks = 0;
          rc = linux_futex_timeout_ticks(regs->r10, &timeout_ticks);
          if (rc < 0) {
            regs->rax = (uint64_t)(int64_t)rc;
            break;
          }
          rc = task_futex_wait_private_timed((uint32_t *)regs->rdi,
                                             (uint32_t)regs->rdx,
                                             timeout_ticks);
        } else {
          rc = task_futex_wait_private((uint32_t *)regs->rdi,
                                       (uint32_t)regs->rdx);
        }
        if (rc == TASK_WAIT_RESULT_TIMEOUT) rc = -LINUX_ETIMEDOUT;
        regs->rax = (uint64_t)(int64_t)rc;
      } else if (command == LINUX_FUTEX_WAKE) {
        if (((uintptr_t)regs->rdi & 3U) != 0 || regs->rdx > 0x7fffffffULL) {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
        }
        int rc = task_futex_wake_private((uint32_t *)regs->rdi,
                                         (uint32_t)regs->rdx);
        regs->rax = (uint64_t)(int64_t)rc;
      } else if (command == LINUX_FUTEX_WAIT_BITSET) {
        if (((uintptr_t)regs->rdi & 3U) != 0 || regs->r9 == 0) {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
        }
        int rc = 0;
        bool timed = regs->r10 != 0;
        uint32_t timeout_ticks = 0;
        if (timed) {
          rc = linux_futex_absolute_timeout_ticks(
              regs->r10,
              (operation & LINUX_FUTEX_CLOCK_REALTIME) != 0,
              &timeout_ticks);
          if (rc < 0) {
            regs->rax = (uint64_t)(int64_t)rc;
            break;
          }
        }
        rc = task_futex_wait_private_bitset(
            (uint32_t *)regs->rdi, (uint32_t)regs->rdx, timeout_ticks, timed,
            (uint32_t)regs->r9);
        if (rc == TASK_WAIT_RESULT_TIMEOUT) rc = -LINUX_ETIMEDOUT;
        regs->rax = (uint64_t)(int64_t)rc;
      } else if (command == LINUX_FUTEX_WAKE_BITSET) {
        if (((uintptr_t)regs->rdi & 3U) != 0 ||
            regs->rdx > 0x7fffffffULL || regs->r9 == 0) {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
        }
        int rc = task_futex_wake_private_bitset(
            (uint32_t *)regs->rdi, (uint32_t)regs->rdx, (uint32_t)regs->r9);
        regs->rax = (uint64_t)(int64_t)rc;
      } else if (command == LINUX_FUTEX_REQUEUE ||
                 command == LINUX_FUTEX_CMP_REQUEUE) {
        if (((uintptr_t)regs->rdi & 3U) != 0 ||
            ((uintptr_t)regs->r8 & 3U) != 0 ||
            regs->rdx > 0x7fffffffULL || regs->r10 > 0x7fffffffULL) {
          regs->rax = (uint64_t)-(int64_t)LINUX_EINVAL;
          break;
        }
        int rc = task_futex_requeue_private(
            (uint32_t *)regs->rdi, (uint32_t *)regs->r8,
            (uint32_t)regs->rdx, (uint32_t)regs->r10,
            command == LINUX_FUTEX_CMP_REQUEUE, (uint32_t)regs->r9);
        regs->rax = (uint64_t)(int64_t)rc;
      } else {
        regs->rax = (uint64_t)-(int64_t)LINUX_ENOSYS;
      }
      break;
    }
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
    case 60: /* exit: terminate this thread */
      sys_exit_thread((int)(regs->rdi & 0xffU));
      break;
    case 231: /* exit_group: terminate all threads in this process */
      sys_exit_process((int)(regs->rdi & 0xffU));
      break;
    default:
      regs->rax = (uint64_t)-(int64_t)LINUX_ENOSYS;
      break;
  }
}
