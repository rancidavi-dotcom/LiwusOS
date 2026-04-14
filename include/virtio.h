#ifndef VIRTIO_H
#define VIRTIO_H

#include <stdbool.h>
#include <stdint.h>
#include "pci.h"

/* VirtIO Constants */
#define VIRTIO_VENDOR_ID 0x1AF4

/* MMIO Registers Offsets (Common Config) */
#define VIRTIO_REG_DEVICE_FEATURES_SEL 0x00
#define VIRTIO_REG_DEVICE_FEATURES     0x04
#define VIRTIO_REG_DRIVER_FEATURES_SEL 0x08
#define VIRTIO_REG_DRIVER_FEATURES     0x0C
#define VIRTIO_REG_MSI_CONFIG          0x10
#define VIRTIO_REG_NUM_QUEUES          0x12
#define VIRTIO_REG_DEVICE_STATUS       0x14
#define VIRTIO_REG_CONFIG_GENERATION   0x15
#define VIRTIO_REG_QUEUE_SEL           0x16
#define VIRTIO_REG_QUEUE_SIZE          0x18
#define VIRTIO_REG_QUEUE_MSI_VECTOR    0x1A
#define VIRTIO_REG_QUEUE_ENABLE        0x1C
#define VIRTIO_REG_QUEUE_NOTIFY_OFF    0x1E
#define VIRTIO_REG_QUEUE_DESC_LOW      0x20
#define VIRTIO_REG_QUEUE_DESC_HIGH     0x24
#define VIRTIO_REG_QUEUE_AVAIL_LOW     0x28
#define VIRTIO_REG_QUEUE_AVAIL_HIGH    0x2C
#define VIRTIO_REG_QUEUE_USED_LOW      0x30
#define VIRTIO_REG_QUEUE_USED_HIGH     0x34

/* Device Status Bits */
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER      2
#define VIRTIO_STATUS_DRIVER_OK   4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 64
#define VIRTIO_STATUS_FAILED      128

/* PCI Capabilities */
#define VIRTIO_PCI_CAP_COMMON_CFG   1
#define VIRTIO_PCI_CAP_NOTIFY_CFG   2
#define VIRTIO_PCI_CAP_ISR_CFG      3
#define VIRTIO_PCI_CAP_DEVICE_CFG   4
#define VIRTIO_PCI_CAP_PCI_CFG      5

/* Virtqueue Descriptor Flags */
#define VIRTQ_DESC_F_NEXT     1
#define VIRTQ_DESC_F_WRITE    2

/* Feature Bits */
#define VIRTIO_F_VERSION_1               32

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed)) virtq_desc_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[64];
} __attribute__((packed)) virtq_avail_t;

typedef struct {
    uint32_t id;
    uint32_t len;
} __attribute__((packed)) virtq_used_elem_t;

typedef struct {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[64];
} __attribute__((packed)) virtq_used_t;

/* VirtIO-GPU Command Types */
#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO 0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D 0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF 0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT 0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH 0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D 0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING 0x0106

#define VIRTIO_GPU_RESP_OK_NODATA 0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO 0x1101

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM 1

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_ctrl_hdr_t;

typedef struct {
    uint32_t x, y, width, height;
} __attribute__((packed)) virtio_gpu_rect_t;

typedef struct {
    virtio_gpu_rect_t r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed)) virtio_gpu_display_one_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_display_one_t pmodes[16];
} __attribute__((packed)) virtio_gpu_resp_display_info_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed)) virtio_gpu_resource_create_2d_t;

typedef struct {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_mem_entry_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed)) virtio_gpu_resource_attach_backing_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed)) virtio_gpu_set_scanout_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_transfer_to_host_2d_t;

typedef struct {
    virtio_gpu_ctrl_hdr_t hdr;
    virtio_gpu_rect_t r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed)) virtio_gpu_resource_flush_t;

/* Driver Interface */
bool virtio_init(pci_device_t* dev, uint32_t width, uint32_t height,
                 uint32_t *backing, uint32_t backing_phys);
bool virtio_gpu_present_full(void);
bool virtio_gpu_is_active(void);

#endif
