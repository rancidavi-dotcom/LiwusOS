#include "procfs.h"
#include "kheap.h"
#include "string.h"
#include "task.h"
#include "vma.h"

#define PROC_ROOT_INODE 0x90000001u
#define PROC_SELF_INODE 0x90000002u
#define PROC_MAPS_INODE 0x90000003u
#define PROC_STAT_INODE 0x90000004u
#define PROC_SELF_STAT_INODE 0x90000005u
#define MAPS_LINE_MAX 80u

static const char proc_stat_text[] =
    "cpu  0 0 0 0 0 0 0 0 0 0\n"
    "intr 0\n"
    "ctxt 0\n"
    "btime 0\n"
    "processes 1\n"
    "procs_running 1\n"
    "procs_blocked 0\n";

static const char proc_self_stat_text[] =
    "5 (Granny) S 1 5 5 0 0 0 0 0 0 0 0 0 0 0 0 0 1 0 0 0 0 0 0\n";

static uint32_t read_static_text(fs_node_t *node, uint32_t offset,
                                 uint32_t size, uint8_t *out) {
    const char *text = (const char *)(node->impl_offset == PROC_STAT_INODE
        ? proc_stat_text : proc_self_stat_text);
    uint32_t length = node->length;
    if (offset >= length) return 0;
    if (size > length - offset) size = length - offset;
    memcpy(out, text + offset, size);
    return size;
}

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

static fs_node_t *find_self_entry(fs_node_t *self, const char *name) {
    fs_node_t *node = find_maps(self, name);
    if (node || strcmp(name, "maps") == 0) return node;
    if (strcmp(name, "stat") != 0 || !current_task ||
        !current_task->user_mode) return NULL;
    static fs_node_t stat_node;
    memset(&stat_node, 0, sizeof(stat_node));
    strcpy(stat_node.name, "stat");
    stat_node.flags = FS_FILE;
    stat_node.mask = 0444;
    stat_node.inode = PROC_SELF_STAT_INODE;
    stat_node.impl_offset = PROC_SELF_STAT_INODE;
    stat_node.length = sizeof(proc_self_stat_text) - 1;
    stat_node.read = read_static_text;
    return &stat_node;
}

static struct dirent *read_self_dir(fs_node_t *node, uint32_t index) {
    static struct dirent maps = { .name = "maps", .ino = PROC_MAPS_INODE };
    static struct dirent stat = { .name = "stat", .ino = PROC_SELF_STAT_INODE };
    (void)node;
    if (index == 0) return &maps;
    if (index == 1) return &stat;
    return NULL;
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
    node->finddir = find_self_entry;
    node->readdir = read_self_dir;
    return node;
}

static struct dirent *read_proc_dir(fs_node_t *node, uint32_t index) {
    static struct dirent self = { .name = "self", .ino = PROC_SELF_INODE };
    static struct dirent stat = { .name = "stat", .ino = PROC_STAT_INODE };
    (void)node;
    if (index == 0) return &self;
    if (index == 1) return &stat;
    return NULL;
}

static fs_node_t *find_proc_entry(fs_node_t *self, const char *name) {
    if (strcmp(name, "self") == 0) return find_self(self, name);
    if (strcmp(name, "stat") != 0 || !current_task || !current_task->user_mode)
        return NULL;
    static fs_node_t stat_node;
    memset(&stat_node, 0, sizeof(stat_node));
    strcpy(stat_node.name, "stat");
    stat_node.flags = FS_FILE;
    stat_node.mask = 0444;
    stat_node.inode = PROC_STAT_INODE;
    stat_node.impl_offset = PROC_STAT_INODE;
    stat_node.length = sizeof(proc_stat_text) - 1;
    stat_node.read = read_static_text;
    return &stat_node;
}

fs_node_t *procfs_init(void) {
    static fs_node_t root = {
        .name = "proc",
        .flags = FS_DIRECTORY,
        .mask = 0555,
        .inode = PROC_ROOT_INODE,
        .finddir = find_proc_entry,
        .readdir = read_proc_dir,
    };
    return &root;
}
