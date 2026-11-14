#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "serial.h"

#define VFS_EACCES 13
#define VFS_ENOTDIR 20

fs_node_t* fs_root = NULL;
static vfs_mount_t* mounts = NULL;
static uint64_t live_owned_nodes = 0;

void vfs_init() {
    mounts = NULL;
}

int vfs_mount(const char* path, fs_node_t* root) {
    if (!path || !root) return -1;

    vfs_mount_t* m = (vfs_mount_t*)kmalloc(sizeof(vfs_mount_t));
    strcpy(m->path, path);
    m->root = root;
    m->next = mounts;
    mounts = m;

    serial_print("[vfs] mounted ");
    serial_print(root->name);
    serial_print(" at ");
    serial_print(path);
    serial_print("\n");

    return 0;
}

struct dirent *readdir_fs(fs_node_t *node, uint32_t index) {
  if (node && (node->flags & FS_DIRECTORY) && node->readdir) {
    return node->readdir(node, index);
  }
  return NULL;
}

fs_node_t *finddir_fs(fs_node_t *node, const char *name) {
  if (node && (node->flags & FS_DIRECTORY) && node->finddir) {
    return node->finddir(node, name);
  }
  return NULL;
}

int vfs_truncate(fs_node_t *node) {
    if (!node || !node->truncate) return -1;
    return node->truncate(node);
}

/* Mounted roots are borrowed. Current filesystem finddir callbacks allocate
 * descendants, which the lookup caller releases with this function. */
void vfs_node_put(fs_node_t *node) {
    if (!node || !node->vfs_owned) return;
    node->vfs_owned = 0;
    if (live_owned_nodes) live_owned_nodes--;
    if (node->close) node->close(node);
    kfree(node);
}

void vfs_node_take_ownership(fs_node_t *node) {
    if (!node || node->vfs_owned) return;
    node->vfs_owned = 1;
    live_owned_nodes++;
}

uint64_t vfs_live_owned_nodes(void) {
    return live_owned_nodes;
}

static vfs_mount_t* vfs_find_mount(const char* path, const char** remaining_out) {
    if (!path || !path[0]) return NULL;

    vfs_mount_t* best_match = NULL;
    int max_len = -1;

    for (vfs_mount_t* it = mounts; it; it = it->next) {
        int m_len = strlen(it->path);
        if (strncmp(path, it->path, m_len) == 0) {
            /* Delimitador valido: barra '/', dois pontos ':', ou fim de caminho */
            if (it->path[m_len - 1] == '/' || it->path[m_len - 1] == ':' ||
                path[m_len] == '\0' || path[m_len] == '/' || path[m_len] == ':') {
                if (m_len > max_len) {
                    max_len = m_len;
                    best_match = it;
                }
            }
        }
    }

    if (!best_match) {
        for (vfs_mount_t* it = mounts; it; it = it->next) {
            if (strcmp(it->path, "/") == 0) {
                best_match = it;
                max_len = 1;
                break;
            }
        }
    }

    if (!best_match) return NULL;

    const char* remaining;
    if (strcmp(best_match->path, "/") == 0) {
        remaining = (path[0] == '/') ? path + 1 : path;
    } else {
        remaining = path + max_len;
        if (*remaining == '/' || *remaining == ':') remaining++;
    }

    if (remaining_out) *remaining_out = remaining;
    return best_match;
}

fs_node_t* vfs_open(const char* path) {
    if (!path) return NULL;
    if (path[0] == '\0') return fs_root;
    if (strlen(path) >= 512) return NULL;

    const char* remaining = NULL;
    vfs_mount_t* best_match = vfs_find_mount(path, &remaining);
    if (!best_match) return NULL;

    fs_node_t* current = best_match->root;
    int current_owned = 0;

    if (!remaining || *remaining == '\0') return current;

    char part[128];
    while (*remaining) {
        int i = 0;
        while (*remaining && *remaining != '/' && *remaining != ':') {
            if (i >= (int)sizeof(part) - 1) {
                if (current_owned) vfs_node_put(current);
                return NULL;
            }
            part[i++] = *remaining++;
        }
        part[i] = '\0';
        if (*remaining == '/' || *remaining == ':') remaining++;

        fs_node_t* next_node = finddir_fs(current, part);
        if (!next_node) {
            if (current_owned) vfs_node_put(current);
            return NULL;
        }
        if (current_owned) vfs_node_put(current);
        vfs_node_take_ownership(next_node);
        current = next_node;
        current_owned = 1;
    }

    return current;
}

int vfs_node_access(const fs_node_t *node, uint32_t uid, uint32_t gid,
                    uint32_t requested) {
    if (!node) return -1;
    if (uid == 0) return 0; /* uid 0 is reserved for kernel-created contexts. */
    uint32_t mode = node->mask & 0777u;
    uint32_t selected;
    if (uid == node->uid) selected = (mode >> 6) & 7u;
    else if (gid == node->gid) selected = (mode >> 3) & 7u;
    else selected = mode & 7u;
    uint32_t mode_requested = 0;
    if (requested & VFS_ACCESS_READ) mode_requested |= 4u;
    if (requested & VFS_ACCESS_WRITE) mode_requested |= 2u;
    if (requested & VFS_ACCESS_EXEC) mode_requested |= 1u;
    return (selected & mode_requested) == mode_requested ? 0 : -VFS_EACCES;
}

int vfs_open_access(const char *path, uint32_t uid, uint32_t gid,
                    uint32_t requested, fs_node_t **out) {
    if (!path || !out) return -1;
    *out = NULL;
    size_t length = strlen(path);
    if (length == 0 || length >= 512) return -1;

    const char *remaining = NULL;
    vfs_mount_t *best_match = vfs_find_mount(path, &remaining);
    if (!best_match) return -2; /* ENOENT */

    fs_node_t *current = best_match->root;
    int current_owned = 0;
    if (!current) return -2;

    char part[128];
    while (remaining && *remaining) {
        if (!(current->flags & FS_DIRECTORY)) {
            if (current_owned) vfs_node_put(current);
            return -VFS_ENOTDIR;
        }
        int dir_rc = vfs_node_access(current, uid, gid, VFS_ACCESS_EXEC);
        if (dir_rc < 0) {
            if (current_owned) vfs_node_put(current);
            return dir_rc;
        }

        int i = 0;
        while (*remaining && *remaining != '/' && *remaining != ':') {
            if (i >= (int)sizeof(part) - 1) {
                if (current_owned) vfs_node_put(current);
                return -1;
            }
            part[i++] = *remaining++;
        }
        part[i] = '\0';
        if (*remaining == '/' || *remaining == ':') remaining++;
        if (part[0] == '\0') continue;

        fs_node_t *next_node = finddir_fs(current, part);
        if (current_owned) vfs_node_put(current);
        if (!next_node) return -2; /* ENOENT */
        vfs_node_take_ownership(next_node);
        current = next_node;
        current_owned = 1;
    }

    int rc = vfs_node_access(current, uid, gid, requested);
    if (rc < 0) {
        if (current_owned) vfs_node_put(current);
        return rc;
    }
    *out = current;
    return 0;
}

uint32_t read_fs(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node && node->read) {
        return node->read(node, offset, size, buffer);
    }
    return 0;
}

uint32_t write_fs(fs_node_t* node, uint32_t offset, uint32_t size, uint8_t* buffer) {
    if (node && node->write) {
        return node->write(node, offset, size, buffer);
    }
    return 0;
}

fs_node_t* vfs_create(const char *path, uint32_t flags) {
    return vfs_create_as(path, flags, 0, 0, 0644);
}

fs_node_t* vfs_create_as(const char* path, uint32_t flags, uint16_t uid,
                         uint16_t gid, uint16_t mode) {
    if (!path) return NULL;
    if (path[0] == '\0') return NULL; // Can't create root
    if (strlen(path) >= 512) return NULL;

    const char* remaining = NULL;
    vfs_mount_t* best_match = vfs_find_mount(path, &remaining);
    if (!best_match || !remaining || *remaining == '\0') return NULL;

    fs_node_t* current = best_match->root;
    int current_owned = 0;

    char part[128];
    while (*remaining) {
        int i = 0;
        while (*remaining && *remaining != '/' && *remaining != ':') {
            if (i >= (int)sizeof(part) - 1) {
                if (current_owned) vfs_node_put(current);
                return NULL;
            }
            part[i++] = *remaining++;
        }
        part[i] = '\0';
        
        if (*remaining == '/' || *remaining == ':') {
            remaining++;
            // Not the last part, must be a directory
            fs_node_t* next_node = finddir_fs(current, part);
            if (!next_node) {
                if (current_owned) vfs_node_put(current);
                return NULL;
            }
            if (current_owned) vfs_node_put(current);
            vfs_node_take_ownership(next_node);
            current = next_node;
            current_owned = 1;
        } else {
            // Last part, this is the file to create
            if (current && (current->flags & FS_DIRECTORY) &&
                (current->create_as || current->create)) {
                fs_node_t *created = current->create_as
                    ? current->create_as(current, part, flags, uid, gid, mode)
                    : current->create(current, part, flags);
                if (current_owned) vfs_node_put(current);
                vfs_node_take_ownership(created);
                return created;
            }
            if (current_owned) vfs_node_put(current);
            return NULL;
        }
    }
    if (current_owned) vfs_node_put(current);
    return NULL;
}

static fs_node_t *vfs_parent_from_path(const char *path, char name[128]) {
    if (!path) return NULL;
    size_t length = strlen(path);
    if (length < 2 || length >= 512 || path[length - 1] == '/' || path[length - 1] == ':') return NULL;

    const char *last = path + length;
    while (last > path && last[-1] != '/' && last[-1] != ':') last--;
    size_t name_length = (size_t)((path + length) - last);
    if (!name_length || name_length >= 128) return NULL;
    memcpy(name, last, name_length);
    name[name_length] = '\0';

    char parent_path[512];
    size_t parent_length = (size_t)(last - path);
    if (parent_length == 0) {
        strcpy(parent_path, "/");
    } else {
        memcpy(parent_path, path, parent_length);
        parent_path[parent_length] = '\0';
    }
    fs_node_t *parent = vfs_open(parent_path);
    if (!parent || !(parent->flags & FS_DIRECTORY)) {
        vfs_node_put(parent);
        return NULL;
    }
    return parent;
}

int vfs_mkdir(const char *path, uint16_t uid, uint16_t gid, uint16_t mode) {
    if (!path || (mode & ~0777u)) return -1;
    char name[128];
    fs_node_t *parent = vfs_parent_from_path(path, name);
    if (!parent) return -2;
    if (!(parent->flags & FS_DIRECTORY) || !parent->mkdir) {
        vfs_node_put(parent);
        return -1;
    }
    fs_node_t *created = parent->mkdir(parent, name, uid, gid, mode);
    vfs_node_put(parent);
    if (!created) return -1;
    vfs_node_take_ownership(created);
    vfs_node_put(created);
    return 0;
}

int vfs_unlink(const char *path) {
    char name[128];
    fs_node_t *parent = vfs_parent_from_path(path, name);
    if (!parent) return -2;
    int rc = parent->unlink ? parent->unlink(parent, name) : -1;
    vfs_node_put(parent);
    return rc;
}

int vfs_rmdir(const char *path) {
    char name[128];
    fs_node_t *parent = vfs_parent_from_path(path, name);
    if (!parent) return -2;
    int rc = parent->rmdir ? parent->rmdir(parent, name) : -1;
    vfs_node_put(parent);
    return rc;
}

int vfs_rename(const char *old_path, const char *new_path) {
    char old_name[128], new_name[128];
    fs_node_t *old_parent = vfs_parent_from_path(old_path, old_name);
    if (!old_parent) return -2;
    fs_node_t *new_parent = vfs_parent_from_path(new_path, new_name);
    if (!new_parent) {
        vfs_node_put(old_parent);
        return -2;
    }
    int rc;
    if (!old_parent->rename || old_parent->rename != new_parent->rename)
        rc = -18; /* EXDEV */
    else
        rc = old_parent->rename(old_parent, old_name, new_parent, new_name);
    vfs_node_put(new_parent);
    vfs_node_put(old_parent);
    return rc;
}

const char *liwus_resource_type_name(liwus_resource_type_t type) {
    switch (type) {
        case LIWUS_RES_TEXT:    return "TEXT ";
        case LIWUS_RES_EXEC:    return "EXEC ";
        case LIWUS_RES_IMAGE:   return "IMAGE";
        case LIWUS_RES_AUDIO:   return "AUDIO";
        case LIWUS_RES_DEVICE:  return "DEV  ";
        case LIWUS_RES_CHANNEL: return "IPC  ";
        case LIWUS_RES_DIR:     return "DIR  ";
        default:                return "DATA ";
    }
}

liwus_resource_type_t vfs_detect_resource_type(fs_node_t *node, const char *path) {
    if (!node) return LIWUS_RES_UNKNOWN;
    if (node->flags & FS_DIRECTORY) return LIWUS_RES_DIR;
    if (node->flags == FS_CHARDEVICE || node->flags == FS_BLOCKDEVICE) return LIWUS_RES_DEVICE;
    if (node->flags == FS_PIPE) return LIWUS_RES_CHANNEL;

    if (path) {
        if (strncmp(path, "dev:", 4) == 0 || strncmp(path, "/dev", 4) == 0)
            return LIWUS_RES_DEVICE;
        if (strncmp(path, "ipc:", 4) == 0 || strncmp(path, "/ipc", 4) == 0)
            return LIWUS_RES_CHANNEL;
    }

    uint8_t magic[16] = {0};
    uint32_t read_bytes = 0;
    if (node->read) {
        read_bytes = node->read(node, 0, sizeof(magic), magic);
    }

    if (read_bytes >= 4) {
        if (magic[0] == 0x7F && magic[1] == 'E' && magic[2] == 'L' && magic[3] == 'F')
            return LIWUS_RES_EXEC;
        if (magic[0] == 'L' && magic[1] == 'X' && magic[2] == 'E')
            return LIWUS_RES_EXEC;
        if (magic[0] == 'B' && magic[1] == 'M')
            return LIWUS_RES_IMAGE;
        if (read_bytes >= 12 && magic[0] == 'R' && magic[1] == 'I' && magic[2] == 'F' && magic[3] == 'F' &&
            magic[8] == 'W' && magic[9] == 'A' && magic[10] == 'V' && magic[11] == 'E')
            return LIWUS_RES_AUDIO;
    }

    if (read_bytes > 0) {
        int is_text = 1;
        for (uint32_t i = 0; i < read_bytes; i++) {
            uint8_t c = magic[i];
            if (c < 9 || (c > 13 && c < 32)) {
                is_text = 0;
                break;
            }
        }
        if (is_text) return LIWUS_RES_TEXT;
    }

    return LIWUS_RES_UNKNOWN;
}
