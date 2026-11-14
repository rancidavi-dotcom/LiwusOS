#ifndef LIWUS_MMAN_H
#define LIWUS_MMAN_H

#include <stddef.h>
#include <stdint.h>

#define LIW_PROT_NONE  0x0
#define LIW_PROT_READ  0x1
#define LIW_PROT_WRITE 0x2
#define LIW_PROT_EXEC  0x4

#define LIW_MAP_SHARED    0x01
#define LIW_MAP_PRIVATE   0x02
#define LIW_MAP_FIXED     0x10
#define LIW_MAP_ANONYMOUS 0x20
#define LIW_MAP_FAILED ((void *)(long)-1)

#define PROT_NONE  LIW_PROT_NONE
#define PROT_READ  LIW_PROT_READ
#define PROT_WRITE LIW_PROT_WRITE
#define PROT_EXEC  LIW_PROT_EXEC

#define MAP_SHARED    LIW_MAP_SHARED
#define MAP_PRIVATE   LIW_MAP_PRIVATE
#define MAP_FIXED     LIW_MAP_FIXED
#define MAP_ANONYMOUS LIW_MAP_ANONYMOUS
#define MAP_ANON      LIW_MAP_ANONYMOUS
#define MAP_FAILED    LIW_MAP_FAILED

#ifndef _OFF_T_DEFINED
#define _OFF_T_DEFINED
typedef int64_t off_t;
#endif

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t len, int prot);

#endif
