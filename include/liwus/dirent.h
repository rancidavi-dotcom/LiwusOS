#ifndef LIWUS_DIRENT_H
#define LIWUS_DIRENT_H

#include <stdint.h>

/* Fixed native SYS_GETDENTS record. d_off is the next directory index. */
struct liw_dirent {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[256];
};

#define LIW_DT_UNKNOWN 0
#define LIW_DT_DIR     4
#define LIW_DT_REG     8

#endif
