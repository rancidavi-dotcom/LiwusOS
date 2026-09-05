#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>

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
void vga_clear(enum vga_color fg, enum vga_color bg);
void vga_set_color(enum vga_color fg, enum vga_color bg);
void vga_set_cursor(int x, int y);
void vga_set_cursor_pos(int x, int y);
void vga_show_cursor(int visible);
void vga_clear_line(int row);
void vga_clear_to_eol(void);

void vga_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void vga_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void vga_draw_char_scaled(uint32_t x, uint32_t y, char c, uint32_t color, int scale);

#endif
