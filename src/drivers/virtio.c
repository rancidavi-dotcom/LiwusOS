#include "virtio.h"
#include "io.h"
#include "pci.h"
#include "serial.h"
#include "vmm.h"
#include "kheap.h"
#include "string.h"

void virtio_init(pci_device_t* dev) {
    serial_print("VirtIO: Initializing Modern (1.0) PCI Device...\n");

    // 1. Enable PCI Bus Master
    uint32_t pci_cmd = pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, pci_cmd | 0x07);

    // 2. Find Capabilities
    uint8_t cap_ptr = pci_read_config(dev->bus, dev->device, dev->function, 0x34) & 0xFF;
    uint32_t common_cfg_addr = 0;
    uint32_t notify_cfg_addr = 0;
    uint32_t notify_off_multiplier = 0;

    serial_print("VirtIO: Scanning PCI Capabilities...\n");
    while (cap_ptr != 0) {
        uint32_t d0 = pci_read_config(dev->bus, dev->device, dev->function, cap_ptr);
        uint8_t cap_id = d0 & 0xFF;
        uint8_t next = (d0 >> 8) & 0xFF;
        uint8_t type = (d0 >> 24) & 0xFF;

        if (cap_id == 0x09) {
            uint32_t d1 = pci_read_config(dev->bus, dev->device, dev->function, cap_ptr + 4);
            uint8_t bar = d1 & 0xFF;
            uint32_t offset = pci_read_config(dev->bus, dev->device, dev->function, cap_ptr + 8);
            uint32_t bar_val = pci_read_config(dev->bus, dev->device, dev->function, 0x10 + (bar * 4)) & ~0xF;

            if (type == VIRTIO_PCI_CAP_COMMON_CFG) {
                common_cfg_addr = bar_val + offset;
            } else if (type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
                notify_cfg_addr = bar_val + offset;
                notify_off_multiplier = pci_read_config(dev->bus, dev->device, dev->function, cap_ptr + 12);
            }
        }
        cap_ptr = next;
    }

    if (!common_cfg_addr || !notify_cfg_addr) {
        serial_print("VirtIO Error: Missing mandatory capabilities!\n");
        return;
    }

    // 3. Map Common Config
    vmm_map_page((void*)common_cfg_addr, (void*)common_cfg_addr, 0x1B);
    vmm_map_page((void*)notify_cfg_addr, (void*)notify_cfg_addr, 0x1B);
    volatile uint8_t* base = (uint8_t*)common_cfg_addr;

    // 4. Reset & Status
    *(volatile uint8_t*)(base + VIRTIO_REG_DEVICE_STATUS) = 0;
    *(volatile uint8_t*)(base + VIRTIO_REG_DEVICE_STATUS) = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;

    // 5. Features (Accept Version 1)
    *(volatile uint32_t*)(base + VIRTIO_REG_DEVICE_FEATURES_SEL) = 1;
    uint32_t f_hi = *(volatile uint32_t*)(base + VIRTIO_REG_DEVICE_FEATURES);
    *(volatile uint32_t*)(base + VIRTIO_REG_DRIVER_FEATURES_SEL) = 1;
    *(volatile uint32_t*)(base + VIRTIO_REG_DRIVER_FEATURES) = (f_hi & 1); // Version 1 bit

    *(volatile uint8_t*)(base + VIRTIO_REG_DEVICE_STATUS) |= VIRTIO_STATUS_FEATURES_OK;

    // 6. Setup Queue 0
    *(volatile uint16_t*)(base + VIRTIO_REG_QUEUE_SEL) = 0;
    uint16_t q_size = *(volatile uint16_t*)(base + VIRTIO_REG_QUEUE_SIZE);
    
    uint32_t p_desc, p_avail, p_used;
    void* v_desc = kmalloc_ap(4096, &p_desc);
    void* v_avail = kmalloc_ap(4096, &p_avail);
    void* v_used = kmalloc_ap(4096, &p_used);
    memset(v_desc, 0, 4096); memset(v_avail, 0, 4096); memset(v_used, 0, 4096);

    *(volatile uint32_t*)(base + VIRTIO_REG_QUEUE_DESC_LOW) = p_desc;
    *(volatile uint32_t*)(base + VIRTIO_REG_QUEUE_AVAIL_LOW) = p_avail;
    *(volatile uint32_t*)(base + VIRTIO_REG_QUEUE_USED_LOW) = p_used;
    *(volatile uint16_t*)(base + VIRTIO_REG_QUEUE_ENABLE) = 1;

    uint16_t notify_off = *(volatile uint16_t*)(base + VIRTIO_REG_QUEUE_NOTIFY_OFF);
    volatile uint16_t* notify_reg = (uint16_t*)(notify_cfg_addr + notify_off * notify_off_multiplier);

    // 7. Go Live
    *(volatile uint8_t*)(base + VIRTIO_REG_DEVICE_STATUS) |= VIRTIO_STATUS_DRIVER_OK;
    serial_print("VirtIO: GPU Ready. Sending command via DMA...\n");

    // 8. Command GET_DISPLAY_INFO
    uint32_t p_req, p_resp;
    virtio_gpu_ctrl_hdr_t* v_req = (virtio_gpu_ctrl_hdr_t*)kmalloc_ap(sizeof(virtio_gpu_ctrl_hdr_t), &p_req);
    virtio_gpu_resp_display_info_t* v_resp = (virtio_gpu_resp_display_info_t*)kmalloc_ap(sizeof(virtio_gpu_resp_display_info_t), &p_resp);
    
    memset(v_req, 0, sizeof(virtio_gpu_ctrl_hdr_t));
    v_req->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    virtq_desc_t* d = (virtq_desc_t*)v_desc;
    d[0].addr = p_req; d[0].len = sizeof(virtio_gpu_ctrl_hdr_t); d[0].flags = VIRTQ_DESC_F_NEXT; d[0].next = 1;
    d[1].addr = p_resp; d[1].len = sizeof(virtio_gpu_resp_display_info_t); d[1].flags = VIRTQ_DESC_F_WRITE; d[1].next = 0;

    virtq_avail_t* a = (virtq_avail_t*)v_avail;
    a->ring[a->idx % 64] = 0;
    a->idx++;

    serial_print("VirtIO: Notifying GPU via register "); serial_print_hex((uint32_t)notify_reg); serial_print("\n");
    *notify_reg = 0; // Notify Queue 0

    serial_print("VirtIO: Waiting for response...\n");
    virtq_used_t* u = (virtq_used_t*)v_used;
    for(int i=0; i<5000000; i++) {
        if (u->idx > 0) break;
        asm volatile("pause");
    }

    if (u->idx > 0) {
        serial_print("VirtIO: SUCCESS! Display 0: ");
        serial_print_hex(v_resp->pmodes[0].r.width); serial_print("x");
        serial_print_hex(v_resp->pmodes[0].r.height); serial_print("\n");
    } else {
        serial_print("VirtIO Error: Still no response. Check DMA alignment.\n");
    }
}
