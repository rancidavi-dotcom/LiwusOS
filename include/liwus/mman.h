#ifndef LIWUS_MMAN_H
#define LIWUS_MMAN_H

#define LIW_PROT_READ  0x1
#define LIW_PROT_WRITE 0x2
#define LIW_PROT_EXEC  0x4

#define LIW_MAP_PRIVATE   0x02
#define LIW_MAP_FIXED     0x10
#define LIW_MAP_ANONYMOUS 0x20
#define LIW_MAP_FAILED ((void *)(long)-1)

#endif
