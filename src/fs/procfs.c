#include "procfs.h"
#include "kheap.h"
#include "string.h"
#include "task.h"
#include "vma.h"

#define PROC_ROOT_INODE 0x90000001u
#define PROC_SELF_INODE 0x90000002u
#define PROC_MAPS_INODE 0x90000003u
#define MAPS_LINE_MAX 80u

static char *append_hex(char *out, uint64_t value, unsigned min_digits) {
    static const char digits[] = "0123456789abcdef";
    char reversed[16];
    unsigned used = 0;
    do {
        reversed[used++] = digits[value & 0xfu];
        value >>= 4;
    } while (value || used < min_digits);
    while (used) *out++ = reversed[--used];
    return out;
}

static char *append_text(char *out, const char *text) {
    size_t length = strlen(text);
    memcpy(out, text, length);
    return out + length;
}

static uint32_t read_maps(fs_node_t *node, uint32_t offset, uint32_t size,
                          uint8_t *out) {
    if (offset >= node->length) return 0;
    if (size > node->length - offset) size = node->length - offset;
    const uint8_t *snapshot = (const uint8_t *)(node + 1);
    memcpy(out, snapshot + offset, size);
    return size;
}

static fs_node_t *open_maps(const process_t *process) {
    uint64_t irq_flags = task_scheduler_lock();
    size_t capacity = 0;
    for (const vma_t *area = process->mm->vmas; area; area = area->next) {
        if (capacity > UINT32_MAX - sizeof(fs_node_t) - MAPS_LINE_MAX) {
            task_scheduler_unlock(irq_flags);
            return NULL;
        }
        capacity += MAPS_LINE_MAX;
    }
    fs_node_t *node = kmalloc(sizeof(*node) + capacity);
    if (!node) {
        task_scheduler_unlock(irq_flags);
        return NULL;
    }

    char *out = (char *)(node + 1);
    for (const vma_t *area = process->mm->vmas; area; area = area->next) {
        out = append_hex(out, area->start, 8);
        *out++ = '-';
        out = append_hex(out, area->end, 8);
        *out++ = ' ';
        *out++ = area->prot & VMA_PROT_READ ? 'r' : '-';
        *out++ = area->prot & VMA_PROT_WRITE ? 'w' : '-';
        *out++ = area->prot & VMA_PROT_EXEC ? 'x' : '-';
        *out++ = area->flags & VMA_SHARED ? 's' : 'p';
        *out++ = ' ';
        uint64_t offset = (area->flags & (VMA_FILE | VMA_ELF))
            ? area->file_offset & ~(uint64_t)(PAGE_SIZE - 1) : 0;
        out = append_hex(out, offset, 8);
        /* The VMAs record offsets and permissions, without a pathname or
         * file identity. The optional name is available for heap and stack. */
        out = append_text(out, " 00:00 0");
        if (area->flags & VMA_STACK) out = append_text(out, " [stack]");
        else if (area->flags & VMA_HEAP) out = append_text(out, " [heap]");
        *out++ = '\n';
    }
    task_scheduler_unlock(irq_flags);

    memset(node, 0, sizeof(*node));
    strcpy(node->name, "maps");
    node->flags = FS_FILE;
    node->mask = 0400;
    node->uid = process->euid;
    node->gid = process->egid;
    node->inode = PROC_MAPS_INODE;
    node->length = (uint32_t)(out - (char *)(node + 1));
    node->read = read_maps;
    return node;
}

static fs_node_t *find_maps(fs_node_t *self, const char *name) {
    (void)self;
    if (strcmp(name, "maps") != 0 || !current_task ||
        !current_task->process || !current_task->process->mm ||
        !current_task->user_mode) return NULL;
    return open_maps(current_task->process);
}

static struct dirent *read_self_dir(fs_node_t *node, uint32_t index) {
    static struct dirent maps = { .name = "maps", .ino = PROC_MAPS_INODE };
    (void)node;
    return index == 0 ? &maps : NULL;
}

static fs_node_t *find_self(fs_node_t *root, const char *name) {
    (void)root;
    if (strcmp(name, "self") != 0 || !current_task || !current_task->user_mode)
        return NULL;
    fs_node_t *node = kmalloc(sizeof(*node));
    if (!node) return NULL;
    memset(node, 0, sizeof(*node));
    strcpy(node->name, "self");
    node->flags = FS_DIRECTORY;
    node->mask = 0555;
    node->inode = PROC_SELF_INODE;
    node->finddir = find_maps;
    node->readdir = read_self_dir;
    return node;
}

static struct dirent *read_proc_dir(fs_node_t *node, uint32_t index) {
    static struct dirent self = { .name = "self", .ino = PROC_SELF_INODE };
    (void)node;
    return index == 0 ? &self : NULL;
}

fs_node_t *procfs_init(void) {
    static fs_node_t root = {
        .name = "proc",
        .flags = FS_DIRECTORY,
        .mask = 0555,
        .inode = PROC_ROOT_INODE,
        .finddir = find_self,
        .readdir = read_proc_dir,
    };
    return &root;
}
