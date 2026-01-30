#ifndef LGX_VIRTIO_H
#define LGX_VIRTIO_H

#include <stdint.h>
#include "pci.h"
#include "video.h"

// VirtIO-GPU PCI IDs
#define VIRTIO_VENDOR_ID 0x1AF4
#define VIRTIO_GPU_DEVICE_ID 0x1050

// VirtIO-GPU Control Queue Commands
typedef enum {
    VIRTIO_GPU_CMD_GET_DISPLAY_INFO = 0x0100,
    VIRTIO_GPU_CMD_RESOURCE_CREATE_2D = 0x0101,
    VIRTIO_GPU_CMD_RESOURCE_UNREF = 0x0102,
    VIRTIO_GPU_CMD_SET_SCANOUT = 0x0103,
    VIRTIO_GPU_CMD_RESOURCE_FLUSH = 0x0104,
    VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D = 0x0105,
    VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING = 0x0106,
    VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING = 0x0107,
    VIRTIO_GPU_CMD_GET_CAPSET_INFO = 0x0108,
    VIRTIO_GPU_CMD_GET_CAPSET = 0x0109,
} lg_virtio_gpu_ctrl_type_t;

// Estruturas de Header do VirtIO
typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} lg_virtio_gpu_ctrl_hdr_t;

// --- VirtIO Ring Structures ---

#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2
#define VIRTQ_DESC_F_INDIRECT 4

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} vring_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[256];
} vring_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} vring_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    vring_used_elem_t ring[256];
} vring_used_t;

typedef struct {
    vring_desc_t* desc;
    vring_avail_t* avail;
    vring_used_t* used;
    uint16_t last_used_idx;
    uint16_t free_head;
    uint16_t num;
    uint32_t queue_index;
} virtqueue_t;

// --- GPU Command Structures ---

typedef struct {
    lg_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} lg_virtio_gpu_resource_create_2d_t;

typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} lg_virtio_gpu_mem_entry_t;

typedef struct {
    lg_virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
    lg_virtio_gpu_mem_entry_t entries[1];
} lg_virtio_gpu_resource_attach_backing_t;

typedef struct {
    lg_virtio_gpu_ctrl_hdr_t hdr;
    rect_t r;
    uint32_t scanout_id;
    uint32_t resource_id;
} lg_virtio_gpu_set_scanout_t;

typedef struct {
    lg_virtio_gpu_ctrl_hdr_t hdr;
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t resource_id;
} lg_virtio_gpu_resource_flush_t;

// Comandos de inicialização do driver
int lg_virtio_gpu_init(pci_device_t* pci_dev);
void lg_virtio_gpu_send_command(uint32_t type, void* data, uint32_t size);

#endif