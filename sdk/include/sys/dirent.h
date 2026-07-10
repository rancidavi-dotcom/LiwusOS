#ifndef _SYS_DIRENT_H
#define _SYS_DIRENT_H

#include <sys/types.h>

#define MAXNAMLEN 255

struct dirent {
    ino_t d_ino;
    off_t d_off;
    unsigned short d_reclen;
    unsigned char d_type;
    char d_name[MAXNAMLEN + 1];
};

#define DT_UNKNOWN 0
#define DT_DIR     4
#define DT_REG     8

typedef struct {
    int dd_fd;
    int dd_index;
    struct dirent dd_entry;
} DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dirp);
int closedir(DIR *dirp);
void rewinddir(DIR *dirp);
int getdents(int fd, void *buf, unsigned int count);

#endif
