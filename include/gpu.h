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

void init_gpu();
void gpu_set_resolution(uint32_t width, uint32_t height);
void gpu_swap_buffers(); // Hardware page flip if supported or blit
uint32_t *gpu_get_lfb(); // Get Linear Framebuffer address

// Hardware Accelerated 2D Ops (Simulated or Real BGA extensions)
void gpu_fill_rect(int x, int y, int w, int h, uint32_t color);
void gpu_blit(uint32_t *src, int x, int y, int w, int h); // Host to Device

#endif
