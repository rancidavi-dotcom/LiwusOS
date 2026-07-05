#ifndef GPU_H
#define GPU_H

#include <stdbool.h>
#include <stdint.h>

// BGA Definitions
#define VBE_DISPI_IOPORT_INDEX 0x01CE
#define VBE_DISPI_IOPORT_DATA 0x01CF
#define VBE_DISPI_INDEX_ID 0
#define VBE_DISPI_INDEX_XRES 1
#define VBE_DISPI_INDEX_YRES 2
#define VBE_DISPI_INDEX_BPP 3
#define VBE_DISPI_INDEX_ENABLE 4
#define VBE_DISPI_INDEX_BANK 5
#define VBE_DISPI_INDEX_VIRT_WIDTH 6
#define VBE_DISPI_INDEX_VIRT_HEIGHT 7
#define VBE_DISPI_INDEX_X_OFFSET 8
#define VBE_DISPI_INDEX_Y_OFFSET 9

#define VBE_DISPI_DISABLED 0x00
#define VBE_DISPI_ENABLED 0x01
#define VBE_DISPI_LFB_ENABLED 0x40

void init_gpu(void);
void gpu_setup_wc_mtrr(uint64_t fb_phys, uint64_t fb_size);
uint16_t gpu_get_vendor(void);
const char *gpu_get_vendor_name(void);

// SSE2 fast memcpy (non-temporal stores for VRAM)
extern void fast_memcpy(void *dst, const void *src, uint64_t size);

#endif
