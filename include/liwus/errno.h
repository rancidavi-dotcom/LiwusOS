#ifndef LIWUS_ERRNO_H
#define LIWUS_ERRNO_H

/* Native syscall errors returned as negative values where implemented. */
#define LIW_EPERM    1
#define LIW_ENOENT   2
#define LIW_EIO      5
#define LIW_ENOEXEC  8
#define LIW_EBADF    9
#define LIW_ENOMEM  12
#define LIW_EACCES  13
#define LIW_EFAULT  14
#define LIW_EBUSY   16
#define LIW_EEXIST  17
#define LIW_ENOTDIR 20
#define LIW_EISDIR  21
#define LIW_EINVAL  22
#define LIW_ENOSPC  28
#define LIW_EPIPE   32

#endif
