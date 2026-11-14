#include "fs/ipcfs.h"
#include "kheap.h"
#include "string.h"
#include "serial.h"
#include "kernel/task.h"

#define LIWUS_IPC_MAX_CHANNELS 32
#define LIWUS_IPC_BUFFER_SIZE  4096

typedef struct {
    char name[64];
    uint8_t buffer[LIWUS_IPC_BUFFER_SIZE];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    int active;
} liwus_ipc_channel_t;

static liwus_ipc_channel_t g_ipc_channels[LIWUS_IPC_MAX_CHANNELS];
static uint32_t g_ipc_channel_count = 0;

static int ipc_channel_truncate(fs_node_t *node) {
    if (!node || node->impl >= LIWUS_IPC_MAX_CHANNELS) return -1;
    uint64_t flags = task_scheduler_lock();
    liwus_ipc_channel_t *ch = &g_ipc_channels[node->impl];
    ch->head = 0;
    ch->tail = 0;
    ch->count = 0;
    node->length = 0;
    task_scheduler_unlock(flags);
    return 0;
}

static uint32_t ipc_channel_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    if (!node || node->impl >= LIWUS_IPC_MAX_CHANNELS) return 0;
    uint64_t flags = task_scheduler_lock();
    liwus_ipc_channel_t *ch = &g_ipc_channels[node->impl];
    if (!ch->active || ch->count == 0) {
        task_scheduler_unlock(flags);
        return 0;
    }

    uint32_t to_read = size;
    if (to_read > ch->count) to_read = ch->count;

    for (uint32_t i = 0; i < to_read; i++) {
        buffer[i] = ch->buffer[ch->tail];
        ch->tail = (ch->tail + 1) % LIWUS_IPC_BUFFER_SIZE;
    }
    ch->count -= to_read;
    node->length = ch->count;
    task_scheduler_unlock(flags);
    return to_read;
}

static uint32_t ipc_channel_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    if (!node || node->impl >= LIWUS_IPC_MAX_CHANNELS) return 0;
    uint64_t flags = task_scheduler_lock();
    liwus_ipc_channel_t *ch = &g_ipc_channels[node->impl];
    if (!ch->active) {
        task_scheduler_unlock(flags);
        return 0;
    }

    uint32_t space = LIWUS_IPC_BUFFER_SIZE - ch->count;
    uint32_t to_write = size;
    if (to_write > space) to_write = space;

    for (uint32_t i = 0; i < to_write; i++) {
        ch->buffer[ch->head] = buffer[i];
        ch->head = (ch->head + 1) % LIWUS_IPC_BUFFER_SIZE;
    }
    ch->count += to_write;
    node->length = ch->count;
    task_scheduler_unlock(flags);
    return to_write;
}

static struct dirent ipc_dirent;

static struct dirent *ipcfs_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    uint32_t current_idx = 0;
    for (uint32_t i = 0; i < LIWUS_IPC_MAX_CHANNELS; i++) {
        if (g_ipc_channels[i].active) {
            if (current_idx == index) {
                strcpy(ipc_dirent.name, g_ipc_channels[i].name);
                ipc_dirent.ino = i + 1;
                return &ipc_dirent;
            }
            current_idx++;
        }
    }
    return NULL;
}

static fs_node_t *ipcfs_finddir(fs_node_t *node, const char *name) {
    (void)node;
    if (!name || !name[0]) return NULL;

    /* 1. Procura se canal ja existe */
    for (uint32_t i = 0; i < LIWUS_IPC_MAX_CHANNELS; i++) {
        if (g_ipc_channels[i].active && strcmp(g_ipc_channels[i].name, name) == 0) {
            fs_node_t *res = (fs_node_t *)kmalloc(sizeof(fs_node_t));
            if (!res) return NULL;
            memset(res, 0, sizeof(fs_node_t));
            strcpy(res->name, g_ipc_channels[i].name);
            res->flags = FS_PIPE;
            res->mask = 0666;
            res->length = g_ipc_channels[i].count;
            res->impl = i;
            res->read = ipc_channel_read;
            res->write = ipc_channel_write;
            res->truncate = ipc_channel_truncate;
            res->vfs_owned = 1;
            return res;
        }
    }

    /* 2. Se nao existe, cria canal dinamicamente */
    for (uint32_t i = 0; i < LIWUS_IPC_MAX_CHANNELS; i++) {
        if (!g_ipc_channels[i].active) {
            liwus_ipc_channel_t *ch = &g_ipc_channels[i];
            memset(ch, 0, sizeof(liwus_ipc_channel_t));
            strncpy(ch->name, name, 63);
            ch->name[63] = '\0';
            ch->active = 1;
            g_ipc_channel_count++;

            fs_node_t *res = (fs_node_t *)kmalloc(sizeof(fs_node_t));
            if (!res) return NULL;
            memset(res, 0, sizeof(fs_node_t));
            strcpy(res->name, ch->name);
            res->flags = FS_PIPE;
            res->mask = 0666;
            res->length = 0;
            res->impl = i;
            res->read = ipc_channel_read;
            res->write = ipc_channel_write;
            res->truncate = ipc_channel_truncate;
            res->vfs_owned = 1;
            return res;
        }
    }

    return NULL;
}

static fs_node_t ipcfs_root_node = {
    .name = "ipc",
    .flags = FS_DIRECTORY,
    .mask = 0755,
    .readdir = ipcfs_readdir,
    .finddir = ipcfs_finddir,
};

fs_node_t *ipcfs_init(void) {
    memset(g_ipc_channels, 0, sizeof(g_ipc_channels));
    g_ipc_channel_count = 0;
    return &ipcfs_root_node;
}
