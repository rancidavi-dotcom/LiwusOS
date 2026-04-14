#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02

struct fs_node;
struct dirent {
    char name[128];
    uint32_t ino;
};

typedef uint32_t (*read_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef uint32_t (*write_type_t)(struct fs_node*, uint32_t, uint32_t, uint8_t*);
typedef void (*open_type_t)(struct fs_node*);
typedef void (*close_type_t)(struct fs_node*);
typedef struct dirent* (*readdir_type_t)(struct fs_node*, uint32_t);
typedef struct fs_node* (*finddir_type_t)(struct fs_node*, const char* name);

typedef struct fs_node {
    char name[128];
    uint32_t mask;
    uint32_t uid;
    uint32_t gid;
    uint32_t flags;
    uint32_t inode;
    uint32_t length;
    read_type_t read;
    write_type_t write;
    open_type_t open;
    close_type_t close;
    readdir_type_t readdir;
    finddir_type_t finddir;
    struct fs_node* ptr; /* Para diretórios */
} fs_node_t;

typedef struct vfs_mount {
    char path[128];
    fs_node_t* root;
    struct vfs_mount* next;
} vfs_mount_t;

extern fs_node_t* fs_root;

void vfs_init();
int vfs_mount(const char* path, fs_node_t* root);
fs_node_t* vfs_open(const char* path);

struct dirent *readdir_fs(fs_node_t *node, uint32_t index);
fs_node_t *finddir_fs(fs_node_t *node, const char *name);

uint32_t read_fs(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);
uint32_t write_fs(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer);

#endif
