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

fs_node_t* vfs_open(const char* path) {
    if (!path) return NULL;
    if (path[0] == '\0') return fs_root;
    if (strlen(path) >= 512) return NULL;

    // Inicializa a busca a partir da raiz ou do mount point mais especifico
    vfs_mount_t* best_match = NULL;
    int max_len = -1;

    vfs_mount_t* it = mounts;
    while (it) {
        int m_len = strlen(it->path);
        if (strncmp(path, it->path, m_len) == 0 &&
            (it->path[m_len - 1] == '/' || path[m_len] == '\0' ||
             path[m_len] == '/')) {
            if (m_len > max_len) {
                max_len = m_len;
                best_match = it;
            }
        }
        it = it->next;
    }

    if (!best_match) {
        it = mounts;
        while (it) {
            if (strcmp(it->path, "/") == 0) {
                best_match = it;
                max_len = 1;
                break;
            }
            it = it->next;
        }
    }

    if (!best_match) return NULL;

    fs_node_t* current = best_match->root;
    int current_owned = 0;
    const char* remaining;

    if (strcmp(best_match->path, "/") == 0) {
        remaining = (path[0] == '/') ? path + 1 : path;
    } else {
        remaining = path + max_len;
        if (*remaining == '/') remaining++;
    }

    // Se sobrar algo apos o mount point, navegamos recursivamente
    if (*remaining == '\0') return current;

    char part[128];
    while (*remaining) {
        int i = 0;
        while (*remaining && *remaining != '/') {
            if (i >= (int)sizeof(part) - 1) {
                if (current_owned) vfs_node_put(current);
                return NULL;
            }
            part[i++] = *remaining++;
        }
        part[i] = '\0';
        if (*remaining == '/') remaining++;

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
    if (length == 0 || length >= 512 || path[0] != '/') return -1;

    fs_node_t *root = vfs_open("/");
    if (!root) return -2;
    int root_rc = (root->flags & FS_DIRECTORY)
        ? vfs_node_access(root, uid, gid, VFS_ACCESS_EXEC)
        : -VFS_ENOTDIR;
    vfs_node_put(root);
    if (root_rc < 0) return root_rc;

    /* Check search permission on each directory before walking through it. */
    char prefix[512];
    memcpy(prefix, path, length + 1);
    for (size_t i = 1; i < length; i++) {
        if (prefix[i] != '/') continue;
        char saved = prefix[i];
        prefix[i] = '\0';
        fs_node_t *parent = vfs_open(prefix[0] ? prefix : "/");
        prefix[i] = saved;
        if (!parent) return -2; /* ENOENT */
        int rc = (parent->flags & FS_DIRECTORY)
            ? vfs_node_access(parent, uid, gid, VFS_ACCESS_EXEC)
            : -VFS_ENOTDIR;
        vfs_node_put(parent);
        if (rc < 0) return rc;
    }

    fs_node_t *node = vfs_open(path);
    if (!node) return -2; /* ENOENT */
    int rc = vfs_node_access(node, uid, gid, requested);
    if (rc < 0) {
        vfs_node_put(node);
        return rc;
    }
    *out = node;
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

    vfs_mount_t* best_match = NULL;
    int max_len = -1;

    vfs_mount_t* it = mounts;
    while (it) {
        int m_len = strlen(it->path);
        if (strncmp(path, it->path, m_len) == 0 &&
            (it->path[m_len - 1] == '/' || path[m_len] == '\0' ||
             path[m_len] == '/')) {
            if (m_len > max_len) {
                max_len = m_len;
                best_match = it;
            }
        }
        it = it->next;
    }

    if (!best_match) {
        it = mounts;
        while (it) {
            if (strcmp(it->path, "/") == 0) {
                best_match = it;
                max_len = 1;
                break;
            }
            it = it->next;
        }
    }

    if (!best_match) return NULL;

    fs_node_t* current = best_match->root;
    int current_owned = 0;
    const char* remaining;

    if (strcmp(best_match->path, "/") == 0) {
        remaining = (path[0] == '/') ? path + 1 : path;
    } else {
        remaining = path + max_len;
        if (*remaining == '/') remaining++;
    }

    if (*remaining == '\0') return NULL; // Exists

    char part[128];
    while (*remaining) {
        int i = 0;
        while (*remaining && *remaining != '/') {
            if (i >= (int)sizeof(part) - 1) {
                if (current_owned) vfs_node_put(current);
                return NULL;
            }
            part[i++] = *remaining++;
        }
        part[i] = '\0';
        
        if (*remaining == '/') {
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

int vfs_mkdir(const char *path, uint16_t uid, uint16_t gid, uint16_t mode) {
    if (!path || path[0] != '/' || (mode & ~0777u)) return -1;
    size_t length = strlen(path);
    if (length < 2 || length >= 512 || path[length - 1] == '/') return -1;
    const char *slash = path + length;
    while (slash > path && slash[-1] != '/') slash--;
    size_t name_length = (size_t)((path + length) - slash);
    if (!name_length || name_length >= 128) return -1;
    char name[128];
    memcpy(name, slash, name_length);
    name[name_length] = '\0';
    char parent_path[512];
    size_t parent_length = (size_t)(slash - path);
    if (parent_length == 0) {
        strcpy(parent_path, "/");
    } else {
        if (parent_length == 1) parent_length = 1;
        else parent_length--;
        memcpy(parent_path, path, parent_length);
        parent_path[parent_length] = '\0';
    }
    fs_node_t *parent = vfs_open(parent_path);
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

static fs_node_t *vfs_parent_from_path(const char *path, char name[128]) {
    if (!path || path[0] != '/') return NULL;
    size_t length = strlen(path);
    if (length < 2 || length >= 512 || path[length - 1] == '/') return NULL;
    const char *last = path + length;
    while (last > path && last[-1] != '/') last--;
    size_t name_length = (size_t)((path + length) - last);
    if (!name_length || name_length >= 128) return NULL;
    memcpy(name, last, name_length);
    name[name_length] = '\0';
    size_t parent_length = (size_t)(last - path);
    char parent_path[512];
    if (parent_length <= 1) {
        strcpy(parent_path, "/");
    } else {
        parent_length--;
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
