#include <lgx.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <libliw.h>

static inline int syscall1(int num, int arg1) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1) : "memory");
    return ret;
}

static inline int syscall3(int num, int arg1, int arg2, int arg3) {
    int ret;
    __asm__ volatile ("int $0x80" : "=a"(ret) : "a"(num), "b"(arg1), "c"(arg2), "d"(arg3) : "memory");
    return ret;
}

extern void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);

lgx_window_t* lgx_init(int width, int height, int flags) {
    lgx_window_t *win = malloc(sizeof(lgx_window_t));
    if (!win) return NULL;
    
    // Syscall 120: Create Window
    int win_id = syscall3(120, width, height, flags);
    if (win_id < 0) {
        free(win);
        return NULL;
    }
    
    // Syscall 121: Map Window Buffer
    void *buf = (void*)syscall1(121, win_id);
    if (!buf || buf == (void*)-1) {
        // Syscall 122: Destroy Window
        syscall1(122, win_id);
        free(win);
        return NULL;
    }
    
    win->id = win_id;
    win->width = width;
    win->height = height;
    win->buffer = (uint32_t*)buf;
    return win;
}

void lgx_refresh(lgx_window_t *win) {
    // Syscall 123: Refresh window (Opicional, no LiwusOS o refresh é automático no compositor)
    // Mas futuramente podemos implementar Dirty Rectangles.
}

void lgx_close(lgx_window_t *win) {
    if (win) {
        syscall1(122, win->id);
        // O mmap seria unmapped pelo kernel ao destruir a janela
        free(win);
    }
}

void lgx_clear(lgx_window_t *win, uint32_t color) {
    for (int i = 0; i < win->width * win->height; i++) {
        win->buffer[i] = color;
    }
}

void lgx_draw_rect(lgx_window_t *win, int x, int y, int w, int h, uint32_t color) {
    for (int ty = 0; ty < h; ty++) {
        int screen_y = y + ty;
        if (screen_y < 0 || screen_y >= win->height) continue;
        for (int tx = 0; tx < w; tx++) {
            int screen_x = x + tx;
            if (screen_x < 0 || screen_x >= win->width) continue;
            win->buffer[screen_y * win->width + screen_x] = color;
        }
    }
}

void lgx_draw_bitmap(lgx_window_t *win, int x, int y, const uint32_t *bitmap, int w, int h) {
    for (int ty = 0; ty < h; ty++) {
        int screen_y = y + ty;
        if (screen_y < 0 || screen_y >= win->height) continue;
        for (int tx = 0; tx < w; tx++) {
            int screen_x = x + tx;
            if (screen_x < 0 || screen_x >= win->width) continue;
            
            uint32_t fg = bitmap[ty * w + tx];
            // Alpha Blend simplificado (já que liblgx roda no user space)
            uint32_t a = (fg >> 24) & 0xFF;
            if (a == 255) {
                win->buffer[screen_y * win->width + screen_x] = fg;
            } else if (a > 0) {
                uint32_t bg = win->buffer[screen_y * win->width + screen_x];
                uint32_t inv_a = 255 - a;
                uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv_a) / 255;
                uint32_t g = (((fg >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * inv_a) / 255;
                uint32_t b = ((fg & 0xFF) * a + (bg & 0xFF) * inv_a) / 255;
                win->buffer[screen_y * win->width + screen_x] = (0xFF000000) | (r << 16) | (g << 8) | b;
            }
        }
    }
}
