#ifndef _SYS_STATFS_H_
#define _SYS_STATFS_H_

#include <sys/types.h>

struct statfs {
    long f_type;
    long f_bsize;
    unsigned long f_blocks;
    unsigned long f_bfree;
    unsigned long f_bavail;
    unsigned long f_files;
    unsigned long f_ffree;
    struct { int val[2]; } f_fsid;
    long f_namelen;
    long f_frsize;
    long f_flags;
    long f_spare[4];
};

int statfs(const char *, struct statfs *);
int fstatfs(int, struct statfs *);

#endif
