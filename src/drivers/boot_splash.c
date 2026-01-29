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

/* --- Cores (formato 0x00RRGGBB) --- */
#define BG_COLOR       0x1A1A2E  /* dark navy-charcoal              */
#define LOGO_COLOR     0xFFFFFF  /* pure white                      */
#define TEXT_COLOR     0xCCCCCC  /* light gray                      */
#define DIM_COLOR      0x666666  /* medium gray                     */
#define BAR_BORDER     0x333355  /* dark blue-gray border           */
#define BAR_BG         0x222240  /* bar track                       */
#define BAR_FILL       0x4488FF  /* bright blue fill                */
#define BAR_COMPLETE   0x44CC66  /* green flash when done           */

static int s_progress = 0;
static int s_max_progress = 100;
static const char *s_status_text = "Iniciando...";
static bool s_active = false;

static void draw_bg(void) {
    if (!vga_fb_addr) return;
    uint32_t *fb = (uint32_t *)(uint64_t)vga_fb_addr;
    uint32_t stride = vga_fb_pitch / 4;
    for (uint32_t y = 0; y < vga_fb_height; y++) {
        for (uint32_t x = 0; x < vga_fb_width; x++) {
            fb[y * stride + x] = BG_COLOR;
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
    int bar_w = vga_fb_width * 50 / 100;
    int bar_h = 8;
    int bar_x = (vga_fb_width - bar_w) / 2;
    int bar_y = vga_fb_height - SPLASH_BAR_Y_PAD - bar_h;
    int fill_w = (bar_w * progress) / max_progress;

    vga_draw_rect(bar_x - 1, bar_y - 1, bar_w + 2, bar_h + 2, BAR_BORDER);
    vga_draw_rect(bar_x, bar_y, bar_w, bar_h, BAR_BG);
    if (fill_w > 0) {
        vga_draw_rect(bar_x, bar_y, fill_w, bar_h, BAR_FILL);
    }
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
        draw_centered_text(logo[i], start_y + i * 16, LOGO_COLOR, 1);
    }
}

void boot_splash_init(void) {
    if (!vga_fb_addr) return;
    s_active = true;
    s_progress = 0;
    draw_bg();
    draw_logo();
    draw_centered_text("LiwusOS", vga_fb_height / 2 + 30, TEXT_COLOR, 2);
    draw_centered_text("Carregando...", vga_fb_height - SPLASH_BAR_Y_PAD - 30, DIM_COLOR, 1);
    draw_progress_bar(0, s_max_progress);
}

void boot_splash_set_progress(int progress, const char *status) {
    if (!s_active || !vga_fb_addr) return;
    if (progress < 0) progress = 0;
    if (progress > s_max_progress) progress = s_max_progress;
    s_progress = progress;
    if (status) s_status_text = status;

    int bar_w = vga_fb_width * 50 / 100;
    int bar_h = 8;
    int bar_x = (vga_fb_width - bar_w) / 2;
    int bar_y = vga_fb_height - SPLASH_BAR_Y_PAD - bar_h;

    int old_fill = (bar_w * (s_progress - 1)) / s_max_progress;
    int new_fill = (bar_w * s_progress) / s_max_progress;
    if (new_fill > old_fill) {
        vga_draw_rect(bar_x + old_fill, bar_y, new_fill - old_fill, bar_h, BAR_FILL);
    }
    draw_centered_text(s_status_text, vga_fb_height - SPLASH_BAR_Y_PAD - 30, DIM_COLOR, 1);
}

void boot_splash_set_max_progress(int max_progress) {
    if (max_progress > 0) s_max_progress = max_progress;
}

void boot_splash_done(void) {
    if (!s_active) return;
    s_active = false;
    boot_splash_set_progress(s_max_progress, "Pronto!");
    /* flash the bar green for a beat so the user sees "done" */
    int bar_w = vga_fb_width * 50 / 100;
    int bar_h = 8;
    int bar_x = (vga_fb_width - bar_w) / 2;
    int bar_y = vga_fb_height - SPLASH_BAR_Y_PAD - bar_h;
    vga_draw_rect(bar_x, bar_y, bar_w, bar_h, BAR_COMPLETE);
    draw_centered_text("Pronto!", vga_fb_height - SPLASH_BAR_Y_PAD - 30, BAR_COMPLETE, 1);
    for (volatile int i = 0; i < 300000; i++) asm volatile("nop");
}