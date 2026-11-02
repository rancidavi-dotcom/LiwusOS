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

#endif
