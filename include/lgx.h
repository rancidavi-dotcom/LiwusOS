#ifndef LGX_H
#define LGX_H

#include <stdint.h>
#include <stdbool.h>

struct VideoMode {
    uint32_t* vram_address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch; // Bytes por linha física na tela
    uint8_t bpp;    // Bits por pixel (use 32-bit para ARGB)
};

// Flags para janelas
#define WIN_FLAG_TRANSPARENT 1
#define WIN_FLAG_NO_BORDER   2
#define WIN_FLAG_MOVABLE     4

typedef struct Window {
    uint32_t id;
    int x, y;
    int width, height;
    uint32_t* buffer; // Framebuffer privado reservado desta janela
    uint32_t flags;   // Ex: TRANSPARENT, NO_BORDER, MOVABLE
    struct Window* next;
    struct Window* prev;
} window_t;

void lgx_init(void);
void lgx_compositor_task(void);

// Milestone 2.1: Engine de Blitting
void graphics_blit(uint32_t* dest_buffer, int dest_pitch, int dest_x, int dest_y, 
                   uint32_t* src_buffer, int src_w, int src_h, int src_pitch, 
                   int src_x, int src_y, int width, int height);

// Milestone 3.2: Controle de Z-Order
window_t* window_create(int x, int y, int width, int height, uint32_t flags);
void window_bring_to_front(window_t* win);
void window_destroy(window_t* win);

extern struct VideoMode current_video_mode;

#endif
