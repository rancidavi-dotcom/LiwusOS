#ifndef VIDEO_H
#define VIDEO_H

#include "multiboot.h"
#include <stdbool.h>
#include <stdint.h>

extern uint32_t screen_width;
extern uint32_t screen_height;
extern uint32_t screen_size;
extern uint32_t *framebuffer;
extern uint32_t *backbuffer;

typedef struct {
  int x, y, w, h;
} rect_t;

void init_video(multiboot_info_t *mbi);
void draw_pixel(int x, int y, uint32_t color);
void draw_rect(int x, int y, int w, int h, uint32_t color);
void draw_char(int x, int y, char c, uint32_t color);
void draw_string(int x, int y, const char *str, uint32_t color);
void draw_mouse_cursor(int x, int y, int type);
void draw_filled_circle(int xm, int ym, int r, uint32_t color);
void draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
void draw_box_shadow(int x, int y, int w, int h, int r, int blur,
                     uint32_t color);
void draw_pixel_alpha(int x, int y, uint32_t color,
                      uint8_t alpha); // Alpha blending helper
void clear_screen(uint32_t color);
void refresh_screen();
void draw_loading_bar(int x, int y, int w, int h, int p);
void set_clip(int x, int y, int w, int h);
void reset_clip();

// Rendering Target API
void video_set_target(uint32_t *buffer, uint32_t width, uint32_t height);
void video_reset_target();
// Blit a surface to the current target with transparency (skipping 0x000000)
void video_blit(uint32_t *source, int src_pitch, int sx, int sy, int w, int h,
                int dx, int dy);
void video_refresh_rect(rect_t r);

#endif
