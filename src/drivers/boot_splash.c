#include "boot_splash.h"
#include "vga.h"
#include "string.h"
#include "kheap.h"
#include <stdbool.h>

extern uint32_t vga_fb_width, vga_fb_height, vga_fb_pitch;
extern uint64_t vga_fb_addr;

#define SPLASH_BAR_H       6
#define SPLASH_BAR_Y_PAD   40
#define SPLASH_TEXT_Y_PAD  10
#define SPLASH_LOGO_MAX_H  200

static uint32_t s_splash_fg_color = 0x00FF41; /* Bright phosphor green */
static uint32_t s_splash_accent   = 0x00FF41; /* Phosphor green */
static uint32_t s_splash_text     = 0x00FF41; /* Bright green */
static uint32_t s_splash_dim      = 0x00CC33; /* Medium green */

static int s_progress = 0;
static int s_max_progress = 100;
static const char *s_status_text = "Iniciando...";
static bool s_active = false;

static void draw_gradient_bg(void) {
    if (!vga_fb_addr) return;
    uint32_t *fb = (uint32_t *)(uint64_t)vga_fb_addr;
    uint32_t stride = vga_fb_pitch / 4;
    /* Pure black CRT background */
    for (uint32_t y = 0; y < vga_fb_height; y++) {
        for (uint32_t x = 0; x < vga_fb_width; x++) {
            fb[y * stride + x] = 0x000000;
        }
    }
}

static void draw_centered_text(const char *text, int y, uint32_t color, int scale) {
    if (!text || !vga_fb_addr) return;
    int len = (int)strlen(text);
    int char_w = 8 * scale;
    int start_x = (vga_fb_width - len * char_w) / 2;
    if (start_x < 0) start_x = 0;
    for (int i = 0; i < len; i++) {
        vga_draw_char_scaled(start_x + i * char_w, y, text[i], color, scale);
    }
}

static void draw_progress_bar(int progress, int max_progress) {
    if (!vga_fb_addr) return;
    int bar_w = vga_fb_width * 60 / 100;
    int bar_h = SPLASH_BAR_H;
    int bar_x = (vga_fb_width - bar_w) / 2;
    int bar_y = vga_fb_height - SPLASH_BAR_Y_PAD - bar_h;
    int fill_w = (bar_w * progress) / max_progress;

    vga_draw_rect(bar_x - 2, bar_y - 2, bar_w + 4, bar_h + 4, 0x004400);
    vga_draw_rect(bar_x, bar_y, bar_w, bar_h, 0x002200);
    if (fill_w > 0) {
        vga_draw_rect(bar_x, bar_y, fill_w, bar_h, s_splash_fg_color);
    }
    vga_draw_rect(bar_x, bar_y, bar_w, bar_h, 0x002200);
}

static void draw_logo(void) {
    if (!vga_fb_addr) return;
    const char *logo[] = {
        "  ██╗     ███████╗ ██████╗ ██████╗ ███╗   ███╗",
        "  ██║     ██╔════╝██╔═══██╗██╔══██╗████╗ ████║",
        "  ██║     █████╗  ██║   ██║██████╔╝██╔████╔██║",
        "  ██║     ██╔══╝  ██║   ██║██╔══██╗██║╚██╔╝██║",
        "  ███████╗███████╗╚██████╔╝██║  ██║██║ ╚═╝ ██║",
        "  ╚══════╝╚══════╝ ╚═════╝ ╚═╝  ╚═╝╚═╝     ╚═╝"
    };
    int logo_h = 6 * 16;
    int start_y = (vga_fb_height - logo_h) / 2 - 40;
    for (int i = 0; i < 6; i++) {
        draw_centered_text(logo[i], start_y + i * 16, s_splash_accent, 1);
    }
}

void boot_splash_init(void) {
    if (!vga_fb_addr) return;
    s_active = true;
    s_progress = 0;
    draw_gradient_bg();
    draw_logo();
    draw_centered_text("LiwusOS", vga_fb_height / 2 + 30, s_splash_text, 2);
    draw_centered_text("Carregando...", vga_fb_height - SPLASH_BAR_Y_PAD - 30, s_splash_dim, 1);
    draw_progress_bar(0, s_max_progress);
}

void boot_splash_set_progress(int progress, const char *status) {
    if (!s_active || !vga_fb_addr) return;
    if (progress < 0) progress = 0;
    if (progress > s_max_progress) progress = s_max_progress;
    s_progress = progress;
    if (status) s_status_text = status;

    int bar_w = vga_fb_width * 60 / 100;
    int bar_h = SPLASH_BAR_H;
    int bar_x = (vga_fb_width - bar_w) / 2;
    int bar_y = vga_fb_height - SPLASH_BAR_Y_PAD - bar_h;

    int old_fill = (bar_w * (s_progress - 1)) / s_max_progress;
    int new_fill = (bar_w * s_progress) / s_max_progress;
    if (new_fill > old_fill) {
        vga_draw_rect(bar_x + old_fill, bar_y, new_fill - old_fill, bar_h, s_splash_fg_color);
    }
    draw_centered_text(s_status_text, vga_fb_height - SPLASH_BAR_Y_PAD - 30, s_splash_dim, 1);
}

void boot_splash_set_max_progress(int max_progress) {
    if (max_progress > 0) s_max_progress = max_progress;
}

void boot_splash_done(void) {
    if (!s_active) return;
    s_active = false;
    boot_splash_set_progress(s_max_progress, "Pronto!");
    for (volatile int i = 0; i < 500000; i++) asm volatile("nop");
}