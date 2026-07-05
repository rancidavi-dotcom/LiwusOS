#ifndef LGX_LIB_H
#define LGX_LIB_H

#include <stdint.h>

#define LGX_WIN_NO_BORDER 1
#define LGX_WIN_NO_DRAG   2

typedef struct {
    int id;
    int width;
    int height;
    uint32_t *buffer;
} lgx_window_t;

// Window Management
lgx_window_t* lgx_init(int width, int height, int flags);
void lgx_refresh(lgx_window_t *win);
void lgx_close(lgx_window_t *win);

// Drawing Primitives
void lgx_clear(lgx_window_t *win, uint32_t color);
void lgx_draw_rect(lgx_window_t *win, int x, int y, int w, int h, uint32_t color);
void lgx_draw_text(lgx_window_t *win, int x, int y, const char *text, uint32_t color);
void lgx_draw_bitmap(lgx_window_t *win, int x, int y, const uint32_t *bitmap, int w, int h);

#endif
