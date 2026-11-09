#include "vga.h"
#include "io.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "vmm.h"
#include "kernel_layout.h"
#include <stdbool.h>

extern void fast_memcpy(void *dst, const void *src, uint64_t size);

uint64_t vga_fb_addr = 0;
uint32_t vga_fb_width = 0;
uint32_t vga_fb_height = 0;
uint32_t vga_fb_pitch = 0;
uint8_t vga_fb_bpp = 0;
uint8_t vga_fb_type = 0;
uint8_t vga_fb_red_pos = 0;
uint8_t vga_fb_red_size = 0;
uint8_t vga_fb_green_pos = 0;
uint8_t vga_fb_green_size = 0;
uint8_t vga_fb_blue_pos = 0;
uint8_t vga_fb_blue_size = 0;

static bool is_framebuffer = false;
static uint32_t *vga_backbuffer = NULL;

// Fallback text mode buffer
static uint16_t *text_buffer;

static size_t vga_row = 0;
static size_t vga_column = 0;
static uint8_t vga_color = 0;
static uint32_t vga_fg_color = 0xFFFFFF; // White
static uint32_t vga_bg_color = 0x000000; // Black

// Font handling (PSF1)
extern uint8_t _binary_src_drivers_font_psf_start[];
extern char _binary_src_drivers_font_psf_end;
static uint8_t* font_data = NULL;
static int font_width = 8;
static int font_height = 16;
static int font_bpg = 16; // bytes per glyph

// Columns and rows available
static size_t max_cols = 80;
static size_t max_rows = 25;

// ANSI escape sequence parser state
static int vga_esc_state = 0; 
static int vga_esc_params[16];
static int vga_esc_pcount;
static int vga_esc_val;

static int vga_ansi_fg = 7;
static int vga_ansi_bg = 0;
static bool vga_reverse = false;

static const uint32_t vga_palette[16] = {
    0xCC0B0F19, // 0: Black (Glassmorphic Midnight Slate: 80% opacity)
    0xFF3B82F6, // 1: Blue (Vibrant Indigo/Blue 500)
    0xFF10B981, // 2: Emerald Green 500
    0xFF06B6D4, // 3: Cyan 500
    0xFFEF4444, // 4: Rose/Red 500
    0xFFD946EF, // 5: Fuchsia 500
    0xFFF59E0B, // 6: Amber 500
    0xFFCBD5E1, // 7: Light Grey (Slate 300)
    0xFF64748B, // 8: Dark Grey (Slate 500)
    0xFF60A5FA, // 9: Light Blue 400
    0xFF34D399, // 10: Light Emerald 400
    0xFF22D3EE, // 11: Light Cyan 400
    0xFFF87171, // 12: Light Red 400
    0xFFE879F9, // 13: Light Fuchsia 400
    0xFFFBBF24, // 14: Light Amber 400
    0xFFF8FAFC, // 15: White (Slate 50)
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

static inline uint32_t vga_convert_color(uint32_t color) {
    uint32_t red = (color >> 16) & 0xFF;
    uint32_t green = (color >> 8) & 0xFF;
    uint32_t blue = color & 0xFF;
    return (red << vga_fb_red_pos) | (green << vga_fb_green_pos) |
           (blue << vga_fb_blue_pos);
}

void vga_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!is_framebuffer) return;
    if (x >= vga_fb_width || y >= vga_fb_height) return;
    uint32_t px = vga_convert_color(color);
    uint32_t stride_words = vga_fb_pitch / 4;
    uint32_t offset = y * stride_words + x;
    uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);
    fb[offset] = px;
    if (vga_backbuffer) {
        vga_backbuffer[offset] = px;
    }
}

void vga_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    if (!is_framebuffer) return;
    if (x >= vga_fb_width || y >= vga_fb_height) return;
    if (x + w > vga_fb_width) w = vga_fb_width - x;
    if (y + h > vga_fb_height) h = vga_fb_height - y;
    if (!w || !h) return;

    uint32_t px = vga_convert_color(color);
    uint32_t stride_words = vga_fb_pitch / 4;
    uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);
    uint32_t* dst = fb + (y * stride_words) + x;
    uint32_t* back_dst = vga_backbuffer ? (vga_backbuffer + (y * stride_words) + x) : NULL;

    for (uint32_t j = 0; j < h; j++) {
        for (uint32_t i = 0; i < w; i++) {
            dst[i] = px;
        }
        if (back_dst) {
            for (uint32_t i = 0; i < w; i++) {
                back_dst[i] = px;
            }
            back_dst += stride_words;
        }
        dst += stride_words;
    }
}

static void vga_draw_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg) {
    if (!is_framebuffer || !font_data) return;
    if (x + (uint32_t)font_width > vga_fb_width || y + (uint32_t)font_height > vga_fb_height) return;

    uint8_t* glyph = font_data + (unsigned char)c * font_bpg;
    uint32_t fg_px = vga_convert_color(fg);
    uint32_t bg_px = vga_convert_color(bg);
    uint32_t stride_words = vga_fb_pitch / 4;
    uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);
    uint32_t* dst = fb + (y * stride_words) + x;
    uint32_t* back_dst = vga_backbuffer ? (vga_backbuffer + (y * stride_words) + x) : NULL;

    for (int j = 0; j < font_height; j++) {
        uint8_t row = glyph[j];
        uint32_t p0 = (row & 0x80) ? fg_px : bg_px;
        uint32_t p1 = (row & 0x40) ? fg_px : bg_px;
        uint32_t p2 = (row & 0x20) ? fg_px : bg_px;
        uint32_t p3 = (row & 0x10) ? fg_px : bg_px;
        uint32_t p4 = (row & 0x08) ? fg_px : bg_px;
        uint32_t p5 = (row & 0x04) ? fg_px : bg_px;
        uint32_t p6 = (row & 0x02) ? fg_px : bg_px;
        uint32_t p7 = (row & 0x01) ? fg_px : bg_px;

        dst[0] = p0; dst[1] = p1; dst[2] = p2; dst[3] = p3;
        dst[4] = p4; dst[5] = p5; dst[6] = p6; dst[7] = p7;

        if (back_dst) {
            back_dst[0] = p0; back_dst[1] = p1; back_dst[2] = p2; back_dst[3] = p3;
            back_dst[4] = p4; back_dst[5] = p5; back_dst[6] = p6; back_dst[7] = p7;
            back_dst += stride_words;
        }
        dst += stride_words;
    }
}

static void vga_update_color(void) {
    int fg = vga_ansi_fg;
    int bg = vga_ansi_bg;
    if (vga_reverse) {
        int tmp = fg;
        fg = bg;
        bg = tmp;
    }
    vga_color = vga_entry_color((enum vga_color)fg, (enum vga_color)bg);
    vga_fg_color = vga_palette[fg];
    vga_bg_color = vga_palette[bg];
}

static void vga_sgr_reset(void) {
    vga_ansi_fg = 7;
    vga_ansi_bg = 0;
    vga_reverse = false;
    vga_update_color();
}

static void vga_exec_esc(void) {
    // simplified for brevity... (same logic as before, using max_rows/max_cols)
    if (vga_esc_pcount == 0) {
        vga_esc_params[0] = 0;
        vga_esc_pcount = 1;
    }
    char cmd = (char)vga_esc_val;
    if (cmd == 'H' || cmd == 'f') {
        int row = vga_esc_params[0] - 1;
        int col = (vga_esc_pcount > 1 ? vga_esc_params[1] : 1) - 1;
        if (row < 0) row = 0;
        if (col < 0) col = 0;
        if ((size_t)row >= max_rows) row = max_rows - 1;
        if ((size_t)col >= max_cols) col = max_cols - 1;
        vga_row = (size_t)row;
        vga_column = (size_t)col;
        vga_set_cursor((int)vga_column, (int)vga_row);
    } else if (cmd == 'J') {
        int mode = vga_esc_params[0];
        if (mode == 2) {
            vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }
    } else if (cmd == 'C') {
        int n = vga_esc_params[0];
        if (n == 0) n = 1;
        vga_column += n;
        if (vga_column >= max_cols) vga_column = max_cols - 1;
        vga_set_cursor((int)vga_column, (int)vga_row);
    } else if (cmd == 'D') {
        int n = vga_esc_params[0];
        if (n == 0) n = 1;
        if (vga_column >= (size_t)n) vga_column -= n;
        else vga_column = 0;
        vga_set_cursor((int)vga_column, (int)vga_row);
    } else if (cmd == 'K') {
        int mode = vga_esc_params[0];
        if (mode == 0 || mode == 2) {
            for (size_t x = (mode == 2 ? 0 : vga_column); x < max_cols; x++) {
                if (is_framebuffer) {
                    vga_draw_rect(x * font_width, vga_row * font_height, font_width, font_height, vga_bg_color);
                } else {
                    text_buffer[vga_row * max_cols + x] = vga_entry(' ', vga_color);
                }
            }
        }
    } else if (cmd == 'm') {
        for (int i = 0; i < vga_esc_pcount; i++) {
            int p = vga_esc_params[i];
            if (p == 0) {
                vga_sgr_reset();
            } else if (p == 7) {
                vga_reverse = true;
                vga_update_color();
            } else if (p == 27) {
                vga_reverse = false;
                vga_update_color();
            } else if (p >= 30 && p <= 37) {
                vga_ansi_fg = p - 30;
                vga_update_color();
            } else if (p >= 40 && p <= 47) {
                vga_ansi_bg = p - 40;
                vga_update_color();
            } else if (p >= 90 && p <= 97) {
                vga_ansi_fg = p - 90 + 8;
                vga_update_color();
            } else if (p >= 100 && p <= 107) {
                vga_ansi_bg = p - 100 + 8;
                vga_update_color();
            } else if (p == 39) {
                vga_ansi_fg = 7;
                vga_update_color();
            } else if (p == 49) {
                vga_ansi_bg = 0;
                vga_update_color();
            }
        }
    }
}

static void vga_handle_esc_char(char c) {
    if (vga_esc_state == 1) {
        if (c == '[') {
            vga_esc_state = 2;
            vga_esc_pcount = 0;
            vga_esc_val = 0;
        } else {
            vga_esc_state = 0;
        }
        return;
    }

    if (vga_esc_state == 3) {
        if (c >= '0' && c <= '9') {
            vga_esc_val = vga_esc_val * 10 + (c - '0');
        } else if (c == 'h' || c == 'l') {
            vga_esc_state = 0;
        } else {
            vga_esc_state = 0;
        }
        return;
    }

    if (vga_esc_state == 2) {
        if (c == '?') {
            vga_esc_state = 3;
            vga_esc_val = 0;
            return;
        }

        if (c >= '0' && c <= '9') {
            vga_esc_val = vga_esc_val * 10 + (c - '0');
            return;
        }

        if (c == ';') {
            if (vga_esc_pcount < 16) {
                vga_esc_params[vga_esc_pcount++] = vga_esc_val;
            }
            vga_esc_val = 0;
            return;
        }

        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '~') {
            if (vga_esc_pcount < 16) {
                vga_esc_params[vga_esc_pcount++] = vga_esc_val;
            }
            vga_esc_val = c;
            vga_exec_esc();
            vga_esc_state = 0;
            return;
        }

        vga_esc_state = 0;
        return;
    }

    vga_esc_state = 0;
}

void vga_set_cursor(int x, int y) {
    if (is_framebuffer) {
        // No hardware cursor in framebuffer mode for now.
        // We could draw an underscore if we want.
    } else {
        uint16_t pos = y * max_cols + x;
        outb(0x3D4, 0x0F);
        outb(0x3D5, (uint8_t)(pos & 0xFF));
        outb(0x3D4, 0x0E);
        outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
    }
}

void vga_set_cursor_pos(int x, int y) {
    vga_column = (size_t)x;
    vga_row = (size_t)y;
    vga_set_cursor(x, y);
}

void vga_show_cursor(int visible) {
    if (!is_framebuffer) {
        if (visible) {
            outb(0x3D4, 0x0A);
            outb(0x3D5, 0x0E);
            outb(0x3D4, 0x0B);
            outb(0x3D5, 0x0F);
        } else {
            outb(0x3D4, 0x0A);
            outb(0x3D5, 0x20);
        }
    }
}

void vga_clear_line(int row) {
    if (row < 0 || (size_t)row >= max_rows) return;
    for (size_t x = 0; x < max_cols; x++) {
        if (is_framebuffer) {
            vga_draw_rect(x * font_width, row * font_height, font_width, font_height, vga_bg_color);
        } else {
            text_buffer[(size_t)row * max_cols + x] = vga_entry(' ', vga_color);
        }
    }
}

void vga_clear_to_eol(void) {
    for (size_t x = vga_column; x < max_cols; x++) {
        if (is_framebuffer) {
            vga_draw_rect(x * font_width, vga_row * font_height, font_width, font_height, vga_bg_color);
        } else {
            text_buffer[vga_row * max_cols + x] = vga_entry(' ', vga_color);
        }
    }
}

void vga_init(void) {
    text_buffer = (uint16_t *)phys_to_virt(0xB8000);
    is_framebuffer = false;
    max_cols = 80;
    max_rows = 25;

    /* UEFI machines commonly have no usable VGA text memory. Keep the UI
     * text-only, but render those characters into GRUB's RGB framebuffer. */
    if (vga_fb_addr && vga_fb_type == 1 && vga_fb_bpp == 32 &&
        vga_fb_red_size == 8 && vga_fb_green_size == 8 &&
        vga_fb_blue_size == 8 && vga_fb_red_pos <= 24 &&
        vga_fb_green_pos <= 24 && vga_fb_blue_pos <= 24 &&
        vga_fb_width >= 80 * 8 &&
        vga_fb_height >= 25 * 16 && vga_fb_pitch >= vga_fb_width * 4U &&
        vga_fb_addr < DIRECT_MAP_SIZE &&
        (uint64_t)vga_fb_pitch * vga_fb_height <=
            DIRECT_MAP_SIZE - vga_fb_addr) {
        uint8_t *font_hdr = _binary_src_drivers_font_psf_start;
        if (font_hdr[0] == 0x36 && font_hdr[1] == 0x04 && font_hdr[3] == 16) {
            font_data = font_hdr + 4;
            font_bpg = font_hdr[3];
            uint64_t fb_size = (uint64_t)vga_fb_pitch * vga_fb_height;
            vmm_map_framebuffer(vga_fb_addr, fb_size);
            is_framebuffer = true;
            max_cols = vga_fb_width / (size_t)font_width;
            max_rows = vga_fb_height / (size_t)font_height;
            vga_backbuffer = (uint32_t *)kmalloc(fb_size);
            if (vga_backbuffer) {
                memset(vga_backbuffer, 0, fb_size);
            }
            serial_print("[console] text framebuffer active\n");
        }
    }

    if (!is_framebuffer) {
        vmm_map_page_in(kernel_directory, (void *)0xB8000,
                        phys_to_virt(0xB8000),
                        PTE_P | PTE_W | PTE_PCD | PTE_NX);
        serial_print("[console] VGA text fallback active\n");
    }
    
    vga_row = 0;
    vga_column = 0;
    vga_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_fg_color = vga_palette[7];
    vga_bg_color = vga_palette[0];
    
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void vga_set_color(enum vga_color fg, enum vga_color bg) {
    vga_color = vga_entry_color(fg, bg);
    vga_fg_color = vga_palette[fg];
    vga_bg_color = vga_palette[bg];
}

void vga_putentryat(char c, uint8_t color, size_t x, size_t y) {
    if (is_framebuffer) {
        vga_draw_char(c, x * font_width, y * font_height, vga_fg_color, vga_bg_color);
    } else {
        const size_t index = y * max_cols + x;
        text_buffer[index] = vga_entry(c, color);
    }
}

void vga_scroll() {
    if (is_framebuffer) {
        uint32_t stride_words = vga_fb_pitch / 4;
        uint32_t row_words = stride_words * (uint32_t)font_height;
        uint32_t total_scroll_words =
            stride_words * (uint32_t)((max_rows - 1) * font_height);
        uint32_t* fb = (uint32_t*)phys_to_virt(vga_fb_addr);

        if (vga_backbuffer) {
            memmove(vga_backbuffer, vga_backbuffer + row_words,
                    total_scroll_words * sizeof(uint32_t));
            uint32_t bg_px = vga_convert_color(vga_bg_color);
            uint32_t *bottom = vga_backbuffer + total_scroll_words;
            for (uint32_t i = 0; i < row_words; i++) {
                bottom[i] = bg_px;
            }
            fast_memcpy(fb, vga_backbuffer,
                        (total_scroll_words + row_words) * sizeof(uint32_t));
        } else {
            memmove(fb, fb + row_words, total_scroll_words * 4);
            vga_draw_rect(0, (max_rows - 1) * font_height, vga_fb_width, font_height, vga_bg_color);
        }
    } else {
        for (size_t y = 0; y < max_rows - 1; y++) {
            for (size_t x = 0; x < max_cols; x++) {
                text_buffer[y * max_cols + x] = text_buffer[(y + 1) * max_cols + x];
            }
        }
        for (size_t x = 0; x < max_cols; x++) {
            text_buffer[(max_rows - 1) * max_cols + x] = vga_entry(' ', vga_color);
        }
    }
}

void vga_putc(char c) {
    if (vga_esc_state > 0) {
        vga_handle_esc_char(c);
        return;
    }
    if (c == '\x1b') {
        vga_esc_state = 1;
        vga_esc_pcount = 0;
        vga_esc_val = 0;
        return;
    }

    if (c == '\n') {
        vga_column = 0;
        if (++vga_row >= max_rows) {
            vga_scroll();
            vga_row = max_rows - 1;
        }
    } else if (c == '\r') {
        vga_column = 0;
    } else if (c == '\t') {
        vga_column += 4;
        if (vga_column >= max_cols) {
            vga_column = 0;
            if (++vga_row >= max_rows) {
                vga_scroll();
                vga_row = max_rows - 1;
            }
        }
    } else if (c == '\b') {
        if (vga_column > 0) {
            vga_column--;
            vga_putentryat(' ', vga_color, vga_column, vga_row);
        } else if (vga_row > 0) {
            vga_row--;
            vga_column = max_cols - 1;
            vga_putentryat(' ', vga_color, vga_column, vga_row);
        }
    } else {
        vga_putentryat(c, vga_color, vga_column, vga_row);
        if (++vga_column >= max_cols) {
            vga_column = 0;
            if (++vga_row >= max_rows) {
                vga_scroll();
                vga_row = max_rows - 1;
            }
        }
    }
    vga_set_cursor((int)vga_column, (int)vga_row);
}

void vga_puts(const char* str) {
    
    
    
    
    
    
    
    
    
    
    
    for (size_t i = 0; str[i] != '\0'; i++)
        vga_putc(str[i]);
}

void vga_clear(enum vga_color fg, enum vga_color bg) {
    vga_color = vga_entry_color(fg, bg);
    vga_fg_color = vga_palette[fg];
    vga_bg_color = vga_palette[bg];
    
    if (is_framebuffer) {
        vga_draw_rect(0, 0, vga_fb_width, vga_fb_height, vga_bg_color);
    } else {
        for (size_t y = 0; y < max_rows; y++) {
            for (size_t x = 0; x < max_cols; x++) {
                text_buffer[y * max_cols + x] = vga_entry(' ', vga_color);
            }
        }
    }
    vga_row = 0;
    vga_column = 0;
    vga_set_cursor(0, 0);
}

void vga_get_dimensions(size_t *cols, size_t *rows) {
    if (cols) *cols = max_cols;
    if (rows) *rows = max_rows;
}
