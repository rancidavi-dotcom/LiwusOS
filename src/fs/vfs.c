#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "serial.h"

fs_node_t* fs_root = NULL;
static vfs_mount_t* mounts = NULL;

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

fs_node_t* vfs_open(const char* path) {
    if (!path) return NULL;
    if (path[0] == '\0') return fs_root;

    // Inicializa a busca a partir da raiz ou do mount point mais especifico
    vfs_mount_t* best_match = NULL;
    int max_len = -1;

    vfs_mount_t* it = mounts;
    while (it) {
        int m_len = strlen(it->path);
        if (strncmp(path, it->path, m_len) == 0) {
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
            part[i++] = *remaining++;
        }
        part[i] = '\0';
        if (*remaining == '/') remaining++;

        fs_node_t* next_node = finddir_fs(current, part);
        if (!next_node) return NULL;
        current = next_node;
    }

    return current;
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

fs_node_t* vfs_create(const char* path, uint32_t flags) {
    if (!path) return NULL;
    if (path[0] == '\0') return NULL; // Can't create root

    vfs_mount_t* best_match = NULL;
    int max_len = -1;

    vfs_mount_t* it = mounts;
    while (it) {
        int m_len = strlen(it->path);
        if (strncmp(path, it->path, m_len) == 0) {
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
            part[i++] = *remaining++;
        }
        part[i] = '\0';
        
        if (*remaining == '/') {
            remaining++;
            // Not the last part, must be a directory
            fs_node_t* next_node = finddir_fs(current, part);
            if (!next_node) return NULL;
            current = next_node;
        } else {
            // Last part, this is the file to create
            if (current && (current->flags & FS_DIRECTORY) && current->create) {
                return current->create(current, part, flags);
            }
            return NULL;
        }
    }
    return NULL;
}
