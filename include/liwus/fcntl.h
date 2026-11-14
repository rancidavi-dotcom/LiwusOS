#ifndef LIWUS_FCNTL_H
#define LIWUS_FCNTL_H

/* Native LIWUS open flags; access mode values are frozen by the LIWUS ABI. */
#define LIW_O_RDONLY 0x0000
#define LIW_O_WRONLY 0x0001
#define LIW_O_RDWR   0x0002
#define LIW_O_ACCMODE 0x0003
#define LIW_O_CREAT  0x0200
#define LIW_O_TRUNC  0x0400
#define LIW_O_APPEND 0x0008
#define LIW_O_EXCL   0x0800

#define O_RDONLY  LIW_O_RDONLY
#define O_WRONLY  LIW_O_WRONLY
#define O_RDWR    LIW_O_RDWR
#define O_ACCMODE LIW_O_ACCMODE
#define O_CREAT   LIW_O_CREAT
#define O_TRUNC   LIW_O_TRUNC
#define O_APPEND  LIW_O_APPEND
#define O_EXCL    LIW_O_EXCL
#define O_BINARY  0

int open(const char *pathname, int flags, ...);

#endif
