#include "gpu.h"
#include "io.h"
#include "pci.h"
#include "string.h"
#include "video.h" // Fallback refs

static uint32_t gpu_width = 1024;
static uint32_t gpu_height = 768;
static uint32_t *gpu_lfb_addr = 0;

static void bga_write_register(uint16_t index, uint16_t value) {
  outw(VBE_DISPI_IOPORT_INDEX, index);
  outw(VBE_DISPI_IOPORT_DATA, value);
}

static uint16_t bga_read_register(uint16_t index) {
  outw(VBE_DISPI_IOPORT_INDEX, index);
  return inw(VBE_DISPI_IOPORT_DATA);
}

void init_gpu() {
  // 1. Detect BGA on PCI (Vendor 0x1234, Device 0x1111)

  // Check ID first via Ports (Sometimes faster/present even if PCI enumeration
  // fails logic)
  uint16_t id = bga_read_register(VBE_DISPI_INDEX_ID);
  if (id < 0xB0C0 || id > 0xB0C6) {
    // Not BGA compatible
    return;
  }

  // 2. Set Mode
  gpu_set_resolution(1024, 768);

  // 3. Get LFB Address from PCI
  // Scan PCI bus for device 0x1111:0x1234 -> BAR0 is LFB
  pci_device_t *dev = pci_get_device(0x1234, 0x1111);

  if (dev) {
    // Read BAR0 (Offset 0x10) manually
    uint32_t bar0 = pci_read_config(dev->bus, dev->device, dev->function, 0x10);
    gpu_lfb_addr =
        (uint32_t *)(bar0 & 0xFFFFFFF0); // Mask low bits (type/prefetch)
  } else {
    // Fallback: If PCI scan failed, assume Standard QEMU/Bochs LFB @ 0xE0000000
    // is common, BUT safer to use what Multiboot gave us in video.c if
    // accessible.
    extern uint32_t *framebuffer;
    gpu_lfb_addr = framebuffer;
  }
}

void gpu_set_resolution(uint32_t width, uint32_t height) {
  bga_write_register(VBE_DISPI_INDEX_ENABLE, VBE_DISPI_DISABLED);
  bga_write_register(VBE_DISPI_INDEX_XRES, width);
  bga_write_register(VBE_DISPI_INDEX_YRES, height);
  bga_write_register(VBE_DISPI_INDEX_BPP, 32);
  bga_write_register(VBE_DISPI_INDEX_ENABLE,
                     VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);

  gpu_width = width;
  gpu_height = height;
}

uint32_t *gpu_get_lfb() { return gpu_lfb_addr; }

// "GPU" Fill Rect - Optimized MMIO
void gpu_fill_rect(int x, int y, int w, int h, uint32_t color) {
  if (!gpu_lfb_addr)
    return;

  uint32_t *dst = gpu_lfb_addr;
  uint32_t pitch = gpu_width;

  for (int r = 0; r < h; r++) {
    uint32_t *row = &dst[(y + r) * pitch + x];
    for (int c = 0; c < w; c++)
      row[c] = color;
  }
}

// "GPU" Blit - Host to Device transfer
void gpu_blit(uint32_t *src, int x, int y, int w, int h) {
  if (!gpu_lfb_addr)
    return;

  uint32_t *dst = gpu_lfb_addr;
  uint32_t pitch = gpu_width;

  for (int r = 0; r < h; r++) {
    uint32_t *dst_row = &dst[(y + r) * pitch + x];
    uint32_t *src_row = &src[r * w];
    memcpy(dst_row, src_row, w * 4); // Fast Block Transfer
  }
}
