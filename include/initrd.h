#ifndef INITRD_H
#define INITRD_H

#include <stdint.h>

/* Estrutura do cabeçalho TAR simples */
struct tar_header {
    char filename[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
} __attribute__((packed));

#include "vfs.h"

typedef void (*copy_progress_cb)(int percent, const char *filename);

fs_node_t* init_initrd(uint32_t location, uint32_t size);
void* initrd_get_file(const char* name, uint32_t* size);
char* initrd_list_files(int index);
void initrd_copy_to_sdfs(copy_progress_cb cb);

#endif
