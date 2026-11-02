#ifndef LIWUS_STAT_H
#define LIWUS_STAT_H

#include <stdint.h>

/* Native Liwus stat payload (matches SYS_STAT/SYS_FSTAT, not Linux struct stat). */
struct liw_stat {
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

struct liw_timeval {
    int64_t tv_sec;
    int64_t tv_usec;
};

#endif
