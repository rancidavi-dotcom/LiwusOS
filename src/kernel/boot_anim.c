#include "boot_anim.h"
#include "video.h"
#include "string.h"

#define BG_COLOR     0xFF1A1A1A
#define ACCENT_COLOR 0xFF5747E0
#define TEXT_COLOR   0xFFFFFFFF
#define MUTED_COLOR  0xFFAAAAAA
#define DONE_COLOR   0xFF86C440

static int bar_x, bar_y, bar_w, bar_h;

void boot_anim_init(void) {
    if (!framebuffer || screen_width == 0 || screen_height == 0) return;

    clear_screen(BG_COLOR);

    int cx = screen_width / 2;

    const char *logo = "LiwusOS";
    int logo_len = 7;
    int logo_x = cx - (logo_len * 16) / 2;
    int logo_y = screen_height / 2 - 60;
    for (int i = 0; i < logo_len; i++)
        draw_char(logo_x + i * 16, logo_y, logo[i], TEXT_COLOR);

    int line_w = 100;
    draw_rect(cx - line_w / 2, logo_y + 28, line_w, 3, ACCENT_COLOR);

    const char *sub = "Preparing your system...";
    int sub_y = logo_y + 48;
    draw_string(cx - ((int)strlen(sub) * 8) / 2, sub_y, sub, MUTED_COLOR);

    bar_w = 320;
    bar_h = 6;
    bar_x = cx - bar_w / 2;
    bar_y = sub_y + 30;
    draw_rect(bar_x, bar_y, bar_w, bar_h, 0xFF333333);

    refresh_screen();
}

void boot_anim_update(int percent, const char *current_file) {
    (void)current_file;
    if (!framebuffer) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    int fill_w = (bar_w * percent) / 100;
    if (fill_w > 0)
        draw_rect(bar_x, bar_y, fill_w, bar_h, ACCENT_COLOR);

    refresh_screen();
}

void boot_anim_finish(void) {
    if (!framebuffer) return;

    boot_anim_update(100, NULL);

    int cx = screen_width / 2;
    const char *ready = "Ready!";
    draw_string(cx - ((int)strlen(ready) * 8) / 2, bar_y + 24, ready, DONE_COLOR);
    refresh_screen();
}
