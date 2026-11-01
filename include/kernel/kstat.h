#ifndef LIWUS_KERNEL_KSTAT_H
#define LIWUS_KERNEL_KSTAT_H

#include <stdint.h>

/* Native kernel stat payload. User ABI layers must translate this layout. */
struct kernel_stat {
  uint64_t st_dev;
  uint64_t st_ino;
  uint32_t st_mode;
  uint64_t st_nlink;
  uint32_t st_uid;
  uint32_t st_gid;
  uint64_t st_rdev;
  uint64_t st_size;
  int64_t st_atime;
  int64_t st_atimensec;
  int64_t st_mtime;
  int64_t st_mtimensec;
  int64_t st_ctime;
  int64_t st_ctimensec;
  int64_t st_blksize;
  int64_t st_blocks;
  int64_t st_spare4[2];
};

#endif
