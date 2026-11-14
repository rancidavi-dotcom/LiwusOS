#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_ADDRESS 0xB8000

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

void vga_init(void);
void vga_putc(char c);
void vga_puts(const char* str);
extern void (*vga_puts_capture_hook)(const char *str);
extern void (*vga_putc_capture_hook)(char c);
void vga_clear(enum vga_color fg, enum vga_color bg);
void vga_set_color(enum vga_color fg, enum vga_color bg);
void vga_set_cursor(int x, int y);
void vga_set_cursor_pos(int x, int y);
void vga_show_cursor(int visible);
void vga_clear_line(int row);
void vga_clear_to_eol(void);
void vga_get_dimensions(size_t *cols, size_t *rows);

/* Low-level rendering primitives for the framebuffer backend. */
void vga_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void vga_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vga_draw_char_fb(char c, int x, int y, uint32_t fg, uint32_t bg);

bool vga_is_framebuffer(void);
uint32_t *vga_get_backbuffer(void);
uint32_t vga_get_fb_width(void);
uint32_t vga_get_fb_height(void);
uint32_t vga_get_fb_pitch(void);
void vga_flip(void);
void vga_clear_framebuffer(uint32_t xrgb);
int vga_blit_rgb32(const uint32_t *src, uint32_t src_w, uint32_t src_h);

/* Allocates a 24-bit BMP snapshot of the current RGB framebuffer with kmalloc. */
int vga_capture_bmp(uint8_t **out_data, uint32_t *out_size);

#endif
