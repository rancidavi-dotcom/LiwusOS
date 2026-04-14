#include "virtio.h"
#include "io.h"
#include "kheap.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "vmm.h"
#include <stdbool.h>

static volatile uint8_t *virtio_common_cfg;
static volatile uint16_t *virtio_notify_reg;
static virtq_desc_t *virtio_desc;
static virtq_avail_t *virtio_avail;
static virtq_used_t *virtio_used;
static uint32_t virtio_resource_id = 1;
static uint32_t virtio_width;
static uint32_t virtio_height;
static bool virtio_gpu_ready;
static bool virtio_scanout_armed;
static bool virtio_logged_first_present;

static bool virtio_submit(void *req, uint32_t req_phys, uint32_t req_len,
                          void *resp, uint32_t resp_phys, uint32_t resp_len) {
  if (!virtio_gpu_ready || !virtio_desc || !virtio_avail || !virtio_used ||
      !virtio_notify_reg) {
    return false;
  }

  memset((void *)virtio_desc, 0, sizeof(virtq_desc_t) * 2);
  virtio_desc[0].addr = req_phys;
  virtio_desc[0].len = req_len;
  virtio_desc[0].flags = VIRTQ_DESC_F_NEXT;
  virtio_desc[0].next = 1;
  virtio_desc[1].addr = resp_phys;
  virtio_desc[1].len = resp_len;
  virtio_desc[1].flags = VIRTQ_DESC_F_WRITE;
  virtio_desc[1].next = 0;

  uint16_t start_used = virtio_used->idx;
  virtio_avail->ring[virtio_avail->idx % 64] = 0;
  virtio_avail->idx++;
  *virtio_notify_reg = 0;

  for (int i = 0; i < 5000000; i++) {
    if (virtio_used->idx != start_used) {
      return true;
    }
    asm volatile("pause");
  }

  return false;
}

static bool virtio_gpu_get_display_info(void) {
  uint32_t p_req, p_resp;
  virtio_gpu_ctrl_hdr_t *req =
      (virtio_gpu_ctrl_hdr_t *)kmalloc_ap(sizeof(*req), &p_req);
  virtio_gpu_resp_display_info_t *resp =
      (virtio_gpu_resp_display_info_t *)kmalloc_ap(sizeof(*resp), &p_resp);

  memset(req, 0, sizeof(*req));
  memset(resp, 0, sizeof(*resp));
  req->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

  if (!virtio_submit(req, p_req, sizeof(*req), resp, p_resp, sizeof(*resp))) {
    serial_print("VirtIO Error: GET_DISPLAY_INFO failed\n");
    return false;
  }

  serial_print("VirtIO: Display 0 mode ");
  serial_print_hex(resp->pmodes[0].r.width);
  serial_print("x");
  serial_print_hex(resp->pmodes[0].r.height);
  serial_print("\n");
  return true;
}

static bool virtio_gpu_create_resource(uint32_t width, uint32_t height) {
  uint32_t p_req, p_resp;
  virtio_gpu_resource_create_2d_t *req =
      (virtio_gpu_resource_create_2d_t *)kmalloc_ap(sizeof(*req), &p_req);
  virtio_gpu_ctrl_hdr_t *resp =
      (virtio_gpu_ctrl_hdr_t *)kmalloc_ap(sizeof(*resp), &p_resp);

  memset(req, 0, sizeof(*req));
  memset(resp, 0, sizeof(*resp));
  req->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
  req->resource_id = virtio_resource_id;
  req->format = VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM;
  req->width = width;
  req->height = height;

  if (!virtio_submit(req, p_req, sizeof(*req), resp, p_resp, sizeof(*resp))) {
    serial_print("VirtIO Error: RESOURCE_CREATE_2D failed\n");
    return false;
  }

  return true;
}

static bool virtio_gpu_attach_backing(uint32_t backing_phys,
                                      uint32_t backing_len) {
  uint32_t req_len = sizeof(virtio_gpu_resource_attach_backing_t) +
                     sizeof(virtio_gpu_mem_entry_t);
  uint32_t p_req, p_resp;
  uint8_t *blob = (uint8_t *)kmalloc_ap(req_len, &p_req);
  virtio_gpu_resource_attach_backing_t *req =
      (virtio_gpu_resource_attach_backing_t *)blob;
  virtio_gpu_mem_entry_t *entry =
      (virtio_gpu_mem_entry_t *)(blob + sizeof(*req));
  virtio_gpu_ctrl_hdr_t *resp =
      (virtio_gpu_ctrl_hdr_t *)kmalloc_ap(sizeof(*resp), &p_resp);

  memset(blob, 0, req_len);
  memset(resp, 0, sizeof(*resp));

  req->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
  req->resource_id = virtio_resource_id;
  req->nr_entries = 1;
  entry->addr = backing_phys;
  entry->length = backing_len;

  if (!virtio_submit(req, p_req, req_len, resp, p_resp, sizeof(*resp))) {
    serial_print("VirtIO Error: RESOURCE_ATTACH_BACKING failed\n");
    return false;
  }

  return true;
}

static bool virtio_gpu_set_scanout(uint32_t width, uint32_t height) {
  uint32_t p_req, p_resp;
  virtio_gpu_set_scanout_t *req =
      (virtio_gpu_set_scanout_t *)kmalloc_ap(sizeof(*req), &p_req);
  virtio_gpu_ctrl_hdr_t *resp =
      (virtio_gpu_ctrl_hdr_t *)kmalloc_ap(sizeof(*resp), &p_resp);

  memset(req, 0, sizeof(*req));
  memset(resp, 0, sizeof(*resp));
  req->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
  req->r.width = width;
  req->r.height = height;
  req->scanout_id = 0;
  req->resource_id = virtio_resource_id;

  if (!virtio_submit(req, p_req, sizeof(*req), resp, p_resp, sizeof(*resp))) {
    serial_print("VirtIO Error: SET_SCANOUT failed\n");
    return false;
  }

  return true;
}

bool virtio_init(pci_device_t *dev, uint32_t width, uint32_t height,
                 uint32_t *backing, uint32_t backing_phys) {
  serial_print("VirtIO: Initializing Modern (1.0) PCI Device...\n");

  uint32_t pci_cmd =
      pci_read_config(dev->bus, dev->device, dev->function, 0x04);
  pci_write_config(dev->bus, dev->device, dev->function, 0x04, pci_cmd | 0x07);

  uint8_t cap_ptr =
      pci_read_config(dev->bus, dev->device, dev->function, 0x34) & 0xFF;
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
      uint32_t d1 =
          pci_read_config(dev->bus, dev->device, dev->function, cap_ptr + 4);
      uint8_t bar = d1 & 0xFF;
      uint32_t offset =
          pci_read_config(dev->bus, dev->device, dev->function, cap_ptr + 8);
      uint32_t bar_val =
          pci_read_config(dev->bus, dev->device, dev->function, 0x10 + (bar * 4)) &
          ~0xF;

      if (type == VIRTIO_PCI_CAP_COMMON_CFG) {
        common_cfg_addr = bar_val + offset;
      } else if (type == VIRTIO_PCI_CAP_NOTIFY_CFG) {
        notify_cfg_addr = bar_val + offset;
        notify_off_multiplier = pci_read_config(dev->bus, dev->device,
                                                dev->function, cap_ptr + 12);
      }
    }

    cap_ptr = next;
  }

  if (!common_cfg_addr || !notify_cfg_addr) {
    serial_print("VirtIO Error: Missing mandatory capabilities!\n");
    return false;
  }

  vmm_map_page((void *)common_cfg_addr, (void *)common_cfg_addr, 0x1B);
  vmm_map_page((void *)notify_cfg_addr, (void *)notify_cfg_addr, 0x1B);
  virtio_common_cfg = (volatile uint8_t *)common_cfg_addr;

  *(volatile uint8_t *)(virtio_common_cfg + VIRTIO_REG_DEVICE_STATUS) = 0;
  *(volatile uint8_t *)(virtio_common_cfg + VIRTIO_REG_DEVICE_STATUS) =
      VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER;

  *(volatile uint32_t *)(virtio_common_cfg + VIRTIO_REG_DEVICE_FEATURES_SEL) = 1;
  uint32_t features_hi =
      *(volatile uint32_t *)(virtio_common_cfg + VIRTIO_REG_DEVICE_FEATURES);
  *(volatile uint32_t *)(virtio_common_cfg + VIRTIO_REG_DRIVER_FEATURES_SEL) = 1;
  *(volatile uint32_t *)(virtio_common_cfg + VIRTIO_REG_DRIVER_FEATURES) =
      (features_hi & 1);
  *(volatile uint8_t *)(virtio_common_cfg + VIRTIO_REG_DEVICE_STATUS) |=
      VIRTIO_STATUS_FEATURES_OK;

  *(volatile uint16_t *)(virtio_common_cfg + VIRTIO_REG_QUEUE_SEL) = 0;
  uint16_t queue_size =
      *(volatile uint16_t *)(virtio_common_cfg + VIRTIO_REG_QUEUE_SIZE);
  if (queue_size == 0) {
    serial_print("VirtIO Error: queue 0 unavailable\n");
    return false;
  }

  uint32_t p_desc, p_avail, p_used;
  void *v_desc = kmalloc_ap(4096, &p_desc);
  void *v_avail = kmalloc_ap(4096, &p_avail);
  void *v_used = kmalloc_ap(4096, &p_used);
  memset(v_desc, 0, 4096);
  memset(v_avail, 0, 4096);
  memset(v_used, 0, 4096);

  *(volatile uint32_t *)(virtio_common_cfg + VIRTIO_REG_QUEUE_DESC_LOW) = p_desc;
  *(volatile uint32_t *)(virtio_common_cfg + VIRTIO_REG_QUEUE_AVAIL_LOW) = p_avail;
  *(volatile uint32_t *)(virtio_common_cfg + VIRTIO_REG_QUEUE_USED_LOW) = p_used;
  *(volatile uint16_t *)(virtio_common_cfg + VIRTIO_REG_QUEUE_ENABLE) = 1;

  uint16_t notify_off =
      *(volatile uint16_t *)(virtio_common_cfg + VIRTIO_REG_QUEUE_NOTIFY_OFF);
  virtio_notify_reg = (volatile uint16_t *)(notify_cfg_addr +
                                            notify_off * notify_off_multiplier);
  virtio_desc = (virtq_desc_t *)v_desc;
  virtio_avail = (virtq_avail_t *)v_avail;
  virtio_used = (virtq_used_t *)v_used;
  virtio_width = width;
  virtio_height = height;
  virtio_gpu_ready = true;
  virtio_scanout_armed = false;
  virtio_logged_first_present = false;

  *(volatile uint8_t *)(virtio_common_cfg + VIRTIO_REG_DEVICE_STATUS) |=
      VIRTIO_STATUS_DRIVER_OK;

  if (!virtio_gpu_get_display_info()) {
    virtio_gpu_ready = false;
    return false;
  }
  if (!virtio_gpu_create_resource(width, height)) {
    virtio_gpu_ready = false;
    return false;
  }
  if (!virtio_gpu_attach_backing(backing_phys, width * height * 4)) {
    virtio_gpu_ready = false;
    return false;
  }

  (void)backing;
  serial_print("VirtIO: GPU resource/backing ready, waiting first present\n");
  return true;
}

bool virtio_gpu_present_full(void) {
  if (!virtio_gpu_ready) {
    return false;
  }

  uint32_t p_req, p_resp;
  virtio_gpu_transfer_to_host_2d_t *xfer =
      (virtio_gpu_transfer_to_host_2d_t *)kmalloc_ap(sizeof(*xfer), &p_req);
  virtio_gpu_ctrl_hdr_t *xfer_resp =
      (virtio_gpu_ctrl_hdr_t *)kmalloc_ap(sizeof(*xfer_resp), &p_resp);
  memset(xfer, 0, sizeof(*xfer));
  memset(xfer_resp, 0, sizeof(*xfer_resp));
  xfer->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
  xfer->r.width = virtio_width;
  xfer->r.height = virtio_height;
  xfer->resource_id = virtio_resource_id;

  if (!virtio_submit(xfer, p_req, sizeof(*xfer), xfer_resp, p_resp,
                     sizeof(*xfer_resp))) {
    if (!virtio_logged_first_present) {
      serial_print("VirtIO Error: first TRANSFER_TO_HOST_2D failed\n");
      virtio_logged_first_present = true;
    }
    return false;
  }

  uint32_t p_flush, p_flush_resp;
  virtio_gpu_resource_flush_t *flush =
      (virtio_gpu_resource_flush_t *)kmalloc_ap(sizeof(*flush), &p_flush);
  virtio_gpu_ctrl_hdr_t *flush_resp =
      (virtio_gpu_ctrl_hdr_t *)kmalloc_ap(sizeof(*flush_resp), &p_flush_resp);
  memset(flush, 0, sizeof(*flush));
  memset(flush_resp, 0, sizeof(*flush_resp));
  flush->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
  flush->r.width = virtio_width;
  flush->r.height = virtio_height;
  flush->resource_id = virtio_resource_id;

  if (!virtio_submit(flush, p_flush, sizeof(*flush), flush_resp, p_flush_resp,
                     sizeof(*flush_resp))) {
    if (!virtio_logged_first_present) {
      serial_print("VirtIO Error: first RESOURCE_FLUSH failed\n");
      virtio_logged_first_present = true;
    }
    return false;
  }

  if (!virtio_scanout_armed) {
    if (!virtio_gpu_set_scanout(virtio_width, virtio_height)) {
      if (!virtio_logged_first_present) {
        serial_print("VirtIO Error: first SET_SCANOUT failed\n");
        virtio_logged_first_present = true;
      }
      return false;
    }
    virtio_scanout_armed = true;
    serial_print("VirtIO: first desktop frame presented, scanout armed\n");
  } else if (!virtio_logged_first_present) {
    serial_print("VirtIO: first desktop frame transferred\n");
  }

  virtio_logged_first_present = true;

  return true;
}

bool virtio_gpu_is_active(void) { return virtio_gpu_ready; }
