#include "lgx_virtio.h"
#include "io.h"
#include "video.h"
#include "string.h"
#include "kheap.h"

static uint32_t virtio_io_base = 0;
static virtqueue_t control_q;
static virtqueue_t cursor_q;

static void virtqueue_setup(virtqueue_t* q, uint32_t index, uint32_t io_base) {
    q->queue_index = index;
    q->num = 256; 

    outw(io_base + 0x0E, index); 

    void* ring_mem = kmalloc_a(16384); 
    memset(ring_mem, 0, 16384);

    q->desc = (vring_desc_t*)ring_mem;
    q->avail = (vring_avail_t*)((uintptr_t)ring_mem + (256 * 16));
    q->used = (vring_used_t*)((uintptr_t)ring_mem + 4096 * 2); 

    uint32_t pfn = (uint32_t)ring_mem / 4096;
    outl(io_base + 0x08, pfn); 

    q->last_used_idx = 0;
    q->free_head = 0;
}

static uint32_t next_resource_id = 1;

void lg_virtio_gpu_create_resource_2d(uint32_t width, uint32_t height, uint32_t* res_id) {
    lg_virtio_gpu_resource_create_2d_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd.resource_id = next_resource_id++;
    cmd.format = 1; 
    cmd.width = width;
    cmd.height = height;

    lg_virtio_gpu_send_command(cmd.hdr.type, &cmd, sizeof(cmd));
    if (res_id) *res_id = cmd.resource_id;
}

void lg_virtio_gpu_attach_backing(uint32_t res_id, void* ptr, uint32_t size) {
    lg_virtio_gpu_resource_attach_backing_t* cmd = (lg_virtio_gpu_resource_attach_backing_t*)kmalloc(sizeof(lg_virtio_gpu_resource_attach_backing_t));
    memset(cmd, 0, sizeof(lg_virtio_gpu_resource_attach_backing_t));
    
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->resource_id = res_id;
    cmd->nr_entries = 1;
    cmd->entries[0].addr = (uint64_t)(uintptr_t)ptr;
    cmd->entries[0].length = size;

    lg_virtio_gpu_send_command(cmd->hdr.type, cmd, sizeof(lg_virtio_gpu_resource_attach_backing_t));
}

void lg_virtio_gpu_set_scanout(uint32_t res_id, uint32_t w, uint32_t h) {
    lg_virtio_gpu_set_scanout_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd.resource_id = res_id;
    cmd.scanout_id = 0; // Monitor principal
    cmd.r.x = 0; cmd.r.y = 0;
    cmd.r.w = w; cmd.r.h = h;

    lg_virtio_gpu_send_command(cmd.hdr.type, &cmd, sizeof(cmd));
}

void lg_virtio_gpu_flush(uint32_t res_id, uint32_t w, uint32_t h) {
    lg_virtio_gpu_resource_flush_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd.resource_id = res_id;
    cmd.width = w;
    cmd.height = h;
    lg_virtio_gpu_send_command(cmd.hdr.type, &cmd, sizeof(cmd));
}

int lg_virtio_gpu_init(pci_device_t* pci_dev) {
    uint32_t bar0 = pci_read_config(pci_dev->bus, pci_dev->device, pci_dev->function, 0x10);
    if (!(bar0 & 0x1)) return -1;
    virtio_io_base = bar0 & ~0x3;

    outb(virtio_io_base + 0x12, 0); 
    uint8_t status = 0x01 | 0x02; 
    outb(virtio_io_base + 0x12, status);

    virtqueue_setup(&control_q, 0, virtio_io_base);
    virtqueue_setup(&cursor_q, 1, virtio_io_base);

    status |= 0x04; 
    outb(virtio_io_base + 0x12, status);

    lg_virtio_gpu_ctrl_hdr_t test_cmd;
    memset(&test_cmd, 0, sizeof(test_cmd));
    test_cmd.type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;
    lg_virtio_gpu_send_command(test_cmd.type, &test_cmd, sizeof(test_cmd));

    extern uint32_t* backbuffer;
    extern uint32_t screen_width, screen_height, screen_size;
    uint32_t res_id;
    lg_virtio_gpu_create_resource_2d(screen_width, screen_height, &res_id);
    lg_virtio_gpu_attach_backing(res_id, backbuffer, screen_size * 4);
    lg_virtio_gpu_set_scanout(res_id, screen_width, screen_height);

    return 0;
}

void lg_virtio_gpu_send_command(uint32_t type, void* data, uint32_t size) {
    (void)type;
    if (virtio_io_base == 0) return;

    uint16_t head = control_q.free_head;
    vring_desc_t* d = &control_q.desc[head];
    d->addr = (uint64_t)(uintptr_t)data;
    d->len = size;
    d->flags = 0;
    d->next = 0;

    control_q.avail->ring[control_q.avail->idx % 256] = head;
    control_q.avail->idx++;

    outw(virtio_io_base + 0x10, 0); 

    while (control_q.used->idx == control_q.last_used_idx) {
        asm volatile("pause");
    }
    control_q.last_used_idx = control_q.used->idx;
    control_q.free_head = (control_q.free_head + 1) % 256;
}