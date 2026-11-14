#include "fs/devfs.h"
#include "kheap.h"
#include "string.h"
#include "vga.h"

extern volatile uint32_t timer_ticks;

static uint64_t dev_entropy_state = 0x853c49e6748fea9bULL;

static inline uint64_t rdtsc_val(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint8_t dev_next_random_byte(void) {
    dev_entropy_state ^= rdtsc_val();
    dev_entropy_state ^= (uint64_t)timer_ticks;
    dev_entropy_state ^= dev_entropy_state << 13;
    dev_entropy_state ^= dev_entropy_state >> 7;
    dev_entropy_state ^= dev_entropy_state << 17;
    return (uint8_t)(dev_entropy_state & 0xFF);
}

/* dev:display */
static uint32_t dev_display_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    uint32_t *fb = vga_get_backbuffer();
    if (!fb) return 0;
    uint32_t total = vga_get_fb_pitch() * vga_get_fb_height();
    if (offset >= total) return 0;
    if (offset + size > total) size = total - offset;
    memcpy(buffer, ((uint8_t *)fb) + offset, size);
    return size;
}

static uint32_t dev_display_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    uint32_t *fb = vga_get_backbuffer();
    if (!fb) return 0;
    uint32_t total = vga_get_fb_pitch() * vga_get_fb_height();
    if (offset >= total) return 0;
    if (offset + size > total) size = total - offset;
    memcpy(((uint8_t *)fb) + offset, buffer, size);
    vga_flip();
    return size;
}

/* dev:entropy & dev:random */
static uint32_t dev_entropy_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        buffer[i] = dev_next_random_byte();
    }
    return size;
}

static uint32_t dev_entropy_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;
    /* Adiciona entropia ao estado */
    for (uint32_t i = 0; i < size; i++) {
        dev_entropy_state = (dev_entropy_state << 5) ^ buffer[i];
    }
    return size;
}

/* dev:info */
static uint32_t dev_info_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    static char info_buf[512];
    uint32_t w = vga_get_fb_width();
    uint32_t h = vga_get_fb_height();
    uint32_t pitch = vga_get_fb_pitch();
    uint32_t ticks = timer_ticks;
    uint32_t seconds = ticks / 100;

    char *p = info_buf;
    strcpy(p, "LiwusOS 2.0 (Lean Monolithic x86_64 GNU C99)\n"); p += strlen(p);
    strcpy(p, "Display: "); p += strlen(p);
    /* Simple integer formatting */
    char num[16];
    int n = 0; uint32_t temp = w;
    do { num[n++] = '0' + (temp % 10); temp /= 10; } while (temp);
    while (n) *p++ = num[--n];
    *p++ = 'x';
    temp = h;
    do { num[n++] = '0' + (temp % 10); temp /= 10; } while (temp);
    while (n) *p++ = num[--n];
    strcpy(p, " (32 bpp, pitch "); p += strlen(p);
    temp = pitch;
    do { num[n++] = '0' + (temp % 10); temp /= 10; } while (temp);
    while (n) *p++ = num[--n];
    strcpy(p, ")\nUptime: "); p += strlen(p);
    temp = seconds;
    do { num[n++] = '0' + (temp % 10); temp /= 10; } while (temp);
    while (n) *p++ = num[--n];
    strcpy(p, "s ("); p += strlen(p);
    temp = ticks;
    do { num[n++] = '0' + (temp % 10); temp /= 10; } while (temp);
    while (n) *p++ = num[--n];
    strcpy(p, " ticks)\nVolumes: sys: (System), disk: (Storage), dev: (Devices), ipc: (Channels)\n");
    p += strlen(p);
    *p = '\0';

    uint32_t total = (uint32_t)(p - info_buf);
    if (offset >= total) return 0;
    if (offset + size > total) size = total - offset;
    memcpy(buffer, info_buf + offset, size);
    return size;
}

/* dev:null */
static uint32_t dev_null_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}
static uint32_t dev_null_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

/* dev:zero */
static uint32_t dev_zero_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    memset(buffer, 0, size);
    return size;
}

/* dev:sound */
static uint32_t dev_sound_write(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

static fs_node_t dev_nodes[] = {
    {
        .name = "display",
        .flags = FS_CHARDEVICE,
        .mask = 0666,
        .length = 0,
        .read = dev_display_read,
        .write = dev_display_write,
    },
    {
        .name = "entropy",
        .flags = FS_CHARDEVICE,
        .mask = 0666,
        .length = 0,
        .read = dev_entropy_read,
        .write = dev_entropy_write,
    },
    {
        .name = "random",
        .flags = FS_CHARDEVICE,
        .mask = 0666,
        .length = 0,
        .read = dev_entropy_read,
        .write = dev_entropy_write,
    },
    {
        .name = "info",
        .flags = FS_CHARDEVICE,
        .mask = 0444,
        .length = 256,
        .read = dev_info_read,
        .write = NULL,
    },
    {
        .name = "null",
        .flags = FS_CHARDEVICE,
        .mask = 0666,
        .length = 0,
        .read = dev_null_read,
        .write = dev_null_write,
    },
    {
        .name = "zero",
        .flags = FS_CHARDEVICE,
        .mask = 0666,
        .length = 0,
        .read = dev_zero_read,
        .write = dev_null_write,
    },
    {
        .name = "sound",
        .flags = FS_CHARDEVICE,
        .mask = 0666,
        .length = 0,
        .read = dev_null_read,
        .write = dev_sound_write,
    }
};

#define DEV_NODE_COUNT (sizeof(dev_nodes) / sizeof(dev_nodes[0]))

static struct dirent dev_dirent;

static struct dirent *devfs_readdir(fs_node_t *node, uint32_t index) {
    (void)node;
    if (index >= DEV_NODE_COUNT) return NULL;
    strcpy(dev_dirent.name, dev_nodes[index].name);
    dev_dirent.ino = index + 1;
    return &dev_dirent;
}

static fs_node_t *devfs_finddir(fs_node_t *node, const char *name) {
    (void)node;
    if (!name) return NULL;
    for (uint32_t i = 0; i < DEV_NODE_COUNT; i++) {
        if (strcmp(dev_nodes[i].name, name) == 0) {
            fs_node_t *res = (fs_node_t *)kmalloc(sizeof(fs_node_t));
            if (!res) return NULL;
            memcpy(res, &dev_nodes[i], sizeof(fs_node_t));
            if (strcmp(res->name, "display") == 0) {
                res->length = vga_get_fb_pitch() * vga_get_fb_height();
            }
            res->vfs_owned = 1;
            return res;
        }
    }
    return NULL;
}

static fs_node_t devfs_root_node = {
    .name = "dev",
    .flags = FS_DIRECTORY,
    .mask = 0755,
    .length = DEV_NODE_COUNT,
    .readdir = devfs_readdir,
    .finddir = devfs_finddir,
};

fs_node_t *devfs_init(void) {
    return &devfs_root_node;
}
