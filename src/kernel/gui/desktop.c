#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "vga.h"
#include "mouse.h"
#include "keyboard.h"
#include "kheap.h"
#include "string.h"
#include "rtc.h"
#include "pmm.h"
#include "net_manager.h"
#include "desktop.h"
#include "serial.h"
#include "task.h"
#include "terminal.h"
#include "hda.h"
#include "http_client.h"
#include "lpkg.h"
#include "sdfs.h"
#include "initrd.h"
#include "vfs.h"
#include "ata.h"
#include "ahci.h"
#include "gui/html_view.h"
#include "gui/html_font.h"
#include "gui/browser_url.h"

#include "lvgl.h"
#include "lvgl_port.h"
#include "acpi.h"

extern bool is_live_mode;
extern char g_system_hostname[32];

extern void fast_memcpy(void *dst, const void *src, uint64_t size);
extern char *itoa(int value, char *str, int base);
extern volatile uint32_t timer_ticks;
extern void usb_ehci_service(void);
extern void usb_ehci_poll(void);
extern void usb_xhci_poll(void);

bool g_desktop_running = false;
static void browser_stop_runtime(void);

/* =========================================================================
 * Paleta "Breeze Dark" (estilo KDE Plasma)
 * ========================================================================= */
#define PANEL_H          44
#define WIN_HEADER_H     32
#define BZ_PANEL         0x1f2226
#define BZ_PANEL_LINE    0x383d42
#define BZ_WIN_BG        0x2a2e32
#define BZ_TITLE         0x31363b
#define BZ_TITLE_LINE    0x1b1e20
#define BZ_VIEW          0x1b1e20
#define BZ_BUTTON        0x31363b
#define BZ_BUTTON_LINE   0x4d5359
#define BZ_HOVER         0x3b4148
#define BZ_TEXT          0xfcfcfc
#define BZ_TEXT_DIM      0xa1a9b1
#define BZ_ACCENT        0x3daee9
#define BZ_RED           0xda4453
#define BZ_GREEN         0x27ae60
#define BZ_AMBER         0xf67400
#define BZ_WALL_TOP      0x223042
#define BZ_WALL_BOTTOM   0x0d1117
#define BROWSER_QJS_MODULE_FETCH_ID_BASE 0x40000000U

static uint32_t desktop_screen_w(void) {
    uint32_t sw = vga_get_fb_width();
    return sw ? sw : 1024;
}

static uint32_t desktop_screen_h(void) {
    uint32_t sh = vga_get_fb_height();
    return sh ? sh : 768;
}

/* Botao plano estilo Breeze (cantos discretos, sem sombra). */
static void bz_style_button(lv_obj_t *btn) {
    lv_obj_set_style_bg_color(btn, lv_color_hex(BZ_BUTTON), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(BZ_HOVER), LV_STATE_PRESSED);
    lv_obj_set_style_border_color(btn, lv_color_hex(BZ_BUTTON_LINE), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(BZ_ACCENT), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(BZ_TEXT), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x6e757c), LV_STATE_DISABLED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(BZ_WIN_BG), LV_STATE_DISABLED);
}

/* =========================================================================
 * Funções Auxiliares
 * ========================================================================= */
static int simple_atoi(const char *s) {
    if (!s) return 0;
    while (*s == ' ') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    int res = 0;
    while (*s >= '0' && *s <= '9') {
        res = res * 10 + (*s - '0');
        s++;
    }
    return res * sign;
}

/* =========================================================================
 * Cursor Nativo LVGL (Seta Moderna Branca com Borda Preta sem Rastro)
 * ========================================================================= */
static const uint16_t cursor_border[16] = {
    0x8000, 0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFE00, 0xFF00,
    0xFF80, 0xF800, 0xD800, 0x8C00, 0x0C00, 0x0600, 0x0600, 0x0000
};

static const uint16_t cursor_mask[16] = {
    0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7C00, 0x7E00, 0x7F00,
    0x7800, 0x4800, 0x0800, 0x0400, 0x0400, 0x0200, 0x0200, 0x0000
};

static uint32_t g_cursor_pixels[16 * 16];
static lv_img_dsc_t g_cursor_img_dsc;
static lv_obj_t *g_cursor_obj = NULL;

static void init_cursor(void) {
    for (int y = 0; y < 16; y++) {
        uint16_t bmask = cursor_border[y];
        uint16_t fmask = cursor_mask[y];
        for (int x = 0; x < 16; x++) {
            uint16_t bit = 1 << (15 - x);
            if (bmask & bit) {
                g_cursor_pixels[y * 16 + x] = 0xFF000000; /* Borda preta opaca */
            } else if (fmask & bit) {
                g_cursor_pixels[y * 16 + x] = 0xFFFFFFFF; /* Preenchimento branco opaco */
            } else {
                g_cursor_pixels[y * 16 + x] = 0x00000000; /* Transparente */
            }
        }
    }

    g_cursor_img_dsc.header.always_zero = 0;
    g_cursor_img_dsc.header.w = 16;
    g_cursor_img_dsc.header.h = 16;
    g_cursor_img_dsc.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    g_cursor_img_dsc.data_size = sizeof(g_cursor_pixels);
    g_cursor_img_dsc.data = (const uint8_t *)g_cursor_pixels;

    g_cursor_obj = lv_img_create(lv_scr_act());
    lv_img_set_src(g_cursor_obj, &g_cursor_img_dsc);
    lv_indev_set_cursor(lvgl_get_mouse_indev(), g_cursor_obj);
}

/* =========================================================================
 * Sistema de Janelas (Arrasto, Minimizar, Maximizar/Restaurar, Fechar)
 * ========================================================================= */
#define MAX_MANAGED_WINS 8

typedef struct {
    lv_obj_t *win;
    lv_obj_t *btn_max;
    bool maximized;
    lv_coord_t nx, ny, nw, nh;
} win_state_t;

static win_state_t g_win_states[MAX_MANAGED_WINS];
static int g_win_state_count = 0;

static win_state_t *win_state_get(lv_obj_t *win) {
    for (int i = 0; i < g_win_state_count; i++) {
        if (g_win_states[i].win == win) return &g_win_states[i];
    }
    if (g_win_state_count < MAX_MANAGED_WINS) {
        win_state_t *st = &g_win_states[g_win_state_count++];
        memset(st, 0, sizeof(*st));
        st->win = win;
        return st;
    }
    return NULL;
}

static void win_drag_event_cb(lv_event_t *e) {
    lv_obj_t *win = (lv_obj_t *)lv_event_get_user_data(e);
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev || !win) return;

    win_state_t *st = win_state_get(win);
    if (st && st->maximized) return;

    lv_point_t vect;
    lv_indev_get_vect(indev, &vect);

    if (vect.x != 0 || vect.y != 0) {
        lv_coord_t x = lv_obj_get_x(win) + vect.x;
        lv_coord_t y = lv_obj_get_y(win) + vect.y;

        uint32_t sw = desktop_screen_w();
        uint32_t sh = desktop_screen_h();

        if (x < -100) x = -100;
        if (y < 0) y = 0;
        if (x > (lv_coord_t)sw - 100) x = (lv_coord_t)sw - 100;
        if (y > (lv_coord_t)sh - PANEL_H - WIN_HEADER_H) y = (lv_coord_t)sh - PANEL_H - WIN_HEADER_H;

        lv_obj_set_pos(win, x, y);
    }
}

static lv_obj_t *win_browser;
static lv_obj_t *win_pkgmgr;
static void set_browser_url_focus(bool focus);
static void set_pkg_input_focus(bool focus);
static void start_menu_hide(void);

static void win_focus_event_cb(lv_event_t *e) {
    lv_obj_t *win = (lv_obj_t *)lv_event_get_user_data(e);
    start_menu_hide();
    if (win) {
        lv_obj_move_foreground(win);
        set_browser_url_focus(win == win_browser);
        if (win != win_pkgmgr) set_pkg_input_focus(false);
    }
}

static void setup_window(lv_obj_t *win, int w, int h, int x, int y) {
    if (!win) return;
    lv_obj_set_size(win, w, h);
    lv_obj_set_align(win, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(win, x, y);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    win_state_t *st = win_state_get(win);
    if (st) {
        st->nx = (lv_coord_t)x;
        st->ny = (lv_coord_t)y;
        st->nw = (lv_coord_t)w;
        st->nh = (lv_coord_t)h;
        st->maximized = false;
    }

    lv_obj_set_style_shadow_width(win, 22, 0);
    lv_obj_set_style_shadow_color(win, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(win, LV_OPA_50, 0);
    lv_obj_set_style_radius(win, 0, 0);
    lv_obj_set_style_bg_color(win, lv_color_hex(BZ_WIN_BG), 0);
    lv_obj_set_style_bg_opa(win, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(win, lv_color_hex(BZ_TITLE_LINE), 0);
    lv_obj_set_style_border_width(win, 1, 0);
    lv_obj_set_style_text_color(win, lv_color_hex(BZ_TEXT), 0);

    lv_obj_t *content = lv_win_get_content(win);
    if (content) {
        lv_obj_set_style_bg_color(content, lv_color_hex(BZ_WIN_BG), 0);
        lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(content, 0, 0);
        lv_obj_set_style_text_color(content, lv_color_hex(BZ_TEXT), 0);
    }

    lv_obj_t *header = lv_win_get_header(win);
    if (header) {
        lv_obj_clear_flag(header, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(header, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(header, lv_color_hex(BZ_TITLE), 0);
        lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(header, 0, 0);
        lv_obj_set_style_border_side(header, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_color(header, lv_color_hex(BZ_TITLE_LINE), 0);
        lv_obj_set_style_border_width(header, 1, 0);
        lv_obj_set_style_pad_left(header, 10, 0);
        lv_obj_set_style_pad_column(header, 2, 0);
        lv_obj_set_style_pad_right(header, 4, 0);
        lv_obj_set_style_pad_ver(header, 3, 0);
        lv_obj_add_event_cb(header, win_focus_event_cb, LV_EVENT_PRESSED, win);
        lv_obj_add_event_cb(header, win_drag_event_cb, LV_EVENT_PRESSING, win);
    }

    lv_obj_add_event_cb(win, win_focus_event_cb, LV_EVENT_PRESSED, win);
}

/* =========================================================================
 * Terminal do Sistema (Konsole LiwusOS)
 * ========================================================================= */
#define KONSOLE_MAX_ROWS 256
#define KONSOLE_MAX_COLS 120
#define KONSOLE_VISIBLE_ROWS 24
static char g_konsole_history[KONSOLE_MAX_ROWS][KONSOLE_MAX_COLS];
static int g_konsole_row_count = 0;
static int g_term_cur_row = 0;
static int g_term_cur_col = 0;
static char g_cwd[64] = "/";

static char g_konsole_input[128] = "";
static int g_konsole_input_len = 0;

static int g_term_esc_state = 0;
static int g_term_esc_params[4] = {0};
static int g_term_esc_pcount = 0;
static int g_term_esc_val = 0;
static bool g_term_esc_private = false;
static bool g_term_cursor_visible = true;

static lv_obj_t *win_terminal = NULL;
static lv_obj_t *lbl_term_text = NULL;
static lv_obj_t *obj_term_cursor = NULL;

static void term_handle_ansi_cmd(char cmd) {
    if (g_term_esc_pcount < 4) {
        g_term_esc_params[g_term_esc_pcount++] = g_term_esc_val;
    }
    if (cmd == 'J') {
        int mode = g_term_esc_params[0];
        if (mode == 2 || mode == 0) {
            for (int r = 0; r < KONSOLE_MAX_ROWS; r++) g_konsole_history[r][0] = '\0';
            g_konsole_row_count = 0;
            g_term_cur_row = 0;
            g_term_cur_col = 0;
        }
    } else if (cmd == 'H' || cmd == 'f') {
        int row = (g_term_esc_pcount >= 1 && g_term_esc_params[0] > 0) ? g_term_esc_params[0] - 1 : 0;
        int col = (g_term_esc_pcount >= 2 && g_term_esc_params[1] > 0) ? g_term_esc_params[1] - 1 : 0;
        if (row < 0) row = 0;
        if (row >= KONSOLE_MAX_ROWS) row = KONSOLE_MAX_ROWS - 1;
        if (col < 0) col = 0;
        if (col >= KONSOLE_MAX_COLS - 2) col = KONSOLE_MAX_COLS - 2;
        g_term_cur_row = row;
        g_term_cur_col = col;
    } else if (cmd == 'K') {
        if (g_term_cur_row < KONSOLE_MAX_ROWS && g_term_cur_col < KONSOLE_MAX_COLS) {
            g_konsole_history[g_term_cur_row][g_term_cur_col] = '\0';
        }
    } else if (cmd == 'l' && g_term_esc_private && g_term_esc_params[0] == 25) {
        g_term_cursor_visible = false;
    } else if (cmd == 'h' && g_term_esc_private && g_term_esc_params[0] == 25) {
        g_term_cursor_visible = true;
    }
}

static void term_putc(char c) {
    if (g_term_esc_state == 1) {
        if (c == '[') {
            g_term_esc_state = 2;
            g_term_esc_pcount = 0;
            g_term_esc_val = 0;
            g_term_esc_private = false;
        } else {
            g_term_esc_state = 0;
        }
        return;
    }
    if (g_term_esc_state == 2) {
        if (c >= '0' && c <= '9') {
            g_term_esc_val = g_term_esc_val * 10 + (c - '0');
            return;
        }
        if (c == ';') {
            if (g_term_esc_pcount < 4) {
                g_term_esc_params[g_term_esc_pcount++] = g_term_esc_val;
            }
            g_term_esc_val = 0;
            return;
        }
        if (c == '?') {
            g_term_esc_private = true;
            return;
        }
        term_handle_ansi_cmd(c);
        g_term_esc_state = 0;
        g_term_esc_private = false;
        return;
    }
    if (c == '\x1b') {
        g_term_esc_state = 1;
        g_term_esc_pcount = 0;
        g_term_esc_val = 0;
        return;
    }

    if (c == '\r') {
        g_term_cur_col = 0;
        return;
    }
    if (c == '\n') {
        g_term_cur_col = 0;
        if (g_term_cur_row < KONSOLE_MAX_ROWS - 1) {
            g_term_cur_row++;
        }
        if (g_term_cur_row >= g_konsole_row_count) {
            g_konsole_row_count = g_term_cur_row + 1;
        }
        return;
    }
    if (c == '\b') {
        if (g_term_cur_col > 0) {
            g_term_cur_col--;
            g_konsole_history[g_term_cur_row][g_term_cur_col] = '\0';
        }
        return;
    }
    if (c == '\t') {
        int next_tab = (g_term_cur_col + 4) & ~3;
        while (g_term_cur_col < next_tab && g_term_cur_col < KONSOLE_MAX_COLS - 2) {
            g_konsole_history[g_term_cur_row][g_term_cur_col++] = ' ';
        }
        g_konsole_history[g_term_cur_row][g_term_cur_col] = '\0';
        if (g_term_cur_row >= g_konsole_row_count) {
            g_konsole_row_count = g_term_cur_row + 1;
        }
        return;
    }
    if ((unsigned char)c >= 32) {
        if (g_term_cur_col < KONSOLE_MAX_COLS - 2) {
            g_konsole_history[g_term_cur_row][g_term_cur_col++] = c;
            g_konsole_history[g_term_cur_row][g_term_cur_col] = '\0';
            if (g_term_cur_row >= g_konsole_row_count) {
                g_konsole_row_count = g_term_cur_row + 1;
            }
        }
    }
}

static void term_append(const char *str) {
    if (!str) return;
    for (size_t i = 0; str[i] != '\0'; i++) {
        term_putc(str[i]);
    }
}

static void terminal_update_view(void) {
    if (!lbl_term_text) return;

    static char display_buf[4096];
    display_buf[0] = '\0';
    size_t cur_len = 0;

    int start_row = 0;
    if (g_konsole_row_count > KONSOLE_VISIBLE_ROWS) {
        start_row = g_konsole_row_count - KONSOLE_VISIBLE_ROWS;
    }

    size_t cur_row_char_start = 0;
    size_t cur_row_llen = 0;
    bool cur_row_in_view = false;

    for (int r = start_row; r < g_konsole_row_count && r < KONSOLE_MAX_ROWS; r++) {
        const char *line = g_konsole_history[r];
        size_t llen = strlen(line);
        if (r == g_term_cur_row) {
            cur_row_char_start = cur_len;
            cur_row_llen = llen;
            cur_row_in_view = true;
        }
        if (cur_len + llen + 2 < sizeof(display_buf)) {
            memcpy(display_buf + cur_len, line, llen);
            cur_len += llen;
            display_buf[cur_len++] = '\n';
            display_buf[cur_len] = '\0';
        }
    }

    /* Se o programa posicionou o cursor numa linha ainda além de g_konsole_row_count */
    if (vga_putc_capture_hook != NULL && !cur_row_in_view &&
        g_term_cur_row >= start_row && g_term_cur_row < start_row + KONSOLE_VISIBLE_ROWS) {
        int extra_rows = g_term_cur_row - g_konsole_row_count;
        for (int e = 0; e <= extra_rows && cur_len + 2 < sizeof(display_buf); e++) {
            if (e == extra_rows) {
                cur_row_char_start = cur_len;
                cur_row_llen = 0;
                cur_row_in_view = true;
            }
            display_buf[cur_len++] = '\n';
            display_buf[cur_len] = '\0';
        }
    }

    /* O prompt do shell só aparece se NÃO houver um programa interativo rodando */
    if (vga_putc_capture_hook == NULL) {
        const char *cur_cwd = (current_task && current_task->process && current_task->process->cwd[0]) ?
                              current_task->process->cwd : "/";
        char prompt[160];
        lv_snprintf(prompt, sizeof(prompt), "root@%s:[%s]# %s_",
                    g_system_hostname[0] ? g_system_hostname : "liwus",
                    cur_cwd, g_konsole_input);
        size_t plen = strlen(prompt);
        if (cur_len + plen < sizeof(display_buf)) {
            memcpy(display_buf + cur_len, prompt, plen);
            cur_len += plen;
            display_buf[cur_len] = '\0';
        }
    }

    lv_label_set_text(lbl_term_text, display_buf);

    /* Renderiza o cursor visual de caractere quando um programa interativo (ex: Kilo) está ativo */
    if (obj_term_cursor) {
        if (vga_putc_capture_hook != NULL && g_term_cursor_visible && cur_row_in_view) {
            lv_obj_update_layout(lbl_term_text);
            const lv_font_t *font = lv_obj_get_style_text_font(lbl_term_text, LV_PART_MAIN);
            lv_coord_t letter_space = lv_obj_get_style_text_letter_space(lbl_term_text, LV_PART_MAIN);
            lv_coord_t char_w = (lv_coord_t)lv_font_get_glyph_width(font, 'M', '\0') + letter_space;
            lv_coord_t font_h = lv_font_get_line_height(font);
            if (char_w <= 0) char_w = 8;
            if (font_h <= 0) font_h = 9;

            size_t col_in_line = ((size_t)g_term_cur_col <= cur_row_llen) ? (size_t)g_term_cur_col : cur_row_llen;
            lv_point_t pos;
            lv_label_get_letter_pos(lbl_term_text, (uint32_t)(cur_row_char_start + col_in_line), &pos);
            if ((size_t)g_term_cur_col > cur_row_llen) {
                pos.x += (lv_coord_t)(((size_t)g_term_cur_col - cur_row_llen) * (size_t)char_w);
            }

            lv_obj_set_size(obj_term_cursor, char_w, font_h);
            lv_obj_set_pos(obj_term_cursor, pos.x, pos.y);
            lv_obj_clear_flag(obj_term_cursor, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(obj_term_cursor, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void term_exec_cmd(const char *cmd);

static void desktop_update_topbar_status(bool force);

void desktop_force_redraw(void) {
    if (!g_desktop_running) return;
    lv_obj_invalidate(lv_scr_act());
    lv_refr_now(NULL);
    vga_flip();
}

void desktop_refresh_terminal(void) {
    if (!g_desktop_running) return;

    net_poll();
    usb_ehci_service();
    usb_xhci_poll();

    desktop_update_topbar_status(false);
    terminal_update_view();
    lv_refr_now(NULL);
    vga_flip();
}

/* =========================================================================
 * Janelas e Aplicativos Gráficos Essenciais
 * ========================================================================= */
static lv_obj_t *win_sysmon = NULL;
static lv_obj_t *win_calc = NULL;
static lv_obj_t *win_browser = NULL;
static lv_obj_t *win_pkgmgr = NULL;
static lv_obj_t *win_installer = NULL;
static lv_obj_t *win_about = NULL;

static lv_obj_t *lbl_inst_disk = NULL;
static lv_obj_t *ta_inst_hostname = NULL;
static lv_obj_t *btn_inst_kb_br = NULL;
static lv_obj_t *btn_inst_kb_us = NULL;
static lv_obj_t *btn_inst_prof_full = NULL;
static lv_obj_t *btn_inst_prof_min = NULL;
static lv_obj_t *bar_inst_prog = NULL;
static lv_obj_t *lbl_inst_step = NULL;
static lv_obj_t *lbl_inst_log = NULL;
static lv_obj_t *btn_inst_start = NULL;
static lv_obj_t *btn_inst_reboot = NULL;
static bool g_inst_host_focused = false;
static int g_inst_kb_abnt2 = 1;
static int g_inst_profile_full = 1;
static int g_inst_disk_found = 0;
static int g_inst_disk_is_ahci = 0;
static uint8_t g_inst_ahci_port = 0xFF;
static uint16_t g_inst_ata_bus = ATA_PRIMARY;
static uint8_t g_inst_ata_drive = ATA_MASTER;
static uint32_t g_inst_disk_sectors = 0;
static bool g_inst_completed = false;

static lv_obj_t *ta_pkg_input = NULL;
static lv_obj_t *obj_pkg_list = NULL;
static lv_obj_t *lbl_pkg_status = NULL;
static lv_obj_t *btn_pkg_tab_apps = NULL;
static lv_obj_t *btn_pkg_tab_sys = NULL;
static lv_obj_t *pkg_tab_apps_cont = NULL;
static lv_obj_t *pkg_tab_sys_cont = NULL;
static lv_obj_t *lbl_sysupd_cur = NULL;
static lv_obj_t *lbl_sysupd_rem = NULL;
static lv_obj_t *lbl_sysupd_state = NULL;
static lv_obj_t *lbl_sysupd_notes = NULL;
static lv_obj_t *bar_sysupd_prog = NULL;
static lv_obj_t *lbl_about_title = NULL;
static int g_pkg_active_tab = 0;
static lpkg_sysupdate_info_t g_sysupd_info;
static bool g_pkg_input_focused = false;
static int g_sysupd_last_percent = -1;
static int g_sysupd_last_phase_percent = -1;
static size_t g_sysupd_last_unknown_bytes = 0;
static char g_sysupd_last_phase[48];

static lv_obj_t *lbl_clock = NULL;
static lv_obj_t *lbl_net = NULL;
static lv_obj_t *lbl_ram = NULL;

static lv_obj_t *lbl_calc_display = NULL;
static char g_calc_str[32] = "0";
static double g_calc_val = 0;
static char g_calc_op = 0;
static bool g_calc_new_num = true;

#define BROWSER_URL_MAX 2048U
#define BROWSER_DOCUMENT_MAX (2U * 1024U * 1024U)
static char g_browser_url[BROWSER_URL_MAX] = "http://liwus.local/";
static lv_obj_t *ta_browser_url = NULL;
static lv_obj_t *obj_browser_view = NULL;
static lv_obj_t *lbl_browser_win_title = NULL;
static lv_obj_t *btn_browser_back = NULL;
static lv_obj_t *btn_browser_fwd = NULL;
static bool g_browser_url_focused = false;
static lv_obj_t *g_active_web_input = NULL;
static int g_web_runtime_pid = 0;
static fs_node_t *g_web_runtime_commands = NULL;
static fs_node_t *g_web_runtime_notifications = NULL;
static fs_node_t *g_web_runtime_fetch_requests = NULL;
static fs_node_t *g_web_runtime_fetch_responses = NULL;
static volatile uint32_t g_web_runtime_generation;
static const char g_web_runtime_input_path[] = "/.web-runtime/input.html";
static const char g_web_runtime_output_path[] = "/.web-runtime/output.html";
static volatile int g_browser_fetch_state;
static int g_browser_fetch_tid;
static char g_browser_fetch_url[BROWSER_URL_MAX];
static uint8_t *g_browser_fetch_body;
static size_t g_browser_fetch_size;
static int g_browser_fetch_status;
static volatile size_t g_browser_fetch_received;
static volatile size_t g_browser_fetch_total;
static bool g_browser_fetch_pending;
static char g_browser_pending_url[BROWSER_URL_MAX];

#define BROWSER_HIST_MAX 16
static char g_browser_hist[BROWSER_HIST_MAX][BROWSER_URL_MAX];
static int g_browser_hist_count = 0;
static int g_browser_hist_idx = -1;

#define WEB_CACHE_SLOTS 40
#define WEB_CACHE_MAX_TOTAL (8U * 1024U * 1024U)
typedef struct {
    char url[BROWSER_URL_MAX];
    uint8_t *data;
    size_t len;
    uint32_t last_used;
    bool used;
} web_cache_slot_t;

static web_cache_slot_t g_web_cache[WEB_CACHE_SLOTS];
static uint32_t g_web_cache_clock = 0;
static size_t g_web_cache_bytes = 0;
static size_t g_browser_progress_last = 0;
static unsigned g_browser_resource_number = 0;

static void browser_progress_title(const char *phase, size_t received,
                                  size_t total) {
    if (!lbl_browser_win_title) return;
    char text[128];
    if (total) {
        unsigned pct = received >= total ? 100U : (unsigned)((received * 100U) / total);
        lv_snprintf(text, sizeof(text), "  %s %u%% (%u/%u bytes) - LiwusWeb",
                    phase, pct, (unsigned)received, (unsigned)total);
    } else {
        lv_snprintf(text, sizeof(text), "  %s (%u bytes) - LiwusWeb",
                    phase, (unsigned)received);
    }
    lv_label_set_text(lbl_browser_win_title, text);
    lv_refr_now(NULL);
    vga_flip();
}

static void browser_http_status(const char *message, void *user_data) {
    (void)user_data;
    if (!message || !lbl_browser_win_title) return;
    char text[128];
    lv_snprintf(text, sizeof(text), "  %s - LiwusWeb", message);
    lv_label_set_text(lbl_browser_win_title, text);
    lv_refr_now(NULL);
    vga_flip();
}

static void browser_http_progress(size_t received, size_t total, void *user_data) {
    const char *phase = (const char *)user_data;
    if (received < g_browser_progress_last + 32768U &&
        (!total || received < total)) return;
    g_browser_progress_last = received;
    browser_progress_title(phase ? phase : "Baixando", received, total);
}

static void set_browser_url_focus(bool focus) {
    if (!focus) {
        g_browser_url_focused = false;
        g_active_web_input = NULL;
        if (ta_browser_url) lv_obj_clear_state(ta_browser_url, LV_STATE_FOCUSED);
        return;
    }
    if (g_active_web_input != NULL) {
        g_browser_url_focused = false;
        if (ta_browser_url) lv_obj_clear_state(ta_browser_url, LV_STATE_FOCUSED);
        return;
    }
    g_browser_url_focused = true;
    if (ta_browser_url) lv_obj_add_state(ta_browser_url, LV_STATE_FOCUSED);
}

static void browser_url_bar_click_cb(lv_event_t *e) {
    lv_obj_t *prev = g_active_web_input;
    g_active_web_input = NULL;
    if (prev) lv_obj_clear_state(prev, LV_STATE_FOCUSED);
    win_focus_event_cb(e);
}

static void set_inst_host_focus(bool focus);

static void set_pkg_input_focus(bool focus) {
    g_pkg_input_focused = focus;
    if (ta_pkg_input) {
        if (focus) lv_obj_add_state(ta_pkg_input, LV_STATE_FOCUSED);
        else lv_obj_clear_state(ta_pkg_input, LV_STATE_FOCUSED);
    }
    if (focus) {
        set_browser_url_focus(false);
        set_inst_host_focus(false);
    }
}

static void set_inst_host_focus(bool focus) {
    g_inst_host_focused = focus;
    if (ta_inst_hostname) {
        if (focus) lv_obj_add_state(ta_inst_hostname, LV_STATE_FOCUSED);
        else lv_obj_clear_state(ta_inst_hostname, LV_STATE_FOCUSED);
    }
    if (focus) {
        set_browser_url_focus(false);
        set_pkg_input_focus(false);
    }
}

static void create_terminal_window(void);
static void create_sysmon_window(void);
static void create_calc_window(void);
static void create_browser_window(void);
static void create_pkgmgr_window(void);
static void create_installer_window(void);
static void create_about_window(void);
static void pkgmgr_refresh_list(void);
static void pkgmgr_refresh_sysupdate_ui(void);
static void pkgmgr_set_status(const char *msg, uint32_t color_hex);

static void win_min_event_cb(lv_event_t *e) {
    lv_obj_t *win = (lv_obj_t *)lv_event_get_user_data(e);
    if (!win) return;
    lv_obj_add_flag(win, LV_OBJ_FLAG_HIDDEN);
    if (win == win_browser) set_browser_url_focus(false);
    if (win == win_pkgmgr) set_pkg_input_focus(false);
    if (win == win_installer) set_inst_host_focus(false);
}

static void win_max_event_cb(lv_event_t *e) {
    lv_obj_t *win = (lv_obj_t *)lv_event_get_user_data(e);
    if (!win) return;
    win_state_t *st = win_state_get(win);
    if (!st) return;

    lv_obj_move_foreground(win);

    if (!st->maximized) {
        st->nx = lv_obj_get_x(win);
        st->ny = lv_obj_get_y(win);
        st->nw = lv_obj_get_width(win);
        st->nh = lv_obj_get_height(win);

        uint32_t sw = desktop_screen_w();
        uint32_t sh = desktop_screen_h();
        int max_h = (int)sh - PANEL_H;
        if (max_h < 240) max_h = 240;

        lv_obj_set_pos(win, 0, 0);
        lv_obj_set_size(win, (lv_coord_t)sw, (lv_coord_t)max_h);
        st->maximized = true;
        if (st->btn_max) {
            lv_obj_t *icon = lv_obj_get_child(st->btn_max, 0);
            if (icon) lv_img_set_src(icon, LV_SYMBOL_COPY);
        }
    } else {
        lv_obj_set_pos(win, st->nx, st->ny);
        lv_obj_set_size(win, st->nw, st->nh);
        st->maximized = false;
        if (st->btn_max) {
            lv_obj_t *icon = lv_obj_get_child(st->btn_max, 0);
            if (icon) lv_img_set_src(icon, LV_SYMBOL_UP);
        }
    }
    lv_obj_update_layout(win);
}

static void win_close_event_cb(lv_event_t *e) {
    lv_obj_t *win = (lv_obj_t *)lv_event_get_user_data(e);
    if (win) {
        lv_obj_add_flag(win, LV_OBJ_FLAG_HIDDEN);
        if (win == win_browser) {
            set_browser_url_focus(false);
            browser_stop_runtime();
        } else if (win == win_pkgmgr) {
            set_pkg_input_focus(false);
        } else if (win == win_installer) {
            set_inst_host_focus(false);
        } else if (win == win_calc) {
            strcpy(g_calc_str, "0");
            g_calc_val = 0;
            g_calc_op = 0;
            g_calc_new_num = true;
            if (lbl_calc_display) lv_label_set_text(lbl_calc_display, g_calc_str);
        }
    }
}

static void bz_style_title_button(lv_obj_t *btn, bool is_close) {
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(is_close ? BZ_RED : BZ_HOVER), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_outline_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(BZ_TEXT), LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(btn, 0, 0);
}

static lv_obj_t *add_window_controls(lv_obj_t *win, const char *title) {
    while (title && *title == ' ') title++;
    lv_obj_t *lbl_title = lv_win_add_title(win, title);
    if (lbl_title) {
        lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_title, lv_color_hex(BZ_TEXT), 0);
    }

    lv_obj_t *btn_min = lv_win_add_btn(win, LV_SYMBOL_DOWN, 26);
    bz_style_title_button(btn_min, false);
    lv_obj_add_event_cb(btn_min, win_min_event_cb, LV_EVENT_CLICKED, win);

    lv_obj_t *btn_max = lv_win_add_btn(win, LV_SYMBOL_UP, 26);
    bz_style_title_button(btn_max, false);
    lv_obj_add_event_cb(btn_max, win_max_event_cb, LV_EVENT_CLICKED, win);

    win_state_t *st = win_state_get(win);
    if (st) st->btn_max = btn_max;

    lv_obj_t *btn_close = lv_win_add_btn(win, LV_SYMBOL_CLOSE, 26);
    bz_style_title_button(btn_close, true);
    lv_obj_add_event_cb(btn_close, win_close_event_cb, LV_EVENT_CLICKED, win);

    return lbl_title;
}

static void term_exec_cmd(const char *cmd) {
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    const char *cur_cwd = (current_task && current_task->process && current_task->process->cwd[0]) ?
                          current_task->process->cwd : "/";
    char prompt_line[160];
    prompt_line[0] = '\0';
    strcat(prompt_line, "root@");
    strncat(prompt_line, g_system_hostname[0] ? g_system_hostname : "liwus", 24);
    strcat(prompt_line, ":[");
    strncat(prompt_line, cur_cwd, 24);
    strcat(prompt_line, "]# ");
    strcat(prompt_line, cmd);
    strcat(prompt_line, "\n");
    term_append(prompt_line);

    if (strcmp(cmd, "clear") == 0) {
        for (int r = 0; r < KONSOLE_MAX_ROWS; r++) g_konsole_history[r][0] = '\0';
        g_konsole_row_count = 0;
        g_term_cur_row = 0;
        g_term_cur_col = 0;
    } else {
        char cmd_copy[128];
        strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
        cmd_copy[sizeof(cmd_copy) - 1] = '\0';

        char *argv[TERMINAL_MAX_ARGS];
        int argc = terminal_parse_line(cmd_copy, argv);

        vga_puts_capture_hook = term_append;
        vga_putc_capture_hook = term_putc;
        terminal_execute(argc, argv);
        vga_puts_capture_hook = NULL;
        vga_putc_capture_hook = NULL;
        g_term_cursor_visible = true;
    }

    if (current_task && current_task->process && current_task->process->cwd[0]) {
        strncpy(g_cwd, current_task->process->cwd, sizeof(g_cwd) - 1);
        g_cwd[sizeof(g_cwd) - 1] = '\0';
    }

    pkgmgr_refresh_list();
    terminal_update_view();
}

/* Handlers do Dock (Abertura Imediata sem Alocação em Tempo de Execução) */
static void dock_term_cb(lv_event_t *e) {
    (void)e;
    lv_obj_clear_flag(win_terminal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(win_terminal);
    set_browser_url_focus(false);
    set_pkg_input_focus(false);
    set_inst_host_focus(false);
}

static void dock_browser_cb(lv_event_t *e) {
    (void)e;
    lv_obj_clear_flag(win_browser, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(win_browser);
    set_pkg_input_focus(false);
    set_inst_host_focus(false);
    set_browser_url_focus(true);
}

static void dock_pkgmgr_cb(lv_event_t *e) {
    (void)e;
    lv_obj_clear_flag(win_pkgmgr, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(win_pkgmgr);
    set_browser_url_focus(false);
    set_pkg_input_focus(false);
    set_inst_host_focus(false);

    if (!lpkg_is_repo_synced() && net_has_ip()) {
        pkgmgr_set_status("Buscando pacotes e versoes mais recentes no GitHub HTTPS...", BZ_ACCENT);
        lv_refr_now(NULL);
        vga_flip();

        char sync_msg[160];
        lpkg_update_repo(sync_msg, sizeof(sync_msg));
        lpkg_check_system_update(&g_sysupd_info, NULL, 0);
        pkgmgr_refresh_list();
        pkgmgr_refresh_sysupdate_ui();
        pkgmgr_set_status(sync_msg, BZ_GREEN);
    } else {
        pkgmgr_refresh_list();
        pkgmgr_refresh_sysupdate_ui();
    }
}

static void dock_sysmon_cb(lv_event_t *e) {
    (void)e;
    lv_obj_clear_flag(win_sysmon, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(win_sysmon);
    set_browser_url_focus(false);
    set_pkg_input_focus(false);
}

static void dock_calc_cb(lv_event_t *e) {
    (void)e;
    lv_obj_clear_flag(win_calc, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(win_calc);
    set_browser_url_focus(false);
    set_pkg_input_focus(false);
}

static void dock_doom_cb(lv_event_t *e) {
    (void)e;
    extern int run_doom_app(int argc, char **argv);
    set_browser_url_focus(false);
    set_pkg_input_focus(false);
    run_doom_app(0, NULL);
}

static void dock_snes_cb(lv_event_t *e) {
    (void)e;
    extern int run_snes_app(int argc, char **argv);
    set_browser_url_focus(false);
    set_pkg_input_focus(false);
    run_snes_app(0, NULL);
}

static void dock_about_cb(lv_event_t *e) {
    (void)e;
    lv_obj_clear_flag(win_about, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(win_about);
    set_browser_url_focus(false);
    set_pkg_input_focus(false);
    set_inst_host_focus(false);
}

static void installer_detect_disk(void);

static void dock_installer_cb(lv_event_t *e) {
    (void)e;
    if (!is_live_mode || !win_installer) return;
    lv_obj_clear_flag(win_installer, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(win_installer);
    set_browser_url_focus(false);
    set_pkg_input_focus(false);
    set_inst_host_focus(false);
    installer_detect_disk();
}

/* =========================================================================
 * Criação da Janela de Terminal (Konsole)
 * ========================================================================= */
static void create_terminal_window(void) {
    if (win_terminal) return;

    win_terminal = lv_win_create(lv_scr_act(), WIN_HEADER_H);
    setup_window(win_terminal, 680, 440, 172, 70);
    add_window_controls(win_terminal, "Konsole - Terminal LiwusOS");

    lv_obj_t *content = lv_win_get_content(win_terminal);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(content, lv_color_hex(0x232627), 0);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_add_event_cb(content, win_focus_event_cb, LV_EVENT_PRESSED, win_terminal);

    lbl_term_text = lv_label_create(content);
    lv_obj_set_width(lbl_term_text, lv_pct(100));
    lv_obj_set_style_text_font(lbl_term_text, &lv_font_unscii_8, 0);
    lv_obj_set_style_text_color(lbl_term_text, lv_color_hex(0xeff0f1), 0);
    lv_label_set_long_mode(lbl_term_text, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(lbl_term_text, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lbl_term_text, win_focus_event_cb, LV_EVENT_PRESSED, win_terminal);

    obj_term_cursor = lv_obj_create(content);
    lv_obj_clear_flag(obj_term_cursor, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj_term_cursor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(obj_term_cursor, 8, 9);
    lv_obj_set_style_bg_color(obj_term_cursor, lv_color_hex(BZ_ACCENT), 0);
    lv_obj_set_style_bg_opa(obj_term_cursor, LV_OPA_60, 0);
    lv_obj_set_style_border_color(obj_term_cursor, lv_color_hex(BZ_ACCENT), 0);
    lv_obj_set_style_border_width(obj_term_cursor, 1, 0);
    lv_obj_set_style_radius(obj_term_cursor, 0, 0);
    lv_obj_set_style_pad_all(obj_term_cursor, 0, 0);
    lv_obj_add_flag(obj_term_cursor, LV_OBJ_FLAG_HIDDEN);

    terminal_update_view();
}

/* =========================================================================
 * Criação da Janela de Monitor de Sistema (SysMon)
 * ========================================================================= */
static lv_obj_t *bar_ram = NULL;
static lv_obj_t *lbl_sys_info = NULL;

static void create_sysmon_window(void) {
    if (win_sysmon) return;

    win_sysmon = lv_win_create(lv_scr_act(), WIN_HEADER_H);
    setup_window(win_sysmon, 500, 340, 260, 130);
    add_window_controls(win_sysmon, "  Monitor do Sistema LiwusOS");

    lv_obj_t *content = lv_win_get_content(win_sysmon);
    lv_obj_set_style_pad_all(content, 14, 0);

    lbl_sys_info = lv_label_create(content);
    lv_label_set_text(lbl_sys_info, "Carregando informacoes do kernel...");

    lv_obj_t *lbl_ram_title = lv_label_create(content);
    lv_obj_align(lbl_ram_title, LV_ALIGN_TOP_LEFT, 0, 70);
    lv_label_set_text(lbl_ram_title, "Uso de Memoria RAM:");

    bar_ram = lv_bar_create(content);
    lv_obj_set_size(bar_ram, lv_pct(100), 22);
    lv_obj_align(bar_ram, LV_ALIGN_TOP_LEFT, 0, 95);
    lv_bar_set_range(bar_ram, 0, 100);
    lv_bar_set_value(bar_ram, 30, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar_ram, 2, 0);
    lv_obj_set_style_radius(bar_ram, 2, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar_ram, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_bg_color(bar_ram, lv_color_hex(BZ_ACCENT), LV_PART_INDICATOR);

    lv_obj_add_flag(win_sysmon, LV_OBJ_FLAG_HIDDEN);
}

/* =========================================================================
 * Criação da Janela da Calculadora
 * ========================================================================= */
static void calc_btn_cb(lv_event_t *e) {
    const char *txt = (const char *)lv_event_get_user_data(e);
    if (!txt) return;

    if (txt[0] >= '0' && txt[0] <= '9') {
        if (g_calc_new_num || strcmp(g_calc_str, "0") == 0) {
            strncpy(g_calc_str, txt, sizeof(g_calc_str) - 1);
            g_calc_new_num = false;
        } else if (strlen(g_calc_str) < sizeof(g_calc_str) - 2) {
            strcat(g_calc_str, txt);
        }
    } else if (txt[0] == 'C') {
        strcpy(g_calc_str, "0");
        g_calc_val = 0;
        g_calc_op = 0;
        g_calc_new_num = true;
    } else if (txt[0] == '+' || txt[0] == '-' || txt[0] == '*' || txt[0] == '/') {
        g_calc_val = (double)simple_atoi(g_calc_str);
        g_calc_op = txt[0];
        g_calc_new_num = true;
    } else if (txt[0] == '=') {
        double current = (double)simple_atoi(g_calc_str);
        double res = current;
        if (g_calc_op == '+') res = g_calc_val + current;
        else if (g_calc_op == '-') res = g_calc_val - current;
        else if (g_calc_op == '*') res = g_calc_val * current;
        else if (g_calc_op == '/' && current != 0) res = g_calc_val / current;

        lv_snprintf(g_calc_str, sizeof(g_calc_str), "%d", (int)res);
        g_calc_new_num = true;
    }

    if (lbl_calc_display) {
        lv_label_set_text(lbl_calc_display, g_calc_str);
    }
}

static void create_calc_window(void) {
    if (win_calc) return;

    win_calc = lv_win_create(lv_scr_act(), WIN_HEADER_H);
    setup_window(win_calc, 280, 360, 360, 150);
    add_window_controls(win_calc, "  Calculadora");

    lv_obj_t *content = lv_win_get_content(win_calc);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(content, 10, 0);

    lbl_calc_display = lv_label_create(content);
    lv_obj_set_size(lbl_calc_display, lv_pct(100), 40);
    lv_obj_set_style_text_align(lbl_calc_display, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_font(lbl_calc_display, &lv_font_montserrat_24, 0);
    lv_obj_set_style_bg_color(lbl_calc_display, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_bg_opa(lbl_calc_display, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(lbl_calc_display, lv_color_hex(BZ_BUTTON_LINE), 0);
    lv_obj_set_style_border_width(lbl_calc_display, 1, 0);
    lv_obj_set_style_radius(lbl_calc_display, 3, 0);
    lv_obj_set_style_pad_all(lbl_calc_display, 6, 0);
    lv_label_set_text(lbl_calc_display, g_calc_str);

    static const char *btn_labels[4][4] = {
        {"7", "8", "9", "/"},
        {"4", "5", "6", "*"},
        {"1", "2", "3", "-"},
        {"C", "0", "=", "+"}
    };

    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            lv_obj_t *btn = lv_btn_create(content);
            lv_obj_set_size(btn, 55, 48);
            lv_obj_set_pos(btn, c * 62, 55 + r * 56);
            bz_style_button(btn);

            if (r == 3 && c == 2) {
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x2980b9), 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(BZ_ACCENT), 0);
            } else if (c == 3) {
                lv_obj_set_style_bg_color(btn, lv_color_hex(0x3a4047), 0);
            } else if (r == 3 && c == 0) {
                lv_obj_set_style_text_color(btn, lv_color_hex(BZ_RED), 0);
            }

            lv_obj_t *lbl = lv_label_create(btn);
            lv_label_set_text(lbl, btn_labels[r][c]);
            lv_obj_center(lbl);

            lv_obj_add_event_cb(btn, calc_btn_cb, LV_EVENT_CLICKED, (void *)btn_labels[r][c]);
        }
    }

    lv_obj_add_flag(win_calc, LV_OBJ_FLAG_HIDDEN);
}

/* =========================================================================
 * Criação da Janela do Navegador Web (LiwusWeb)
 * ========================================================================= */
static void browser_go_cb(lv_event_t *e);

static void browser_navigate_async(void *arg) {
    char *href = (char *)arg;
    if (!href) return;
    if (strlen(href) >= sizeof(g_browser_url)) {
        kfree(href);
        html_view_show_message(obj_browser_view, "Endereco muito longo",
            "O endereco excede o limite de 4095 caracteres do LiwusWeb.",
            lv_color_hex(0xef4444));
        return;
    }
    if (href[0] == '#') {
        if (obj_browser_view) lv_obj_scroll_to_y(obj_browser_view, 0, LV_ANIM_ON);
        kfree(href);
        return;
    }
    strncpy(g_browser_url, href, sizeof(g_browser_url) - 1);
    g_browser_url[sizeof(g_browser_url) - 1] = '\0';
    kfree(href);
    if (ta_browser_url) {
        lv_textarea_set_text(ta_browser_url, g_browser_url);
        lv_textarea_set_cursor_pos(ta_browser_url, LV_TEXTAREA_CURSOR_LAST);
    }
    browser_go_cb(NULL);
}

static int browser_base64_value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return (int)(ch - 'A');
    if (ch >= 'a' && ch <= 'z') return (int)(ch - 'a') + 26;
    if (ch >= '0' && ch <= '9') return (int)(ch - '0') + 52;
    if (ch == '+' || ch == '-') return 62;
    if (ch == '/' || ch == '_') return 63;
    return -1;
}

/* Bing wraps result links in /ck/a and stores the destination in u=a1<base64>. */
static bool browser_unwrap_bing_result(const char *href, char *out, size_t out_sz) {
    static const char *const prefixes[] = {
        "https://www.bing.com/ck/a?", "http://www.bing.com/ck/a?",
        "https://bing.com/ck/a?", "http://bing.com/ck/a?"
    };
    if (!href || !href[0] || !out || out_sz == 0) return false;
    bool is_bing_redirect = false;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++) {
        if (strncmp(href, prefixes[i], strlen(prefixes[i])) == 0) {
            is_bing_redirect = true;
            break;
        }
    }
    if (!is_bing_redirect || !out || out_sz == 0) return false;

    const char *encoded = strstr(href, "u=a1");
    if (!encoded) return false;
    encoded += 4; /* Skip the Bing a1 marker; the remainder is base64url. */

    uint32_t bits = 0;
    unsigned bit_count = 0;
    size_t out_len = 0;
    for (const unsigned char *p = (const unsigned char *)encoded;
         *p && *p != '&' && *p != '#'; p++) {
        if (*p == '=') break;
        int value = browser_base64_value(*p);
        if (value < 0) return false;
        bits = (bits << 6) | (uint32_t)value;
        bit_count += 6;
        if (bit_count >= 8) {
            bit_count -= 8;
            if (out_len + 1 >= out_sz) return false;
            out[out_len++] = (char)(bits >> bit_count);
        }
    }
    out[out_len] = '\0';

    if (out_len < 8 ||
        (strncmp(out, "https://", 8) != 0 && strncmp(out, "http://", 7) != 0)) {
        out[0] = '\0';
        return false;
    }
    return true;
}

static void browser_on_link_click(const char *href, void *user_data) {
    (void)user_data;
    if (!href || !href[0]) return;
    char direct_url[BROWSER_URL_MAX];
    const char *navigate_to = href;
    if (browser_unwrap_bing_result(href, direct_url, sizeof(direct_url))) {
        navigate_to = direct_url;
    }
    size_t len = strlen(navigate_to);
    if (len >= BROWSER_URL_MAX) {
        html_view_show_message(obj_browser_view, "Endereco muito longo",
            "O endereco excede o limite de 4095 caracteres do LiwusWeb.",
            lv_color_hex(0xef4444));
        return;
    }
    char *copy = (char *)kmalloc(len + 1);
    if (!copy) return;
    memcpy(copy, navigate_to, len + 1);
    lv_async_call(browser_navigate_async, copy);
}

static void browser_update_window_title(const char *page_title) {
    if (!lbl_browser_win_title) return;
    if (page_title && page_title[0]) {
        char tbuf[128];
        lv_snprintf(tbuf, sizeof(tbuf), "  %.80s - LiwusWeb", page_title);
        lv_label_set_text(lbl_browser_win_title, tbuf);
    } else {
        lv_label_set_text(lbl_browser_win_title, "  LiwusWeb - Navegador de Internet");
    }
}

static void browser_update_nav_buttons(void) {
    if (btn_browser_back) {
        if (g_browser_hist_idx > 0) lv_obj_clear_state(btn_browser_back, LV_STATE_DISABLED);
        else lv_obj_add_state(btn_browser_back, LV_STATE_DISABLED);
    }
    if (btn_browser_fwd) {
        if (g_browser_hist_idx >= 0 && g_browser_hist_idx + 1 < g_browser_hist_count) {
            lv_obj_clear_state(btn_browser_fwd, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(btn_browser_fwd, LV_STATE_DISABLED);
        }
    }
}

static void browser_history_push(const char *url) {
    if (!url || !url[0]) return;
    if (g_browser_hist_idx >= 0 && strcmp(g_browser_hist[g_browser_hist_idx], url) == 0) {
        browser_update_nav_buttons();
        return;
    }
    if (g_browser_hist_idx + 1 >= BROWSER_HIST_MAX) {
        for (int i = 1; i < BROWSER_HIST_MAX; i++) {
            strcpy(g_browser_hist[i - 1], g_browser_hist[i]);
        }
        g_browser_hist_idx = BROWSER_HIST_MAX - 2;
    }
    g_browser_hist_idx++;
    strncpy(g_browser_hist[g_browser_hist_idx], url, sizeof(g_browser_hist[0]) - 1);
    g_browser_hist[g_browser_hist_idx][sizeof(g_browser_hist[0]) - 1] = '\0';
    g_browser_hist_count = g_browser_hist_idx + 1;
    browser_update_nav_buttons();
}

static void browser_load_current_url(bool push_history);

static void browser_back_cb(lv_event_t *e) {
    (void)e;
    if (g_browser_hist_idx <= 0) return;
    g_browser_hist_idx--;
    strncpy(g_browser_url, g_browser_hist[g_browser_hist_idx], sizeof(g_browser_url) - 1);
    g_browser_url[sizeof(g_browser_url) - 1] = '\0';
    if (ta_browser_url) {
        lv_textarea_set_text(ta_browser_url, g_browser_url);
        lv_textarea_set_cursor_pos(ta_browser_url, LV_TEXTAREA_CURSOR_LAST);
    }
    browser_update_nav_buttons();
    browser_load_current_url(false);
}

static void browser_fwd_cb(lv_event_t *e) {
    (void)e;
    if (g_browser_hist_idx < 0 || g_browser_hist_idx + 1 >= g_browser_hist_count) return;
    g_browser_hist_idx++;
    strncpy(g_browser_url, g_browser_hist[g_browser_hist_idx], sizeof(g_browser_url) - 1);
    g_browser_url[sizeof(g_browser_url) - 1] = '\0';
    if (ta_browser_url) {
        lv_textarea_set_text(ta_browser_url, g_browser_url);
        lv_textarea_set_cursor_pos(ta_browser_url, LV_TEXTAREA_CURSOR_LAST);
    }
    browser_update_nav_buttons();
    browser_load_current_url(false);
}

static void browser_reload_cb(lv_event_t *e) {
    (void)e;
    browser_load_current_url(false);
}

static void web_cache_store(const char *url, uint8_t *data, size_t len) {
    if (!url || !data || len == 0 || len > 1024U * 1024U) return;

    while (g_web_cache_bytes + len > WEB_CACHE_MAX_TOTAL) {
        int evict = -1;
        uint32_t oldest = 0xFFFFFFFFU;
        for (int i = 0; i < WEB_CACHE_SLOTS; i++) {
            if (g_web_cache[i].used && g_web_cache[i].last_used < oldest) {
                oldest = g_web_cache[i].last_used;
                evict = i;
            }
        }
        if (evict < 0) break;
        g_web_cache_bytes -= g_web_cache[evict].len;
        kfree(g_web_cache[evict].data);
        memset(&g_web_cache[evict], 0, sizeof(g_web_cache[evict]));
    }

    int slot = -1;
    uint32_t oldest = 0xFFFFFFFFU;
    for (int i = 0; i < WEB_CACHE_SLOTS; i++) {
        if (!g_web_cache[i].used) {
            slot = i;
            break;
        }
        if (g_web_cache[i].last_used < oldest) {
            oldest = g_web_cache[i].last_used;
            slot = i;
        }
    }
    if (slot < 0) return;

    if (g_web_cache[slot].used) {
        g_web_cache_bytes -= g_web_cache[slot].len;
        kfree(g_web_cache[slot].data);
        memset(&g_web_cache[slot], 0, sizeof(g_web_cache[slot]));
    }

    strncpy(g_web_cache[slot].url, url, sizeof(g_web_cache[slot].url) - 1);
    g_web_cache[slot].url[sizeof(g_web_cache[slot].url) - 1] = '\0';
    g_web_cache[slot].data = data;
    g_web_cache[slot].len = len;
    g_web_cache[slot].last_used = ++g_web_cache_clock;
    g_web_cache[slot].used = true;
    g_web_cache_bytes += len;
}

static bool browser_fetch_resource(const char *url, uint8_t **out_data, size_t *out_len,
                                   bool *out_caller_free, void *user_data) {
    (void)user_data;
    if (!url || !out_data || !out_len || !out_caller_free) return false;
    *out_data = NULL;
    *out_len = 0;
    *out_caller_free = false;

    for (int i = 0; i < WEB_CACHE_SLOTS; i++) {
        if (g_web_cache[i].used && strcmp(g_web_cache[i].url, url) == 0) {
            g_web_cache[i].last_used = ++g_web_cache_clock;
            *out_data = g_web_cache[i].data;
            *out_len = g_web_cache[i].len;
            *out_caller_free = false;
            return true;
        }
    }

    uint8_t *raw = NULL;
    size_t sz = 0;
    int status = 0;
    char phase[48];
    g_browser_resource_number++;
    lv_snprintf(phase, sizeof(phase), "Recurso %u", g_browser_resource_number);
    g_browser_progress_last = 0;
    int res = http_get_mem_progress(url, &raw, &sz, &status,
                                    browser_http_status, NULL,
                                    browser_http_progress, phase);
    net_poll();
    usb_ehci_service();
    usb_xhci_poll();

    if (res <= 0 || !raw || sz == 0 || status >= 400) {
        if (raw) kfree(raw);
        return false;
    }

    if (sz <= 1024U * 1024U) {
        web_cache_store(url, raw, sz);
        /* Verify it actually sits in cache; if so cache owns it. */
        for (int i = 0; i < WEB_CACHE_SLOTS; i++) {
            if (g_web_cache[i].used && g_web_cache[i].data == raw) {
                *out_data = raw;
                *out_len = sz;
                *out_caller_free = false;
                return true;
            }
        }
    }

    *out_data = raw;
    *out_len = sz;
    *out_caller_free = true;
    return true;
}

static void web_input_deleted_cb(lv_event_t *e) {
    lv_obj_t *ta = lv_event_get_target(e);
    if (g_active_web_input == ta) g_active_web_input = NULL;
}

static void browser_on_input_focus(lv_obj_t *ta, void *user_data) {
    (void)user_data;
    if (!ta) return;
    if (g_active_web_input && g_active_web_input != ta) {
        lv_obj_clear_state(g_active_web_input, LV_STATE_FOCUSED);
    }
    g_active_web_input = ta;
    g_browser_url_focused = false;
    if (ta_browser_url) lv_obj_clear_state(ta_browser_url, LV_STATE_FOCUSED);
    lv_obj_add_state(ta, LV_STATE_FOCUSED);
    lv_obj_remove_event_cb(ta, web_input_deleted_cb);
    lv_obj_add_event_cb(ta, web_input_deleted_cb, LV_EVENT_DELETE, NULL);
}

static void browser_render_status_cb(const char *msg, void *user_data) {
    (void)user_data;
    if (lbl_browser_win_title && msg) {
        char buf[128];
        lv_snprintf(buf, sizeof(buf), "  %s - LiwusWeb", msg);
        lv_label_set_text(lbl_browser_win_title, buf);
    }
    lv_refr_now(NULL);
    vga_flip();
}

static void browser_wiki_cb(lv_event_t *e) {
    (void)e;
    if (!ta_browser_url) return;
    lv_textarea_set_text(ta_browser_url, "https://pt.wikipedia.org/wiki/Brasil");
    lv_textarea_set_cursor_pos(ta_browser_url, LV_TEXTAREA_CURSOR_LAST);
    browser_go_cb(NULL);
}

static bool browser_runtime_channels_open(void) {
    if (!g_web_runtime_commands)
        g_web_runtime_commands = vfs_open("/ipc/webjs-in");
    if (!g_web_runtime_notifications)
        g_web_runtime_notifications = vfs_open("/ipc/webjs-out");
    if (!g_web_runtime_fetch_requests)
        g_web_runtime_fetch_requests = vfs_open("/ipc/webjs-fetch-in");
    if (!g_web_runtime_fetch_responses)
        g_web_runtime_fetch_responses = vfs_open("/ipc/webjs-fetch-out");
    return g_web_runtime_commands && g_web_runtime_notifications &&
           g_web_runtime_fetch_requests && g_web_runtime_fetch_responses;
}

static void browser_runtime_send(const char *message) {
    if (!message || !browser_runtime_channels_open()) return;
    (void)write_fs(g_web_runtime_commands, 0, (uint32_t)strlen(message),
                   (uint8_t *)message);
}

static void browser_on_dom_event(const char *element_id, const char *event,
                                 const char *value, void *user_data) {
    (void)user_data;
    if (g_web_runtime_pid <= 0 || !element_id || !event) return;
    if (strcmp(event, "change") == 0 || strcmp(event, "click") == 0)
        g_active_web_input = NULL;
    for (size_t i = 0; element_id[i]; ++i) {
        char c = element_id[i];
        if (c <= ' ' || c == '\n' || c == '\r') return;
    }

    char message[512];
    if (strcmp(event, "input") == 0 && value) {
        size_t out = 0;
        int n = lv_snprintf(message, sizeof(message), "input %s ", element_id);
        if (n < 0 || (size_t)n >= sizeof(message)) return;
        out = (size_t)n;
        for (size_t i = 0; value[i] && out + 2 < sizeof(message); ++i) {
            char c = value[i];
            message[out++] = (c == '\n' || c == '\r') ? ' ' : c;
        }
        message[out++] = '\n';
        message[out] = '\0';
    } else {
        if (strcmp(event, "click") != 0 && strcmp(event, "change") != 0)
            return;
        int n = lv_snprintf(message, sizeof(message), "%s %s\n", event,
                            element_id);
        if (n < 0 || (size_t)n >= sizeof(message)) return;
    }
    browser_runtime_send(message);
}

static void browser_stop_runtime(void) {
    if (g_web_runtime_pid <= 0) return;
    browser_runtime_send("quit\n");
    int status = -1;
    for (unsigned i = 0; i < 100; ++i) {
        int waited = task_waitpid_kernel(g_web_runtime_pid, &status, 1);
        if (waited == g_web_runtime_pid) {
            g_web_runtime_pid = 0;
            return;
        }
        task_sleep_ms(10);
    }
    sys_kill_by_pid(g_web_runtime_pid);
    (void)task_waitpid_kernel(g_web_runtime_pid, &status, 0);
    g_web_runtime_pid = 0;
}

static uint8_t *browser_read_runtime_dom(size_t *result_length) {
    fs_node_t *node = vfs_open(g_web_runtime_output_path);
    if (!node) return NULL;
    size_t bytes = node->length;
    uint8_t *result = NULL;
    if ((node->flags & FS_FILE) && bytes && bytes <= 4U * 1024U * 1024U) {
        result = kmalloc(bytes + 1);
        if (result && read_fs(node, 0, (uint32_t)bytes, result) != bytes) {
            kfree(result);
            result = NULL;
        }
        if (result) {
            result[bytes] = 0;
            *result_length = bytes;
        }
    }
    vfs_node_put(node);
    return result;
}

/* Page scripts live in an isolated ring-3 process. The IPCFS channels carry
 * input events and DOM-ready notifications; the rendered snapshot uses VFS. */
static uint8_t *browser_run_scripts(const uint8_t *source, size_t length,
                                    size_t *result_length) {
    (void)result_length;
    const size_t limit = 4U * 1024U * 1024U;
    if (!length || length > limit) return NULL;
    bool has_script = false;
    for (size_t i = 0; i + 7 <= length; ++i) {
        const char *tag = "<script";
        size_t j = 0;
        for (; j < 7; ++j) {
            unsigned char c = source[i + j];
            if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            if (c != (unsigned char)tag[j]) break;
        }
        if (j == 7) { has_script = true; break; }
    }
    if (!has_script) return NULL;
    browser_stop_runtime();
    if (!browser_runtime_channels_open()) return NULL;
    (void)vfs_truncate(g_web_runtime_commands);
    (void)vfs_truncate(g_web_runtime_notifications);
    (void)vfs_truncate(g_web_runtime_fetch_requests);
    (void)vfs_truncate(g_web_runtime_fetch_responses);

    fs_node_t *directory = vfs_open("/.web-runtime");
    if (directory) {
        vfs_node_put(directory);
    } else if (vfs_mkdir("/.web-runtime", 1000, 1000, 0700) < 0) {
        return NULL;
    }

    (void)vfs_unlink(g_web_runtime_input_path);
    fs_node_t *node = vfs_create_as(g_web_runtime_input_path, FS_FILE,
                                    1000, 1000, 0600);
    if (!node) return NULL;
    uint32_t written = write_fs(node, 0, (uint32_t)length, (uint8_t *)source);
    vfs_node_put(node);
    if (written != length) return NULL;
    char generation[16];
    lv_snprintf(generation, sizeof(generation), "%u",
                g_web_runtime_generation);
    char *args[] = {"qjs", "--browser-runtime",
                    (char *)g_web_runtime_input_path,
                    (char *)g_web_runtime_output_path, generation,
                    (char *)g_browser_url, NULL};
    extern int launch_initrd_program_argv(const char *, char *const []);
    int foreground = last_foreground_pid;
    int pid = launch_initrd_program_argv("/bin/qjs.lxe", args);
    last_foreground_pid = foreground;
    if (pid <= 0) return NULL;
    g_web_runtime_pid = pid;
    /* The first page snapshot is published asynchronously. This keeps the
     * desktop event loop running while QuickJS evaluates large module graphs. */
    return NULL;
}

static bool browser_fetch_script_resource(const char *url, uint8_t **out_data,
                                          size_t *out_len, bool *out_caller_free,
                                          void *user_data) {
    (void)user_data;
    if (!url || !out_data || !out_len || !out_caller_free) return false;
    *out_data = NULL;
    *out_len = 0;
    *out_caller_free = false;

    for (int i = 0; i < WEB_CACHE_SLOTS; i++) {
        if (g_web_cache[i].used && strcmp(g_web_cache[i].url, url) == 0) {
            g_web_cache[i].last_used = ++g_web_cache_clock;
            *out_data = g_web_cache[i].data;
            *out_len = g_web_cache[i].len;
            *out_caller_free = false;
            return true;
        }
    }

    uint8_t *raw = NULL;
    size_t sz = 0;
    int status = 0;
    int res = http_get_mem_progress(url, &raw, &sz, &status,
                                    NULL, NULL, NULL, NULL);
    net_poll();
    usb_ehci_service();
    usb_xhci_poll();

    if (res <= 0 || !raw || sz == 0 || status >= 400) {
        if (raw) kfree(raw);
        return false;
    }

    /* Do not store external JS bundles in g_web_cache so they never evict
     * stylesheets or images needed by the post-JS re-render pass. */
    *out_data = raw;
    *out_len = sz;
    *out_caller_free = true;
    return true;
}

static uint8_t *browser_execute_scripts(const uint8_t *source, size_t length,
                                       const char *base_url,
                                       size_t *result_length) {
    uint8_t *expanded = NULL;
    size_t expanded_length = 0;
    int expand_rc = html_view_inline_external_scripts(
        (const char *)source, length, base_url, browser_fetch_script_resource, NULL,
        &expanded, &expanded_length);
    if (expand_rc > 0 && expanded) {
        uint8_t *result = browser_run_scripts(expanded, expanded_length,
                                             result_length);
        kfree(expanded);
        return result;
    }
    return browser_run_scripts(source, length, result_length);
}

static void browser_render_document(const uint8_t *body, size_t size) {
    if (!body || !obj_browser_view) return;
    char page_title[96];
    int vw = (int)lv_obj_get_width(obj_browser_view) - 24;
    if (vw < 300) vw = 760;

    html_view_opts_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.on_link = browser_on_link_click;
    opts.fetch_resource = browser_fetch_resource;
    opts.on_input_focus = browser_on_input_focus;
    opts.on_dom_event = browser_on_dom_event;
    opts.on_status = browser_render_status_cb;
    opts.viewport_w = vw;
    opts.viewport_h = (int)lv_obj_get_height(obj_browser_view);
    opts.user_data = NULL;
    browser_http_status("Renderizando HTML/CSS/imagens", NULL);
    html_view_render(obj_browser_view, (const char *)body, size, g_browser_url,
                     page_title, sizeof(page_title), &opts);
    browser_update_window_title(page_title);
    lv_refr_now(NULL);
    vga_flip();
}

static void browser_runtime_poll_update(void) {
    if (g_web_runtime_pid <= 0 || !g_web_runtime_notifications ||
        !obj_browser_view) return;
    uint8_t notifications[32];
    uint32_t count = read_fs(g_web_runtime_notifications, 0,
                             sizeof(notifications), notifications);
    if (!count) {
        int status = -1;
        int pid = g_web_runtime_pid;
        if (pid > 0 && task_waitpid_kernel(pid, &status, 1) == pid)
            g_web_runtime_pid = 0;
        return;
    }
    /* Keep the native editor and caret alive while the user is typing. The
     * worker still receives its ack; the next click/change renders the newest
     * DOM snapshot after focus leaves the field. */
    if (g_active_web_input) {
        browser_runtime_send("ack\n");
        return;
    }
    /* Free the previous widget tree's page_mem BEFORE reading the 2-3 MB
     * runtime DOM snapshot from VFS so KHeap does not peak with two full
     * page trees plus the serialized DOM buffer simultaneously. */
    lv_obj_clean(obj_browser_view);
    size_t html_length = 0;
    uint8_t *html = browser_read_runtime_dom(&html_length);
    (void)vfs_unlink(g_web_runtime_output_path);
    if (html) {
        browser_render_document(html, html_length);
        kfree(html);
        browser_runtime_send("ack\n");
    }
}

static void browser_fetch_progress(size_t received, size_t total, void *data) {
    (void)data;
    uint64_t flags = task_scheduler_lock();
    g_browser_fetch_received = received;
    g_browser_fetch_total = total;
    task_scheduler_unlock(flags);
}

static bool browser_resolve_js_fetch_url(const char *input, char *output,
                                         size_t capacity) {
    if (!input || !input[0] || !output || capacity < 8) return false;
    if (strstr(input, "://")) {
        strncpy(output, input, capacity - 1);
        output[capacity - 1] = '\0';
        return true;
    }
    const char *scheme = strstr(g_browser_url, "://");
    if (!scheme) return false;
    if (input[0] == '/' && input[1] == '/') {
        size_t scheme_len = (size_t)(scheme - g_browser_url);
        if (scheme_len + strlen(input) + 2 > capacity) return false;
        memcpy(output, g_browser_url, scheme_len);
        output[scheme_len] = ':';
        strcpy(output + scheme_len + 1, input);
        return true;
    }
    const char *authority_end = scheme + 3;
    while (*authority_end && *authority_end != '/' && *authority_end != '?' &&
           *authority_end != '#') authority_end++;
    size_t origin_len = (size_t)(authority_end - g_browser_url);
    if (input[0] == '/') {
        if (origin_len + strlen(input) + 1 > capacity) return false;
        memcpy(output, g_browser_url, origin_len);
        strcpy(output + origin_len, input);
        return true;
    }
    size_t base_path_len = strlen(g_browser_url);
    for (size_t i = origin_len; i < base_path_len; ++i) {
        if (g_browser_url[i] == '?' || g_browser_url[i] == '#') {
            base_path_len = i;
            break;
        }
    }
    if (input[0] == '?') {
        if (base_path_len + strlen(input) + 1 > capacity) return false;
        memcpy(output, g_browser_url, base_path_len);
        strcpy(output + base_path_len, input);
        return true;
    }
    const char *last_slash = NULL;
    for (size_t i = origin_len; i < base_path_len; ++i)
        if (g_browser_url[i] == '/') last_slash = g_browser_url + i;
    size_t directory_len = last_slash
                         ? (size_t)(last_slash - g_browser_url) + 1
                         : origin_len;
    bool add_slash = directory_len == origin_len;
    if (directory_len + (add_slash ? 1U : 0U) + strlen(input) + 1 > capacity)
        return false;
    memcpy(output, g_browser_url, directory_len);
    if (add_slash) output[directory_len++] = '/';
    strcpy(output + directory_len, input);
    return true;
}

static void browser_run_js_fetch(const char *request) {
    if (!request || !g_web_runtime_fetch_responses) return;
    uint32_t generation = 0;
    uint32_t request_id = 0;
    char raw_url[BROWSER_URL_MAX];
    const char *first = strchr(request, ' ');
    if (!first) return;
    generation = (uint32_t)atoi(request);
    const char *id_start = first + 1;
    const char *second = strchr(id_start, ' ');
    if (!second) return;
    request_id = (uint32_t)atoi(id_start);
    const char *url_start = second + 1;
    size_t url_len = strlen(url_start);
    while (url_len && (url_start[url_len - 1] == '\n' ||
                       url_start[url_len - 1] == '\r')) url_len--;
    if (!request_id || !generation || generation != g_web_runtime_generation ||
        url_len == 0 || url_len >= sizeof(raw_url)) return;
    memcpy(raw_url, url_start, url_len);
    raw_url[url_len] = '\0';

    char url[BROWSER_URL_MAX];
    char path[96];
    bool url_valid = browser_resolve_js_fetch_url(raw_url, url, sizeof(url));
    lv_snprintf(path, sizeof(path), "/.web-runtime/fetch-%u-%u.dat",
                generation, request_id);

    uint8_t *body = NULL;
    size_t body_size = 0;
    int status = 0;
    bool module_fetch = request_id >= BROWSER_QJS_MODULE_FETCH_ID_BASE;
    unsigned max_attempts = module_fetch ? 2U : 1U;
    int result = -1;
    for (unsigned attempt = 0; attempt < max_attempts; ++attempt) {
        if (body) {
            kfree(body);
            body = NULL;
        }
        body_size = 0;
        status = 0;
        result = url_valid
            ? http_get_mem_progress_limit(url, &body, &body_size, &status,
                                          NULL, NULL, NULL, NULL,
                                          1024U * 1024U)
            : -1;
        /* A failed DNS/TLS connection has no HTTP status. Give imported
         * modules one bounded retry so a transient lookup failure does not
         * abort the page's entire script graph. HTTP errors are not retried. */
        if (result > 0 || status != 0 || attempt + 1U >= max_attempts)
            break;
    }

    (void)vfs_unlink(path);
    fs_node_t *file = vfs_create_as(path, FS_FILE, 1000, 1000, 0600);
    if (file && body && result > 0) {
        uint32_t written = write_fs(file, 0, (uint32_t)body_size, body);
        if (written != body_size) status = 0;
    } else {
        status = 0;
        body_size = 0;
    }
    if (file) vfs_node_put(file);
    if (body) kfree(body);

    char response[160];
    if (generation != g_web_runtime_generation) {
        (void)vfs_unlink(path);
        return;
    }
    int length = lv_snprintf(response, sizeof(response), "%u %u %d %s\n",
                             generation, request_id, status, path);
    if (length > 0 && (size_t)length < sizeof(response))
        (void)write_fs(g_web_runtime_fetch_responses, 0, (uint32_t)length,
                       (uint8_t *)response);
}

static void browser_fetch_worker(void) {
    char requests[4096];
    size_t request_length = 0;
    requests[0] = '\0';
    for (;;) {
        uint64_t flags = task_scheduler_lock();
        bool page_fetch = g_browser_fetch_state == 1;
        char url[BROWSER_URL_MAX];
        if (page_fetch) memcpy(url, g_browser_fetch_url, sizeof(url));
        task_scheduler_unlock(flags);
        if (page_fetch) {
            uint8_t *body = NULL;
            size_t size = 0;
            int status = 0;
            int result = http_get_mem_progress_limit(
                url, &body, &size, &status, NULL, NULL,
                browser_fetch_progress, NULL, BROWSER_DOCUMENT_MAX);
            flags = task_scheduler_lock();
            g_browser_fetch_body = (result > 0) ? body : NULL;
            g_browser_fetch_size = (result > 0) ? size : 0;
            g_browser_fetch_status = status;
            g_browser_fetch_state = 2;
            task_scheduler_unlock(flags);
            continue;
        }
        if (g_web_runtime_fetch_requests) {
            uint8_t incoming[512];
            uint32_t count = read_fs(g_web_runtime_fetch_requests, 0,
                                     sizeof(incoming), incoming);
            if (count && count < sizeof(requests) - request_length) {
                memcpy(requests + request_length, incoming, count);
                request_length += count;
                requests[request_length] = '\0';
                char *newline;
                while ((newline = strchr(requests, '\n')) != NULL) {
                    *newline = '\0';
                    browser_run_js_fetch(requests);
                    size_t consumed = (size_t)(newline + 1 - requests);
                    request_length -= consumed;
                    memmove(requests, newline + 1, request_length);
                    requests[request_length] = '\0';
                }
            }
        }
        task_sleep_ms(10);
    }
}

static void browser_start_fetch(void) {
    uint64_t flags = task_scheduler_lock();
    if (g_browser_fetch_state == 1) {
        task_scheduler_unlock(flags);
        return;
    }
    memcpy(g_browser_fetch_url, g_browser_url, sizeof(g_browser_fetch_url));
    g_browser_fetch_url[sizeof(g_browser_fetch_url) - 1] = '\0';
    g_browser_fetch_received = 0;
    g_browser_fetch_total = 0;
    g_browser_fetch_state = 1;
    task_scheduler_unlock(flags);
    if (g_browser_fetch_tid <= 0) {
        g_browser_fetch_tid = create_task_named(browser_fetch_worker, "web-fetch");
        if (g_browser_fetch_tid <= 0) {
            flags = task_scheduler_lock();
            g_browser_fetch_state = 0;
            task_scheduler_unlock(flags);
            html_view_show_message(obj_browser_view, "Falha ao carregar pagina",
                                   "Nao foi possivel iniciar a tarefa de rede.",
                                   lv_color_hex(0xef4444));
        }
    }
}

static void browser_fetch_poll(void) {
    uint64_t flags = task_scheduler_lock();
    int state = g_browser_fetch_state;
    size_t total = g_browser_fetch_total;
    size_t received = g_browser_fetch_received;
    uint8_t *body = NULL;
    size_t size = 0;
    int status = 0;
    bool pending = g_browser_fetch_pending;
    char pending_url[BROWSER_URL_MAX];
    if (pending) memcpy(pending_url, g_browser_pending_url, sizeof(pending_url));
    if (state == 2) {
        body = g_browser_fetch_body;
        size = g_browser_fetch_size;
        status = g_browser_fetch_status;
        g_browser_fetch_body = NULL;
        g_browser_fetch_size = 0;
        g_browser_fetch_state = 0;
        g_browser_fetch_pending = false;
    }
    task_scheduler_unlock(flags);

    if (state == 1) {
        browser_http_progress(received, total, "HTML");
        return;
    }
    if (state != 2) return;
    (void)status;

    if (pending) {
        memcpy(g_browser_url, pending_url, sizeof(g_browser_url));
        g_browser_url[sizeof(g_browser_url) - 1] = '\0';
        if (body) kfree(body);
        browser_start_fetch();
        return;
    }

    if (body && size) {
        /* Paint the server HTML/CSS first. Fetching a page's external script
         * graph can take many network round trips; keep the browser visibly
         * useful while the isolated ring-3 JavaScript runtime is prepared. */
        browser_render_document(body, size);
        serial_printf("[WEB] Pagina renderizada: %s\n", g_browser_url);

        browser_http_status("Executando JavaScript", NULL);
        size_t transformed_size = 0;
        uint8_t *transformed = browser_execute_scripts(body, size,
                                                      g_browser_url,
                                                      &transformed_size);
        if (transformed) {
            browser_render_document(transformed, transformed_size);
            kfree(transformed);
        } else {
            browser_update_window_title(NULL);
            lv_refr_now(NULL);
            vga_flip();
        }
        kfree(body);
        return;
    }

    if (body) kfree(body);
    char err_buf[320];
    lv_snprintf(err_buf, sizeof(err_buf),
                "Nao foi possivel carregar: %s\nVerifique o endereco, DNS e servidor HTTP/HTTPS.",
                g_browser_url);
    html_view_show_message(obj_browser_view, "Falha ao carregar pagina",
                           err_buf, lv_color_hex(0xef4444));
    lv_refr_now(NULL);
    vga_flip();
}

static void browser_load_current_url(bool push_history) {
    if (!ta_browser_url || !obj_browser_view) return;
    const char *raw_url = lv_textarea_get_text(ta_browser_url);
    char url[BROWSER_URL_MAX];
    browser_url_normalize_input(raw_url, url, sizeof(url));
    if (url[0] == '\0') return;

    browser_stop_runtime();
    g_web_runtime_generation++;
    if (!g_web_runtime_generation) g_web_runtime_generation++;
    strncpy(g_browser_url, url, sizeof(g_browser_url) - 1);
    g_browser_url[sizeof(g_browser_url) - 1] = '\0';
    if (strcmp(raw_url, g_browser_url) != 0) {
        lv_textarea_set_text(ta_browser_url, g_browser_url);
        lv_textarea_set_cursor_pos(ta_browser_url, LV_TEXTAREA_CURSOR_LAST);
    }
    g_active_web_input = NULL;

    if (!net_has_ip()) {
        browser_update_window_title(NULL);
        html_view_show_message(obj_browser_view,
            "Rede sem IP ou desconectada!",
            "Verifique o cabo de rede ou configure o IP no Konsole:\n"
            "  - Para obter via DHCP: digite 'dhcp'\n"
            "  - Para IP estatico: digite 'ifconfig <ip> <mascara> <gateway>'\n"
            "    (Exemplo: ifconfig 192.168.1.150 255.255.255.0 192.168.1.1)",
            lv_color_hex(0xf59e0b));
        return;
    }
    if (push_history) browser_history_push(g_browser_url);

    browser_update_window_title(NULL);
    g_browser_progress_last = 0;
    g_browser_resource_number = 0;
    html_view_show_message(obj_browser_view, "Conectando ao site",
                           g_browser_url, lv_color_hex(0x0b57d0));
    uint64_t flags = task_scheduler_lock();
    int fetch_state = g_browser_fetch_state;
    if (fetch_state == 1) {
        memcpy(g_browser_pending_url, g_browser_url, sizeof(g_browser_pending_url));
        g_browser_fetch_pending = true;
    }
    uint8_t *stale_body = NULL;
    if (fetch_state == 2) {
        stale_body = g_browser_fetch_body;
        g_browser_fetch_body = NULL;
        g_browser_fetch_size = 0;
        g_browser_fetch_state = 0;
    }
    task_scheduler_unlock(flags);
    if (stale_body) kfree(stale_body);
    if (fetch_state != 1) {
        browser_start_fetch();
    }
    browser_http_status("Requisicao em segundo plano", NULL);
    lv_refr_now(NULL);
    vga_flip();
}

static void browser_go_cb(lv_event_t *e) {
    (void)e;
    browser_load_current_url(true);
}

static void create_browser_window(void) {
    if (win_browser) return;

    uint32_t sw = vga_get_fb_width();
    uint32_t sh = vga_get_fb_height();
    if (sw == 0) sw = 1024;
    if (sh == 0) sh = 768;

    int bw = (sw >= 920) ? ((int)sw - 24) : ((int)sw - 40);
    int bh = (sh >= 660) ? ((int)sh - 80) : ((int)sh - 130);
    int bx = ((int)sw - bw) / 2;
    int by = 24;
    if (bx < 12) bx = 12;

    win_browser = lv_win_create(lv_scr_act(), WIN_HEADER_H);
    setup_window(win_browser, bw, bh, bx, by);
    lbl_browser_win_title = add_window_controls(win_browser, "LiwusWeb - Navegador de Internet");

    lv_obj_t *content = lv_win_get_content(win_browser);
    lv_obj_set_style_pad_all(content, 6, 0);
    lv_obj_set_style_pad_row(content, 6, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(content, win_focus_event_cb, LV_EVENT_PRESSED, win_browser);

    lv_obj_t *bar = lv_obj_create(content);
    lv_obj_set_size(bar, lv_pct(100), 42);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_set_style_pad_column(bar, 4, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_add_event_cb(bar, win_focus_event_cb, LV_EVENT_PRESSED, win_browser);

    btn_browser_back = lv_btn_create(bar);
    lv_obj_set_size(btn_browser_back, 32, 32);
    bz_style_button(btn_browser_back);
    lv_obj_t *lbl_back = lv_label_create(btn_browser_back);
    lv_label_set_text(lbl_back, LV_SYMBOL_LEFT);
    lv_obj_center(lbl_back);
    lv_obj_add_event_cb(btn_browser_back, browser_back_cb, LV_EVENT_CLICKED, NULL);

    btn_browser_fwd = lv_btn_create(bar);
    lv_obj_set_size(btn_browser_fwd, 32, 32);
    bz_style_button(btn_browser_fwd);
    lv_obj_t *lbl_fwd = lv_label_create(btn_browser_fwd);
    lv_label_set_text(lbl_fwd, LV_SYMBOL_RIGHT);
    lv_obj_center(lbl_fwd);
    lv_obj_add_event_cb(btn_browser_fwd, browser_fwd_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_reload = lv_btn_create(bar);
    lv_obj_set_size(btn_reload, 32, 32);
    bz_style_button(btn_reload);
    lv_obj_t *lbl_reload = lv_label_create(btn_reload);
    lv_label_set_text(lbl_reload, LV_SYMBOL_REFRESH);
    lv_obj_center(lbl_reload);
    lv_obj_add_event_cb(btn_reload, browser_reload_cb, LV_EVENT_CLICKED, NULL);

    browser_update_nav_buttons();

    ta_browser_url = lv_textarea_create(bar);
    lv_obj_set_height(ta_browser_url, 32);
    lv_obj_set_flex_grow(ta_browser_url, 1);
    lv_textarea_set_one_line(ta_browser_url, true);
    lv_textarea_set_max_length(ta_browser_url, sizeof(g_browser_url) - 1);
    lv_textarea_set_text(ta_browser_url, g_browser_url);
    lv_obj_set_style_text_font(ta_browser_url, &g_html_font_14, 0);
    lv_obj_set_style_radius(ta_browser_url, 3, 0);
    lv_obj_set_style_pad_ver(ta_browser_url, 6, 0);
    lv_obj_set_style_bg_color(ta_browser_url, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_text_color(ta_browser_url, lv_color_hex(BZ_TEXT), 0);
    lv_obj_set_style_border_width(ta_browser_url, 1, 0);
    lv_obj_set_style_border_color(ta_browser_url, lv_color_hex(BZ_BUTTON_LINE), 0);
    lv_obj_set_style_border_color(ta_browser_url, lv_color_hex(BZ_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(ta_browser_url, 1, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(ta_browser_url, lv_color_hex(BZ_ACCENT), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ta_browser_url, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta_browser_url, browser_url_bar_click_cb, LV_EVENT_PRESSED, win_browser);
    lv_obj_add_event_cb(ta_browser_url, browser_url_bar_click_cb, LV_EVENT_CLICKED, win_browser);

    lv_obj_t *btn_go = lv_btn_create(bar);
    lv_obj_set_height(btn_go, 32);
    bz_style_button(btn_go);
    lv_obj_t *lbl_go = lv_label_create(btn_go);
    lv_label_set_text(lbl_go, "Ir");
    lv_obj_center(lbl_go);
    lv_obj_add_event_cb(btn_go, win_focus_event_cb, LV_EVENT_PRESSED, win_browser);
    lv_obj_add_event_cb(btn_go, browser_go_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_wiki = lv_btn_create(bar);
    lv_obj_set_height(btn_wiki, 32);
    bz_style_button(btn_wiki);
    lv_obj_t *lbl_wiki = lv_label_create(btn_wiki);
    lv_label_set_text(lbl_wiki, "Wiki");
    lv_obj_center(lbl_wiki);
    lv_obj_add_event_cb(btn_wiki, win_focus_event_cb, LV_EVENT_PRESSED, win_browser);
    lv_obj_add_event_cb(btn_wiki, browser_wiki_cb, LV_EVENT_CLICKED, NULL);

    obj_browser_view = lv_obj_create(content);
    lv_obj_set_width(obj_browser_view, lv_pct(100));
    lv_obj_set_flex_grow(obj_browser_view, 1);
    lv_obj_set_flex_flow(obj_browser_view, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(obj_browser_view, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(obj_browser_view, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj_browser_view, lv_color_hex(BZ_TITLE_LINE), 0);
    lv_obj_set_style_border_width(obj_browser_view, 1, 0);
    lv_obj_set_style_radius(obj_browser_view, 0, 0);
    lv_obj_set_style_pad_all(obj_browser_view, 0, 0);
    lv_obj_set_style_pad_row(obj_browser_view, 0, 0);
    lv_obj_set_scroll_dir(obj_browser_view, LV_DIR_VER | LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(obj_browser_view, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_flag(obj_browser_view, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj_browser_view, win_focus_event_cb, LV_EVENT_PRESSED, win_browser);

    html_view_show_message(obj_browser_view,
        "Bem-vindo ao LiwusWeb!",
        "Digite um endereco ou um termo para pesquisar no DuckDuckGo.\n"
        "Atalho Wikipedia: 'wiki: assunto'. Exemplo: https://pt.wikipedia.org/wiki/Brasil",
        lv_color_hex(0x0b57d0));

    lv_obj_add_flag(win_browser, LV_OBJ_FLAG_HIDDEN);
}

/* =========================================================================
 * Central de Pacotes / Gerenciador de Aplicativos (lpkg GUI)
 * ========================================================================= */
static void pkg_input_click_cb(lv_event_t *e) {
    win_focus_event_cb(e);
    set_pkg_input_focus(true);
}

static void pkgmgr_set_status(const char *msg, uint32_t color_hex) {
    if (!lbl_pkg_status || !msg) return;
    lv_label_set_text(lbl_pkg_status, msg);
    lv_obj_set_style_text_color(lbl_pkg_status, lv_color_hex(color_hex), 0);
}

static void pkgmgr_item_install_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const lpkg_pkg_info_t *it = lpkg_get_catalog_item(idx);
    if (!it) return;

    char msg[192];
    pkgmgr_set_status("Instalando pacote...", BZ_ACCENT);
    lv_refr_now(NULL);
    vga_flip();

    int rc = lpkg_install_pkg(it->name, NULL, msg, sizeof(msg));
    pkgmgr_refresh_list();
    pkgmgr_set_status(msg, (rc == 0) ? BZ_GREEN : BZ_RED);
}

static void pkgmgr_item_remove_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const lpkg_pkg_info_t *it = lpkg_get_catalog_item(idx);
    if (!it) return;

    char msg[160];
    int rc = lpkg_remove_pkg(it->name, msg, sizeof(msg));
    pkgmgr_refresh_list();
    pkgmgr_set_status(msg, (rc == 0) ? BZ_AMBER : BZ_RED);
}

static void pkgmgr_item_run_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    const lpkg_pkg_info_t *it = lpkg_get_catalog_item(idx);
    if (!it) return;

    set_pkg_input_focus(false);
    set_browser_url_focus(false);

    if (it->is_fullscreen_gui) {
        if (strcmp(it->name, "doom") == 0) {
            dock_doom_cb(NULL);
        } else {
            char *argv[2];
            char cmd_buf[32];
            strncpy(cmd_buf, it->name, sizeof(cmd_buf) - 1);
            cmd_buf[sizeof(cmd_buf) - 1] = '\0';
            argv[0] = cmd_buf;
            argv[1] = NULL;
            terminal_execute(1, argv);
        }
        return;
    }

    /* Para utilitarios de terminal (ex: neofetch, sysbench, c_demo), abre o Konsole e executa */
    dock_term_cb(NULL);
    term_exec_cmd(it->name);
}

static void pkgmgr_install_input_cb(lv_event_t *e) {
    (void)e;
    if (!ta_pkg_input) return;
    const char *txt = lv_textarea_get_text(ta_pkg_input);
    if (!txt) return;
    while (*txt == ' ') txt++;
    if (*txt == '\0') {
        pkgmgr_set_status("Digite o nome de um pacote ou uma URL http(s)://.../.lxe", BZ_AMBER);
        return;
    }

    char target[256];
    strncpy(target, txt, sizeof(target) - 1);
    target[sizeof(target) - 1] = '\0';

    pkgmgr_set_status("Baixando / instalando pacote...", BZ_ACCENT);
    lv_refr_now(NULL);
    vga_flip();

    char msg[192];
    int rc = lpkg_install_pkg(target, NULL, msg, sizeof(msg));
    if (rc == 0) {
        lv_textarea_set_text(ta_pkg_input, "");
    }
    pkgmgr_refresh_list();
    pkgmgr_set_status(msg, (rc == 0) ? BZ_GREEN : BZ_RED);
}

static void pkgmgr_sync_cb(lv_event_t *e) {
    (void)e;
    char msg[160];
    pkgmgr_set_status("Sincronizando catalogo de pacotes com GitHub HTTPS...", BZ_ACCENT);
    lv_refr_now(NULL);
    vga_flip();

    lpkg_update_repo(msg, sizeof(msg));
    lpkg_check_system_update(&g_sysupd_info, NULL, 0);
    pkgmgr_refresh_list();
    pkgmgr_refresh_sysupdate_ui();
    pkgmgr_set_status(msg, BZ_GREEN);
}

static void pkgmgr_set_tab(int tab) {
    g_pkg_active_tab = tab;
    if (pkg_tab_apps_cont && pkg_tab_sys_cont) {
        if (tab == 0) {
            lv_obj_clear_flag(pkg_tab_apps_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pkg_tab_sys_cont, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pkg_tab_apps_cont, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(pkg_tab_sys_cont, LV_OBJ_FLAG_HIDDEN);
            set_pkg_input_focus(false);
        }
    }
    if (btn_pkg_tab_apps && btn_pkg_tab_sys) {
        lv_obj_set_style_bg_color(btn_pkg_tab_apps, lv_color_hex(tab == 0 ? 0x215d80 : BZ_BUTTON), 0);
        lv_obj_set_style_border_color(btn_pkg_tab_apps, lv_color_hex(tab == 0 ? BZ_ACCENT : BZ_BUTTON_LINE), 0);
        lv_obj_set_style_bg_color(btn_pkg_tab_sys, lv_color_hex(tab == 1 ? 0x215d80 : BZ_BUTTON), 0);
        lv_obj_set_style_border_color(btn_pkg_tab_sys, lv_color_hex(tab == 1 ? BZ_ACCENT : BZ_BUTTON_LINE), 0);
    }
}

static void pkgmgr_tab_apps_cb(lv_event_t *e) {
    win_focus_event_cb(e);
    pkgmgr_set_tab(0);
}

static void pkgmgr_tab_sys_cb(lv_event_t *e) {
    win_focus_event_cb(e);
    pkgmgr_set_tab(1);
    if (!g_sysupd_info.checked_online && net_has_ip()) {
        char msg[160];
        pkgmgr_set_status("Consultando versao mais recente do LiwusOS no GitHub...", BZ_ACCENT);
        lv_refr_now(NULL);
        vga_flip();
        lpkg_check_system_update(&g_sysupd_info, msg, sizeof(msg));
        pkgmgr_refresh_sysupdate_ui();
        pkgmgr_set_status(msg, g_sysupd_info.update_available ? BZ_AMBER : BZ_GREEN);
    } else {
        pkgmgr_refresh_sysupdate_ui();
    }
}

static void pkgmgr_refresh_sysupdate_ui(void) {
    char cur_ver[16];
    lpkg_get_os_version(cur_ver, sizeof(cur_ver));
    strncpy(g_sysupd_info.current_version, cur_ver, sizeof(g_sysupd_info.current_version) - 1);
    g_sysupd_info.current_version[sizeof(g_sysupd_info.current_version) - 1] = '\0';

    if (lbl_about_title) {
        lv_label_set_text_fmt(lbl_about_title, "LiwusOS v%s", cur_ver);
    }

    if (!lbl_sysupd_cur || !lbl_sysupd_rem || !lbl_sysupd_state || !lbl_sysupd_notes) return;

    lv_label_set_text_fmt(lbl_sysupd_cur, "Versao Instalada no Sistema:  LiwusOS v%s", cur_ver);

    if (g_sysupd_info.checked_online && g_sysupd_info.remote_version[0]) {
        g_sysupd_info.update_available = (strcmp(cur_ver, g_sysupd_info.remote_version) != 0);
        lv_label_set_text_fmt(lbl_sysupd_rem, "Ultima Versao no GitHub (OTA):  LiwusOS v%s  (%s)",
                              g_sysupd_info.remote_version,
                              g_sysupd_info.release_date[0] ? g_sysupd_info.release_date : "pkg-repo");
        if (g_sysupd_info.update_available) {
            if (g_sysupd_info.boot_image_available && g_sysupd_info.system_archive_available) {
                lv_label_set_text_fmt(lbl_sysupd_state,
                                      LV_SYMBOL_WARNING " Atualizacao completa: kernel, sistema e aplicativos (v%s -> v%s)",
                                      cur_ver, g_sysupd_info.remote_version);
                lv_obj_set_style_text_color(lbl_sysupd_state, lv_color_hex(BZ_AMBER), 0);
            } else {
                lv_label_set_text(lbl_sysupd_state,
                                  LV_SYMBOL_WARNING " O feed nao tem os artefatos necessarios para atualizar todo o sistema.");
                lv_obj_set_style_text_color(lbl_sysupd_state, lv_color_hex(BZ_RED), 0);
            }
        } else {
            lv_label_set_text(lbl_sysupd_state,
                              LV_SYMBOL_OK " O sistema LiwusOS esta atualizado na versao mais recente do GitHub!");
            lv_obj_set_style_text_color(lbl_sysupd_state, lv_color_hex(BZ_GREEN), 0);
            if (bar_sysupd_prog) lv_bar_set_value(bar_sysupd_prog, 100, LV_ANIM_OFF);
        }
        lv_label_set_text(lbl_sysupd_notes,
                          g_sysupd_info.notes[0] ? g_sysupd_info.notes : "- Correcoes de estabilidade e novos pacotes.");
    } else {
        lv_label_set_text(lbl_sysupd_rem, "Ultima Versao no GitHub (OTA):  Clique em 'Verificar Atualizacoes'");
        lv_label_set_text(lbl_sysupd_state, "Canal OTA completo: system-v2.txt + imagem de boot + arquivo do sistema");
        lv_obj_set_style_text_color(lbl_sysupd_state, lv_color_hex(BZ_ACCENT), 0);
    }
}

static void pkgmgr_sysupdate_check_cb(lv_event_t *e) {
    (void)e;
    char msg[160];
    pkgmgr_set_status("Verificando atualizacoes do sistema no GitHub HTTPS...", BZ_ACCENT);
    if (bar_sysupd_prog) lv_bar_set_value(bar_sysupd_prog, 20, LV_ANIM_OFF);
    lv_refr_now(NULL);
    vga_flip();

    lpkg_update_repo(msg, sizeof(msg));
    int rc = lpkg_check_system_update(&g_sysupd_info, msg, sizeof(msg));
    if (bar_sysupd_prog) {
        lv_bar_set_value(bar_sysupd_prog, g_sysupd_info.update_available ? 0 : 100, LV_ANIM_OFF);
    }
    pkgmgr_refresh_list();
    pkgmgr_refresh_sysupdate_ui();
    pkgmgr_set_status(msg, (rc == 0) ? (g_sysupd_info.update_available ? BZ_AMBER : BZ_GREEN) : BZ_RED);
}

static void sysupdate_gui_progress_cb(const lpkg_progress_info_t *progress, void *user_data) {
    (void)user_data;
    if (!progress) return;
    bool phase_changed = strcmp(g_sysupd_last_phase,
                                progress->phase ? progress->phase : "") != 0;
    int phase_percent = progress->total
        ? (int)(((uint64_t)progress->completed * 100U) / progress->total)
        : progress->overall_percent;
    if (phase_percent > 100) phase_percent = 100;
    bool unknown_bytes_changed = !progress->total && progress->completed >= g_sysupd_last_unknown_bytes &&
        progress->completed - g_sysupd_last_unknown_bytes >= 16384U;
    if (!phase_changed && !unknown_bytes_changed &&
        g_sysupd_last_percent == progress->overall_percent &&
        g_sysupd_last_phase_percent == phase_percent) return;
    if (progress->phase) {
        strncpy(g_sysupd_last_phase, progress->phase, sizeof(g_sysupd_last_phase) - 1);
        g_sysupd_last_phase[sizeof(g_sysupd_last_phase) - 1] = '\0';
    }
    g_sysupd_last_percent = progress->overall_percent;
    g_sysupd_last_phase_percent = phase_percent;
    if (!progress->total) g_sysupd_last_unknown_bytes = progress->completed;
    if (progress && bar_sysupd_prog) {
        int pct = progress->overall_percent;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        lv_bar_set_value(bar_sysupd_prog, pct, LV_ANIM_OFF);
    }
    if (progress && progress->phase) {
        char status[160];
        strncpy(status, progress->phase, sizeof(status) - 1);
        status[sizeof(status) - 1] = '\0';
        if (progress->total) {
            char *tail = status + strlen(status);
            strcpy(tail, ": ");
            char num[16];
            itoa((int)(progress->completed / 1024U), num, 10); strcat(status, num);
            strcat(status, "/");
            itoa((int)(progress->total / 1024U), num, 10); strcat(status, num);
            strcat(status, " KiB");
        } else if (progress->completed) {
            char num[16];
            strcat(status, ": ");
            itoa((int)progress->completed, num, 10); strcat(status, num);
            strcat(status, " bytes recebidos");
        } else if (progress->message) {
            strcat(status, ": ");
            strncat(status, progress->message,
                    sizeof(status) - strlen(status) - 1);
        }
        pkgmgr_set_status(status, BZ_ACCENT);
    }
    lv_refr_now(NULL);
    vga_flip();
}

static void pkgmgr_sysupdate_apply_cb(lv_event_t *e) {
    (void)e;
    char msg[192];
    g_sysupd_last_percent = -1;
    g_sysupd_last_phase_percent = -1;
    g_sysupd_last_unknown_bytes = 0;
    g_sysupd_last_phase[0] = '\0';
    if (bar_sysupd_prog) lv_bar_set_value(bar_sysupd_prog, 5, LV_ANIM_OFF);
    pkgmgr_set_status("Iniciando atualizacao completa do sistema via GitHub HTTPS...", BZ_ACCENT);
    lv_refr_now(NULL);
    vga_flip();

    int rc = lpkg_perform_system_update(sysupdate_gui_progress_cb, NULL, msg, sizeof(msg));
    lpkg_check_system_update(&g_sysupd_info, NULL, 0);
    pkgmgr_refresh_list();
    pkgmgr_refresh_sysupdate_ui();
    pkgmgr_set_status(msg, (rc == 0) ? BZ_GREEN : BZ_RED);
}

static void pkgmgr_refresh_list(void) {
    if (!obj_pkg_list) return;
    lv_obj_clean(obj_pkg_list);

    int total = lpkg_get_catalog_count();
    int inst_count = 0;
    int upd_count = 0;

    for (int i = 0; i < total; i++) {
        const lpkg_pkg_info_t *it = lpkg_get_catalog_item(i);
        if (!it) continue;

        uint32_t sz = 0;
        bool inst = lpkg_is_installed(it->name, &sz);
        char inst_ver[16];
        inst_ver[0] = '\0';
        if (inst) {
            inst_count++;
            lpkg_get_installed_version(it->name, inst_ver, sizeof(inst_ver));
        }
        bool has_upd = inst && lpkg_has_update(it->name);
        if (has_upd) upd_count++;

        lv_obj_t *row = lv_obj_create(obj_pkg_list);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_size(row, lv_pct(100), 56);
        lv_obj_set_style_bg_color(row, lv_color_hex(BZ_WIN_BG), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(has_upd ? BZ_AMBER : BZ_PANEL_LINE), 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_radius(row, 3, 0);
        lv_obj_set_style_pad_hor(row, 12, 0);
        lv_obj_set_style_pad_ver(row, 6, 0);
        lv_obj_add_event_cb(row, win_focus_event_cb, LV_EVENT_PRESSED, win_pkgmgr);

        /* Titulo + versao + categoria */
        lv_obj_t *lbl_name = lv_label_create(row);
        lv_obj_set_style_text_font(lbl_name, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl_name, lv_color_hex(BZ_TEXT), 0);
        if (has_upd && inst_ver[0]) {
            lv_label_set_text_fmt(lbl_name, "%s  v%s -> v%s  [%s]", it->name, inst_ver, it->version, it->category);
        } else {
            lv_label_set_text_fmt(lbl_name, "%s  v%s  [%s]", it->name, it->version, it->category);
        }
        lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, 0, 2);

        /* Descricao */
        lv_obj_t *lbl_desc = lv_label_create(row);
        lv_obj_set_style_text_font(lbl_desc, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl_desc, lv_color_hex(BZ_TEXT_DIM), 0);
        lv_label_set_text(lbl_desc, it->desc);
        lv_obj_align(lbl_desc, LV_ALIGN_BOTTOM_LEFT, 0, -2);

        /* Badge de estado */
        lv_obj_t *badge = lv_obj_create(row);
        lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_size(badge, 76, 22);
        lv_obj_set_style_radius(badge, 3, 0);
        lv_obj_set_style_border_width(badge, 0, 0);
        lv_obj_set_style_pad_all(badge, 0, 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        uint32_t badge_bg = has_upd ? 0x5a3e10 : (inst ? 0x1e4620 : 0x31363b);
        uint32_t badge_fg = has_upd ? 0xf39c12 : (inst ? 0x58d68d : BZ_TEXT_DIM);
        lv_obj_set_style_bg_color(badge, lv_color_hex(badge_bg), 0);

        int badge_x = -92;
        if (inst) {
            if (has_upd) {
                badge_x = it->is_core ? -172 : -250;
            } else {
                badge_x = it->is_core ? -84 : -164;
            }
        }
        lv_obj_align(badge, LV_ALIGN_RIGHT_MID, badge_x, 0);

        lv_obj_t *b_lbl = lv_label_create(badge);
        lv_obj_set_style_text_font(b_lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(b_lbl, lv_color_hex(badge_fg), 0);
        lv_label_set_text(b_lbl, has_upd ? "Atualizar" : (inst ? "Instalado" : "Disponivel"));
        lv_obj_center(b_lbl);

        if (!inst) {
            lv_obj_t *btn_inst = lv_btn_create(row);
            lv_obj_set_size(btn_inst, 84, 30);
            bz_style_button(btn_inst);
            lv_obj_set_style_bg_color(btn_inst, lv_color_hex(0x215d80), 0);
            lv_obj_set_style_border_color(btn_inst, lv_color_hex(BZ_ACCENT), 0);
            lv_obj_align(btn_inst, LV_ALIGN_RIGHT_MID, 0, 0);
            lv_obj_t *l_in = lv_label_create(btn_inst);
            lv_obj_set_style_text_font(l_in, &lv_font_montserrat_12, 0);
            lv_label_set_text(l_in, LV_SYMBOL_DOWNLOAD " Instalar");
            lv_obj_center(l_in);
            lv_obj_add_event_cb(btn_inst, pkgmgr_item_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        } else {
            if (has_upd) {
                lv_obj_t *btn_upd = lv_btn_create(row);
                lv_obj_set_size(btn_upd, 82, 30);
                bz_style_button(btn_upd);
                lv_obj_set_style_bg_color(btn_upd, lv_color_hex(0x5a3e10), 0);
                lv_obj_set_style_border_color(btn_upd, lv_color_hex(BZ_AMBER), 0);
                lv_obj_align(btn_upd, LV_ALIGN_RIGHT_MID, it->is_core ? -82 : -162, 0);
                lv_obj_t *l_up = lv_label_create(btn_upd);
                lv_obj_set_style_text_font(l_up, &lv_font_montserrat_12, 0);
                lv_label_set_text(l_up, LV_SYMBOL_REFRESH " Atualizar");
                lv_obj_center(l_up);
                lv_obj_add_event_cb(btn_upd, pkgmgr_item_install_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            }

            lv_obj_t *btn_run = lv_btn_create(row);
            lv_obj_set_size(btn_run, 76, 30);
            bz_style_button(btn_run);
            lv_obj_set_style_border_color(btn_run, lv_color_hex(BZ_ACCENT), 0);
            lv_obj_align(btn_run, LV_ALIGN_RIGHT_MID, it->is_core ? 0 : -80, 0);
            lv_obj_t *l_run = lv_label_create(btn_run);
            lv_obj_set_style_text_font(l_run, &lv_font_montserrat_12, 0);
            lv_label_set_text(l_run, LV_SYMBOL_PLAY " Abrir");
            lv_obj_center(l_run);
            lv_obj_add_event_cb(btn_run, pkgmgr_item_run_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

            if (!it->is_core) {
                lv_obj_t *btn_rm = lv_btn_create(row);
                lv_obj_set_size(btn_rm, 74, 30);
                bz_style_button(btn_rm);
                lv_obj_set_style_border_color(btn_rm, lv_color_hex(BZ_RED), 0);
                lv_obj_align(btn_rm, LV_ALIGN_RIGHT_MID, 0, 0);
                lv_obj_t *l_rm = lv_label_create(btn_rm);
                lv_obj_set_style_text_font(l_rm, &lv_font_montserrat_12, 0);
                lv_label_set_text(l_rm, "Remover");
                lv_obj_center(l_rm);
                lv_obj_add_event_cb(btn_rm, pkgmgr_item_remove_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
            }
        }
    }

    if (lbl_pkg_status) {
        char st_buf[140];
        lv_snprintf(st_buf, sizeof(st_buf),
                    "Catalogo GitHub: %d pacotes (%d instalados, %d com atualizacao disponivel).",
                    total, inst_count, upd_count);
        lv_label_set_text(lbl_pkg_status, st_buf);
    }
}

static void create_pkgmgr_window(void) {
    if (win_pkgmgr) return;

    win_pkgmgr = lv_win_create(lv_scr_act(), WIN_HEADER_H);
    setup_window(win_pkgmgr, 720, 480, 152, 74);
    add_window_controls(win_pkgmgr, "Central de Pacotes - lpkg");

    lv_obj_t *content = lv_win_get_content(win_pkgmgr);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_style_pad_row(content, 8, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(content, win_focus_event_cb, LV_EVENT_PRESSED, win_pkgmgr);

    /* Barra de Abas: [Aplicativos (Pacotes)] | [Atualizacao de Sistema] */
    lv_obj_t *tab_bar = lv_obj_create(content);
    lv_obj_clear_flag(tab_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(tab_bar, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(tab_bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tab_bar, 0, 0);
    lv_obj_set_style_pad_all(tab_bar, 0, 0);
    lv_obj_set_style_pad_column(tab_bar, 8, 0);
    lv_obj_set_flex_flow(tab_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tab_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_pkg_tab_apps = lv_btn_create(tab_bar);
    lv_obj_set_size(btn_pkg_tab_apps, 220, 32);
    bz_style_button(btn_pkg_tab_apps);
    lv_obj_t *lbl_t0 = lv_label_create(btn_pkg_tab_apps);
    lv_label_set_text(lbl_t0, LV_SYMBOL_DOWNLOAD " Aplicativos (Pacotes)");
    lv_obj_center(lbl_t0);
    lv_obj_add_event_cb(btn_pkg_tab_apps, pkgmgr_tab_apps_cb, LV_EVENT_CLICKED, win_pkgmgr);

    btn_pkg_tab_sys = lv_btn_create(tab_bar);
    lv_obj_set_size(btn_pkg_tab_sys, 230, 32);
    bz_style_button(btn_pkg_tab_sys);
    lv_obj_t *lbl_t1 = lv_label_create(btn_pkg_tab_sys);
    lv_label_set_text(lbl_t1, LV_SYMBOL_REFRESH " Atualizacao de Sistema");
    lv_obj_center(lbl_t1);
    lv_obj_add_event_cb(btn_pkg_tab_sys, pkgmgr_tab_sys_cb, LV_EVENT_CLICKED, win_pkgmgr);

    /* ==================== ABA 0: APLICATIVOS ==================== */
    pkg_tab_apps_cont = lv_obj_create(content);
    lv_obj_clear_flag(pkg_tab_apps_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(pkg_tab_apps_cont, lv_pct(100));
    lv_obj_set_flex_grow(pkg_tab_apps_cont, 1);
    lv_obj_set_style_bg_opa(pkg_tab_apps_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pkg_tab_apps_cont, 0, 0);
    lv_obj_set_style_pad_all(pkg_tab_apps_cont, 0, 0);
    lv_obj_set_style_pad_row(pkg_tab_apps_cont, 8, 0);
    lv_obj_set_flex_flow(pkg_tab_apps_cont, LV_FLEX_FLOW_COLUMN);

    /* Barra superior: Campo de pacote/URL + Instalar + Sincronizar */
    lv_obj_t *bar = lv_obj_create(pkg_tab_apps_cont);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, lv_pct(100), 36);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    ta_pkg_input = lv_textarea_create(bar);
    lv_textarea_set_one_line(ta_pkg_input, true);
    lv_textarea_set_placeholder_text(ta_pkg_input, "Nome do pacote (ex: neofetch, snake) ou URL http(s)://.../.lxe");
    lv_textarea_set_text(ta_pkg_input, "");
    lv_obj_set_flex_grow(ta_pkg_input, 1);
    lv_obj_set_height(ta_pkg_input, 32);
    lv_obj_set_style_radius(ta_pkg_input, 3, 0);
    lv_obj_set_style_pad_hor(ta_pkg_input, 10, 0);
    lv_obj_set_style_pad_ver(ta_pkg_input, 6, 0);
    lv_obj_set_style_bg_color(ta_pkg_input, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_text_color(ta_pkg_input, lv_color_hex(BZ_TEXT), 0);
    lv_obj_set_style_border_width(ta_pkg_input, 1, 0);
    lv_obj_set_style_border_color(ta_pkg_input, lv_color_hex(BZ_BUTTON_LINE), 0);
    lv_obj_set_style_border_color(ta_pkg_input, lv_color_hex(BZ_ACCENT), LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(ta_pkg_input, lv_color_hex(BZ_ACCENT), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(ta_pkg_input, LV_OPA_COVER, LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta_pkg_input, pkg_input_click_cb, LV_EVENT_PRESSED, win_pkgmgr);
    lv_obj_add_event_cb(ta_pkg_input, pkg_input_click_cb, LV_EVENT_CLICKED, win_pkgmgr);

    lv_obj_t *btn_inst = lv_btn_create(bar);
    lv_obj_set_size(btn_inst, 100, 32);
    bz_style_button(btn_inst);
    lv_obj_set_style_bg_color(btn_inst, lv_color_hex(0x215d80), 0);
    lv_obj_set_style_border_color(btn_inst, lv_color_hex(BZ_ACCENT), 0);
    lv_obj_t *lbl_inst = lv_label_create(btn_inst);
    lv_label_set_text(lbl_inst, LV_SYMBOL_DOWNLOAD " Instalar");
    lv_obj_center(lbl_inst);
    lv_obj_add_event_cb(btn_inst, win_focus_event_cb, LV_EVENT_PRESSED, win_pkgmgr);
    lv_obj_add_event_cb(btn_inst, pkgmgr_install_input_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_sync = lv_btn_create(bar);
    lv_obj_set_size(btn_sync, 110, 32);
    bz_style_button(btn_sync);
    lv_obj_t *lbl_sync = lv_label_create(btn_sync);
    lv_label_set_text(lbl_sync, LV_SYMBOL_REFRESH " Sincronizar");
    lv_obj_center(lbl_sync);
    lv_obj_add_event_cb(btn_sync, win_focus_event_cb, LV_EVENT_PRESSED, win_pkgmgr);
    lv_obj_add_event_cb(btn_sync, pkgmgr_sync_cb, LV_EVENT_CLICKED, NULL);

    /* Lista rolavel de pacotes */
    obj_pkg_list = lv_obj_create(pkg_tab_apps_cont);
    lv_obj_set_width(obj_pkg_list, lv_pct(100));
    lv_obj_set_flex_grow(obj_pkg_list, 1);
    lv_obj_set_flex_flow(obj_pkg_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(obj_pkg_list, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_bg_opa(obj_pkg_list, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(obj_pkg_list, lv_color_hex(BZ_TITLE_LINE), 0);
    lv_obj_set_style_border_width(obj_pkg_list, 1, 0);
    lv_obj_set_style_radius(obj_pkg_list, 2, 0);
    lv_obj_set_style_pad_all(obj_pkg_list, 8, 0);
    lv_obj_set_style_pad_row(obj_pkg_list, 6, 0);
    lv_obj_set_scroll_dir(obj_pkg_list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(obj_pkg_list, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_add_event_cb(obj_pkg_list, win_focus_event_cb, LV_EVENT_PRESSED, win_pkgmgr);

    /* ==================== ABA 1: ATUALIZACAO DE SISTEMA ==================== */
    pkg_tab_sys_cont = lv_obj_create(content);
    lv_obj_clear_flag(pkg_tab_sys_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(pkg_tab_sys_cont, lv_pct(100));
    lv_obj_set_flex_grow(pkg_tab_sys_cont, 1);
    lv_obj_set_style_bg_opa(pkg_tab_sys_cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(pkg_tab_sys_cont, 0, 0);
    lv_obj_set_style_pad_all(pkg_tab_sys_cont, 0, 0);
    lv_obj_set_style_pad_row(pkg_tab_sys_cont, 8, 0);
    lv_obj_set_flex_flow(pkg_tab_sys_cont, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *sys_card = lv_obj_create(pkg_tab_sys_cont);
    lv_obj_clear_flag(sys_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sys_card, lv_pct(100), 124);
    lv_obj_set_style_bg_color(sys_card, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_bg_opa(sys_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(sys_card, lv_color_hex(BZ_PANEL_LINE), 0);
    lv_obj_set_style_border_width(sys_card, 1, 0);
    lv_obj_set_style_radius(sys_card, 3, 0);
    lv_obj_set_style_pad_all(sys_card, 12, 0);
    lv_obj_set_style_pad_row(sys_card, 6, 0);
    lv_obj_set_flex_flow(sys_card, LV_FLEX_FLOW_COLUMN);

    lbl_sysupd_cur = lv_label_create(sys_card);
    lv_obj_set_style_text_font(lbl_sysupd_cur, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sysupd_cur, lv_color_hex(BZ_TEXT), 0);

    lbl_sysupd_rem = lv_label_create(sys_card);
    lv_obj_set_style_text_font(lbl_sysupd_rem, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_sysupd_rem, lv_color_hex(BZ_ACCENT), 0);

    lbl_sysupd_state = lv_label_create(sys_card);
    lv_obj_set_style_text_font(lbl_sysupd_state, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_sysupd_state, lv_color_hex(BZ_TEXT_DIM), 0);

    bar_sysupd_prog = lv_bar_create(sys_card);
    lv_obj_set_size(bar_sysupd_prog, lv_pct(100), 14);
    lv_bar_set_range(bar_sysupd_prog, 0, 100);
    lv_bar_set_value(bar_sysupd_prog, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_sysupd_prog, lv_color_hex(0x31363b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_sysupd_prog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_sysupd_prog, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_sysupd_prog, lv_color_hex(BZ_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_sysupd_prog, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_sysupd_prog, 3, LV_PART_INDICATOR);

    lv_obj_t *notes_card = lv_obj_create(pkg_tab_sys_cont);
    lv_obj_set_width(notes_card, lv_pct(100));
    lv_obj_set_flex_grow(notes_card, 1);
    lv_obj_set_style_bg_color(notes_card, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_bg_opa(notes_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(notes_card, lv_color_hex(BZ_PANEL_LINE), 0);
    lv_obj_set_style_border_width(notes_card, 1, 0);
    lv_obj_set_style_radius(notes_card, 3, 0);
    lv_obj_set_style_pad_all(notes_card, 12, 0);
    lv_obj_set_style_pad_row(notes_card, 6, 0);
    lv_obj_set_flex_flow(notes_card, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *lbl_nhdr = lv_label_create(notes_card);
    lv_obj_set_style_text_font(lbl_nhdr, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_nhdr, lv_color_hex(BZ_TEXT), 0);
    lv_label_set_text(lbl_nhdr, "Notas de Lancamento / Changelog (GitHub OTA):");

    lbl_sysupd_notes = lv_label_create(notes_card);
    lv_obj_set_width(lbl_sysupd_notes, lv_pct(100));
    lv_obj_set_style_text_font(lbl_sysupd_notes, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_sysupd_notes, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text(lbl_sysupd_notes,
        "- Clique em 'Verificar Atualizacoes' para consultar o manifesto system.txt no GitHub.\n"
        "- A atualizacao baixa as versoes mais recentes dos binarios do sistema (/bin/*.lxe)\n"
        "  e atualiza a versao instalada em /etc/os-release no SDFS.");

    lv_obj_t *act_row = lv_obj_create(pkg_tab_sys_cont);
    lv_obj_clear_flag(act_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(act_row, lv_pct(100), 38);
    lv_obj_set_style_bg_opa(act_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(act_row, 0, 0);
    lv_obj_set_style_pad_all(act_row, 0, 0);
    lv_obj_set_style_pad_column(act_row, 10, 0);
    lv_obj_set_flex_flow(act_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(act_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_chk = lv_btn_create(act_row);
    lv_obj_set_size(btn_chk, 210, 34);
    bz_style_button(btn_chk);
    lv_obj_t *lbl_chk = lv_label_create(btn_chk);
    lv_label_set_text(lbl_chk, LV_SYMBOL_REFRESH " Verificar Atualizacoes");
    lv_obj_center(lbl_chk);
    lv_obj_add_event_cb(btn_chk, win_focus_event_cb, LV_EVENT_PRESSED, win_pkgmgr);
    lv_obj_add_event_cb(btn_chk, pkgmgr_sysupdate_check_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *btn_apply = lv_btn_create(act_row);
    lv_obj_set_size(btn_apply, 230, 34);
    bz_style_button(btn_apply);
    lv_obj_set_style_bg_color(btn_apply, lv_color_hex(0x215d80), 0);
    lv_obj_set_style_border_color(btn_apply, lv_color_hex(BZ_ACCENT), 0);
    lv_obj_t *lbl_app = lv_label_create(btn_apply);
    lv_label_set_text(lbl_app, LV_SYMBOL_DOWNLOAD " Atualizar Sistema Agora");
    lv_obj_center(lbl_app);
    lv_obj_add_event_cb(btn_apply, win_focus_event_cb, LV_EVENT_PRESSED, win_pkgmgr);
    lv_obj_add_event_cb(btn_apply, pkgmgr_sysupdate_apply_cb, LV_EVENT_CLICKED, NULL);

    /* Barra de status inferior */
    lbl_pkg_status = lv_label_create(content);
    lv_obj_set_width(lbl_pkg_status, lv_pct(100));
    lv_obj_set_style_text_font(lbl_pkg_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_pkg_status, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text(lbl_pkg_status, "Pronto.");

    pkgmgr_set_tab(0);
    pkgmgr_refresh_list();
    pkgmgr_refresh_sysupdate_ui();
    lv_obj_add_flag(win_pkgmgr, LV_OBJ_FLAG_HIDDEN);
}

/* =========================================================================
 * Criação da Janela Instalar Sistema (SOMENTE Modo Live USB / ISO)
 * ========================================================================= */
static void installer_refresh_toggles(void) {
    if (btn_inst_kb_br && btn_inst_kb_us) {
        lv_obj_set_style_bg_color(btn_inst_kb_br, lv_color_hex(g_inst_kb_abnt2 ? 0x215d80 : BZ_BUTTON), 0);
        lv_obj_set_style_border_color(btn_inst_kb_br, lv_color_hex(g_inst_kb_abnt2 ? BZ_ACCENT : BZ_BUTTON_LINE), 0);
        lv_obj_set_style_bg_color(btn_inst_kb_us, lv_color_hex(!g_inst_kb_abnt2 ? 0x215d80 : BZ_BUTTON), 0);
        lv_obj_set_style_border_color(btn_inst_kb_us, lv_color_hex(!g_inst_kb_abnt2 ? BZ_ACCENT : BZ_BUTTON_LINE), 0);
    }
    if (btn_inst_prof_full && btn_inst_prof_min) {
        lv_obj_set_style_bg_color(btn_inst_prof_full, lv_color_hex(g_inst_profile_full ? 0x215d80 : BZ_BUTTON), 0);
        lv_obj_set_style_border_color(btn_inst_prof_full, lv_color_hex(g_inst_profile_full ? BZ_ACCENT : BZ_BUTTON_LINE), 0);
        lv_obj_set_style_bg_color(btn_inst_prof_min, lv_color_hex(!g_inst_profile_full ? 0x215d80 : BZ_BUTTON), 0);
        lv_obj_set_style_border_color(btn_inst_prof_min, lv_color_hex(!g_inst_profile_full ? BZ_ACCENT : BZ_BUTTON_LINE), 0);
    }
}

static void installer_detect_disk(void) {
    g_inst_disk_found = 0;
    g_inst_disk_is_ahci = 0;
    g_inst_ahci_port = 0xFF;
    g_inst_ata_bus = ATA_PRIMARY;
    g_inst_ata_drive = ATA_MASTER;
    g_inst_disk_sectors = 0;

    uint8_t port = 0xFF;
    if (ahci_find_first(&port) == 0) {
        uint64_t secs = ahci_identify(port);
        if (secs == 0) secs = 262144;
        g_inst_disk_found = 1;
        g_inst_disk_is_ahci = 1;
        g_inst_ahci_port = port;
        g_inst_disk_sectors = (uint32_t)(secs > 0xFFFFFFFFULL ? 0xFFFFFFFFULL : secs);
    } else if (ata_find_first(&g_inst_ata_bus, &g_inst_ata_drive) == 0) {
        uint32_t secs = ata_identify_sectors(g_inst_ata_bus, g_inst_ata_drive);
        if (secs == 0) secs = 262144;
        g_inst_disk_found = 1;
        g_inst_disk_is_ahci = 0;
        g_inst_disk_sectors = secs;
    }

    if (lbl_inst_disk) {
        if (g_inst_disk_found) {
            uint32_t mb = g_inst_disk_sectors / 2048U;
            if (mb == 0) mb = 64;
            if (g_inst_disk_is_ahci) {
                lv_label_set_text_fmt(lbl_inst_disk,
                    LV_SYMBOL_DRIVE " Disco Alvo Detectado: SATA/AHCI (Porta %u)  -  %u MB (%u setores)",
                    (unsigned)g_inst_ahci_port, mb, g_inst_disk_sectors);
            } else {
                const char *bname = (g_inst_ata_bus == ATA_PRIMARY) ? "Primario" : "Secundario";
                const char *dname = (g_inst_ata_drive == ATA_MASTER) ? "Master" : "Slave";
                lv_label_set_text_fmt(lbl_inst_disk,
                    LV_SYMBOL_DRIVE " Disco Alvo Detectado: IDE/ATA %s %s  -  %u MB (%u setores)",
                    bname, dname, mb, g_inst_disk_sectors);
            }
            lv_obj_set_style_text_color(lbl_inst_disk, lv_color_hex(0x58d68d), 0);
        } else {
            lv_label_set_text(lbl_inst_disk,
                LV_SYMBOL_WARNING " Nenhum disco rigido (SATA/AHCI ou IDE/ATA) detectado!");
            lv_obj_set_style_text_color(lbl_inst_disk, lv_color_hex(BZ_RED), 0);
        }
    }
}

static void installer_redetect_cb(lv_event_t *e) {
    (void)e;
    installer_detect_disk();
}

static void installer_host_click_cb(lv_event_t *e) {
    win_focus_event_cb(e);
    set_inst_host_focus(true);
}

static void installer_kb_br_cb(lv_event_t *e) {
    (void)e;
    g_inst_kb_abnt2 = 1;
    keyboard_set_layout(KB_LAYOUT_ABNT2);
    installer_refresh_toggles();
}

static void installer_kb_us_cb(lv_event_t *e) {
    (void)e;
    g_inst_kb_abnt2 = 0;
    keyboard_set_layout(KB_LAYOUT_US);
    installer_refresh_toggles();
}

static void installer_prof_full_cb(lv_event_t *e) {
    (void)e;
    g_inst_profile_full = 1;
    installer_refresh_toggles();
}

static void installer_prof_min_cb(lv_event_t *e) {
    (void)e;
    g_inst_profile_full = 0;
    installer_refresh_toggles();
}

static void installer_ui_step(int pct, const char *step_txt, const char *log_txt, uint32_t col) {
    if (bar_inst_prog) lv_bar_set_value(bar_inst_prog, pct, LV_ANIM_OFF);
    if (lbl_inst_step && step_txt) {
        lv_label_set_text(lbl_inst_step, step_txt);
        lv_obj_set_style_text_color(lbl_inst_step, lv_color_hex(col), 0);
    }
    if (lbl_inst_log && log_txt) {
        lv_label_set_text(lbl_inst_log, log_txt);
    }
    lv_refr_now(NULL);
    vga_flip();
}

static void installer_copy_progress_cb(int percent, const char *filename) {
    static int last_pct = -1;
    if (percent == last_pct && filename != NULL) return;
    last_pct = percent;
    int overall = 40 + (percent * 25) / 100;
    char log_buf[160];
    if (filename) {
        lv_snprintf(log_buf, sizeof(log_buf), "Copiando arquivo do sistema para o HD: %s (%d%%)", filename, percent);
    } else {
        strcpy(log_buf, "Arquivos base do sistema copiados para a particao SDFS do HD.");
    }
    installer_ui_step(overall, "Etapa 3/5: Instalando arquivos base do sistema no HD...", log_buf, BZ_ACCENT);
}

static void installer_reboot_cb(lv_event_t *e) {
    (void)e;
    acpi_reboot();
}

static void installer_start_cb(lv_event_t *e) {
    (void)e;
    if (g_inst_completed) return;

    if (!g_inst_disk_found) {
        installer_detect_disk();
    }
    if (!g_inst_disk_found) {
        installer_ui_step(0, "Erro: Nenhum disco rigido (HD) detectado para instalacao!",
                          "Verifique se ha um disco SATA/AHCI ou IDE/ATA conectado.", BZ_RED);
        return;
    }

    if (g_inst_disk_sectors <= SDFS_HD_PARTITION_LBA + 4096) {
        installer_ui_step(0, "Erro: O disco rigido precisa ter pelo menos 48 MB!",
                          "Aumente a capacidade do disco alvo.", BZ_RED);
        return;
    }

    /* Coleta respostas das perguntas */
    char chosen_host[32] = "liwus-hd";
    if (ta_inst_hostname) {
        const char *txt = lv_textarea_get_text(ta_inst_hostname);
        if (txt) {
            while (*txt == ' ') txt++;
            size_t j = 0;
            while (txt[j] && txt[j] != ' ' && j < sizeof(chosen_host) - 1) {
                chosen_host[j] = txt[j];
                j++;
            }
            if (j > 0) chosen_host[j] = '\0';
        }
    }

    /* Etapa 1/5: Gravar Bootloader GRUB + Kernel (boot/hd_boot.img) no LBA 0 do HD */
    uint32_t boot_img_size = 0;
    const uint8_t *boot_img = (const uint8_t *)initrd_get_file("boot/hd_boot.img", &boot_img_size);
    if (!boot_img || boot_img_size < 65536) {
        installer_ui_step(0, "Erro: Imagem de boot (boot/hd_boot.img) ausente no Live USB!",
                          "Recompile a ISO com 'make all'.", BZ_RED);
        return;
    }

    uint32_t boot_sectors = (boot_img_size + 511U) / 512U;
    if (boot_sectors > SDFS_HD_PARTITION_LBA) {
        boot_sectors = SDFS_HD_PARTITION_LBA;
    }

    installer_ui_step(5, "Etapa 1/5: Gravando Bootloader GRUB + Kernel x86_64 no MBR do HD...",
                      "Escrevendo setores de boot em LBA 0...", BZ_ACCENT);

    uint32_t written = 0;
    while (written < boot_sectors) {
        uint32_t chunk = boot_sectors - written;
        if (g_inst_disk_is_ahci) {
            if (chunk > 8) chunk = 8;
            if (!ahci_write_sector(g_inst_ahci_port, written, chunk,
                                   (uint8_t *)(boot_img + (uint64_t)written * 512ULL))) {
                char err_buf[128];
                lv_snprintf(err_buf, sizeof(err_buf),
                            "Falha na escrita SATA/AHCI (Porta %u, LBA %u).",
                            (unsigned)g_inst_ahci_port, written);
                installer_ui_step(0, "Erro de I/O ao gravar bootloader AHCI!",
                                  err_buf, BZ_RED);
                return;
            }
        } else {
            if (chunk > 16) chunk = 16;
            if (ata_write_sectors_raw(g_inst_ata_bus, g_inst_ata_drive, written, chunk,
                                      boot_img + (uint64_t)written * 512ULL) != 0) {
                char err_buf[128];
                lv_snprintf(err_buf, sizeof(err_buf),
                            "Falha na escrita IDE/ATA (Bus 0x%x, LBA %u).",
                            (unsigned)g_inst_ata_bus, written);
                installer_ui_step(0, "Erro de I/O ao gravar bootloader ATA!",
                                  err_buf, BZ_RED);
                return;
            }
        }
        written += chunk;
        if ((written % 64) == 0 || written == boot_sectors) {
            int pct = 5 + (int)((written * 20U) / boot_sectors);
            char bmsg[128];
            lv_snprintf(bmsg, sizeof(bmsg),
                        "Bootloader + Kernel gravados: %u / %u KB (LBA 0..%u)",
                        (written * 512U) / 1024U, (boot_sectors * 512U) / 1024U, written);
            installer_ui_step(pct, "Etapa 1/5: Gravando Bootloader GRUB + Kernel x86_64 no MBR do HD...",
                              bmsg, BZ_ACCENT);
        }
    }

    /* Mount an existing SDFS in place when present; only a blank, non-corrupt
     * target is formatted. This makes the ISO a data-preserving upgrade path. */
    uint32_t part_sectors = g_inst_disk_sectors - SDFS_HD_PARTITION_LBA;
    ata_set_writeback(1);
    sdfs_switch_to_hd_partition(g_inst_ata_bus, g_inst_ata_drive,
                                g_inst_disk_is_ahci ? g_inst_ahci_port : 0xFF,
                                SDFS_HD_PARTITION_LBA, part_sectors);
    fs_node_t *hd_root = sdfs_mount(g_inst_ata_bus, g_inst_ata_drive, SDFS_HD_PARTITION_LBA);
    if (!hd_root) {
        if (sdfs_mount_failed_corrupt()) {
            ata_set_writeback(0);
            installer_ui_step(0, "Erro: particao SDFS existente parece corrompida.",
                              "Instalacao interrompida para proteger os arquivos existentes.", BZ_RED);
            return;
        }
        installer_ui_step(28, "Etapa 2/5: Formatando nova particao SDFS...",
                          "Nenhum sistema SDFS existente; criando a particao inicial.", BZ_ACCENT);
        if (sdfs_format() != 0) {
            ata_set_writeback(0);
            installer_ui_step(0, "Erro ao formatar a particao SDFS v2 no HD!",
                              "Falha em sdfs_format().", BZ_RED);
            return;
        }
        hd_root = sdfs_mount(g_inst_ata_bus, g_inst_ata_drive, SDFS_HD_PARTITION_LBA);
    }
    if (!hd_root) {
        ata_set_writeback(0);
        char mnt_err[160];
        lv_snprintf(mnt_err, sizeof(mnt_err), "Falha em sdfs_mount(): %s", sdfs_last_mount_error());
        installer_ui_step(0, "Erro ao montar a particao SDFS do HD!",
                          mnt_err, BZ_RED);
        return;
    }
    vfs_mount("disk:", hd_root);
    vfs_mount("/disk", hd_root);
    vfs_mount("/house/localhost", hd_root);
    vfs_mount("/", hd_root);

    uint32_t installed_marker_size = 0;
    char *installed_marker = (char *)sdfs_read_file("/.system_installed", &installed_marker_size);
    int upgrading_existing = installed_marker && installed_marker_size > 0;
    if (installed_marker) kfree(installed_marker);
    if (upgrading_existing) {
        installer_ui_step(32, "Instalacao existente detectada; preservando dados...",
                          "/etc, /home e /var serao mantidos durante a atualizacao.", BZ_GREEN);
        uint32_t hostname_size = 0;
        char *saved_hostname = (char *)sdfs_read_file("/etc/hostname", &hostname_size);
        if (saved_hostname && hostname_size) {
            size_t n = hostname_size < sizeof(g_system_hostname) - 1
                ? hostname_size : sizeof(g_system_hostname) - 1;
            while (n && (saved_hostname[n - 1] == '\n' || saved_hostname[n - 1] == '\r')) n--;
            memcpy(g_system_hostname, saved_hostname, n);
            g_system_hostname[n] = '\0';
        }
        if (saved_hostname) kfree(saved_hostname);
    }

    /* Etapa 3/5: Copiar arquivos base do sistema (initrd -> HD) */
    installer_ui_step(40, upgrading_existing
                          ? "Etapa 3/5: Atualizando arquivos base do sistema no HD..."
                          : "Etapa 3/5: Instalando arquivos base do sistema no HD...",
                      upgrading_existing
                          ? "Atualizando /bin, /sys, bibliotecas C e assets; dados pessoais preservados."
                          : "Copiando /bin, /sys, bibliotecas C e assets para o HD...", BZ_ACCENT);
    initrd_copy_to_sdfs(installer_copy_progress_cb);

    /* Etapa 4/5: Baixar a versao mais recente e pacotes oficiais direto do GitHub HTTPS */
    installer_ui_step(68, "Etapa 4/5: Conectando ao GitHub HTTPS para baixar pacotes atualizados...",
                      "Sincronizando https://raw.githubusercontent.com/rancidavi-dotcom/LiwusOS/pkg-repo ...", BZ_ACCENT);

    char target_os_ver[16];
    lpkg_get_os_version(target_os_ver, sizeof(target_os_ver));
    if (net_has_ip()) {
        char sync_msg[160];
        lpkg_update_repo(sync_msg, sizeof(sync_msg));
        if (lpkg_check_system_update(&g_sysupd_info, NULL, 0) == 0 && g_sysupd_info.remote_version[0]) {
            strncpy(target_os_ver, g_sysupd_info.remote_version, sizeof(target_os_ver) - 1);
            target_os_ver[sizeof(target_os_ver) - 1] = '\0';
        }

        const char *full_pkgs[] = { "neofetch", "kilo", "c_demo", "sysbench", "snake", "matrix" };
        const char *min_pkgs[]  = { "neofetch", "kilo", "c_demo", "sysbench" };
        const char **pkgs = g_inst_profile_full ? full_pkgs : min_pkgs;
        int npkgs = g_inst_profile_full ? 6 : 4;

        for (int i = 0; i < npkgs; i++) {
            int pct = 70 + ((i + 1) * 20) / npkgs;
            char pmsg[160];
            lv_snprintf(pmsg, sizeof(pmsg),
                        "Baixando pacote '%s.lxe' atualizado do GitHub HTTPS (%d/%d)...",
                        pkgs[i], i + 1, npkgs);
            installer_ui_step(pct, "Etapa 4/5: Baixando pacotes mais recentes do GitHub...",
                              pmsg, BZ_ACCENT);
            char out_msg[160];
            lpkg_install_pkg(pkgs[i], NULL, out_msg, sizeof(out_msg));
        }
    }

    /* Etapa 5/5: Gravar configuracoes personalizadas (/etc/hostname, /etc/keyboard.conf, /etc/os-release) */
    installer_ui_step(94, "Etapa 5/5: Salvando configuracoes do sistema no HD...",
                      "Gravando /etc/hostname, /etc/keyboard.conf e /etc/os-release...", BZ_ACCENT);

    sdfs_create_dir("/etc");

    if (!upgrading_existing) {
        char host_file[40];
        lv_snprintf(host_file, sizeof(host_file), "%s\n", chosen_host);
        sdfs_create_file("/etc/hostname");
        sdfs_write_file("/etc/hostname", (uint8_t *)host_file, (uint32_t)strlen(host_file));
        strncpy(g_system_hostname, chosen_host, sizeof(g_system_hostname) - 1);
        g_system_hostname[sizeof(g_system_hostname) - 1] = '\0';

        const char *kb_str = g_inst_kb_abnt2 ? "LAYOUT=ABNT2\n" : "LAYOUT=US\n";
        sdfs_create_file("/etc/keyboard.conf");
        sdfs_write_file("/etc/keyboard.conf", (uint8_t *)kb_str, (uint32_t)strlen(kb_str));
        keyboard_set_layout(g_inst_kb_abnt2 ? KB_LAYOUT_ABNT2 : KB_LAYOUT_US);
    }

    char rel_buf[192];
    lv_snprintf(rel_buf, sizeof(rel_buf),
                "NAME=LiwusOS\nVERSION=%s\nARCH=x86_64\nHOSTNAME=%s\nINSTALLED=HD\n",
                target_os_ver, upgrading_existing ? g_system_hostname : chosen_host);
    sdfs_create_file("/etc/os-release");
    sdfs_write_file("/etc/os-release", (uint8_t *)rel_buf, (uint32_t)strlen(rel_buf));

    sdfs_create_file("/.system_installed");
    sdfs_write_file("/.system_installed", (uint8_t *)"1", 1);

    ata_set_writeback(0);
    sdfs_flush_bitmap();
    if (g_inst_disk_is_ahci) {
        ahci_flush_cache(g_inst_ahci_port);
    } else {
        ata_flush_cache();
    }

    g_inst_completed = true;
    if (btn_inst_reboot) {
        lv_obj_clear_state(btn_inst_reboot, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(btn_inst_reboot, lv_color_hex(0x1e5631), 0);
        lv_obj_set_style_border_color(btn_inst_reboot, lv_color_hex(BZ_GREEN), 0);
    }

    char done_msg[192];
    lv_snprintf(done_msg, sizeof(done_msg),
                "LiwusOS v%s instalado no HD ('%s', teclado %s)! Remova o pendrive Live e inicie pelo HD.",
                target_os_ver, chosen_host, g_inst_kb_abnt2 ? "ABNT2" : "US");
    installer_ui_step(100, LV_SYMBOL_OK " Instalacao Concluida com Sucesso no Disco Rigido (HD)!",
                      done_msg, 0x58d68d);

    pkgmgr_refresh_list();
    pkgmgr_refresh_sysupdate_ui();
    terminal_update_view();
}

static void create_installer_window(void) {
    if (!is_live_mode || win_installer) return;

    win_installer = lv_win_create(lv_scr_act(), WIN_HEADER_H);
    setup_window(win_installer, 700, 486, 162, 68);
    add_window_controls(win_installer, "Instalar LiwusOS no Disco Rigido (HD)");

    lv_obj_t *content = lv_win_get_content(win_installer);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(content, 10, 0);
    lv_obj_set_style_pad_row(content, 7, 0);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_add_event_cb(content, win_focus_event_cb, LV_EVENT_PRESSED, win_installer);

    /* 1. Card de Deteccao do Disco Rigido */
    lv_obj_t *disk_card = lv_obj_create(content);
    lv_obj_clear_flag(disk_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(disk_card, lv_pct(100), 62);
    lv_obj_set_style_bg_color(disk_card, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_bg_opa(disk_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(disk_card, lv_color_hex(BZ_PANEL_LINE), 0);
    lv_obj_set_style_border_width(disk_card, 1, 0);
    lv_obj_set_style_radius(disk_card, 3, 0);
    lv_obj_set_style_pad_all(disk_card, 10, 0);

    lbl_inst_disk = lv_label_create(disk_card);
    lv_obj_set_style_text_font(lbl_inst_disk, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl_inst_disk, LV_ALIGN_TOP_LEFT, 0, 0);

    lv_obj_t *lbl_disk_sub = lv_label_create(disk_card);
    lv_obj_set_style_text_font(lbl_disk_sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_disk_sub, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text(lbl_disk_sub, "Particionamento automatico: GRUB2/Kernel (LBA 0) + SDFS v2 Journaled (LBA 65536)");
    lv_obj_align(lbl_disk_sub, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_t *btn_redet = lv_btn_create(disk_card);
    lv_obj_set_size(btn_redet, 118, 28);
    bz_style_button(btn_redet);
    lv_obj_align(btn_redet, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_t *l_rd = lv_label_create(btn_redet);
    lv_obj_set_style_text_font(l_rd, &lv_font_montserrat_12, 0);
    lv_label_set_text(l_rd, LV_SYMBOL_REFRESH " Redetectar");
    lv_obj_center(l_rd);
    lv_obj_add_event_cb(btn_redet, installer_redetect_cb, LV_EVENT_CLICKED, NULL);

    /* 2. Card de Perguntas de Configuracao Interativa */
    lv_obj_t *cfg_card = lv_obj_create(content);
    lv_obj_clear_flag(cfg_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(cfg_card, lv_pct(100), 198);
    lv_obj_set_style_bg_color(cfg_card, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_bg_opa(cfg_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(cfg_card, lv_color_hex(BZ_PANEL_LINE), 0);
    lv_obj_set_style_border_width(cfg_card, 1, 0);
    lv_obj_set_style_radius(cfg_card, 3, 0);
    lv_obj_set_style_pad_all(cfg_card, 10, 0);
    lv_obj_set_style_pad_row(cfg_card, 8, 0);
    lv_obj_set_flex_flow(cfg_card, LV_FLEX_FLOW_COLUMN);

    /* Pergunta 1: Nome do Computador (Hostname) */
    lv_obj_t *row_h = lv_obj_create(cfg_card);
    lv_obj_clear_flag(row_h, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row_h, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(row_h, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_h, 0, 0);
    lv_obj_set_style_pad_all(row_h, 0, 0);
    lv_obj_set_style_pad_column(row_h, 10, 0);
    lv_obj_set_flex_flow(row_h, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_h, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_q1 = lv_label_create(row_h);
    lv_obj_set_width(lbl_q1, 225);
    lv_obj_set_style_text_font(lbl_q1, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_q1, lv_color_hex(BZ_TEXT), 0);
    lv_label_set_text(lbl_q1, "1. Nome do Computador (Host):");

    ta_inst_hostname = lv_textarea_create(row_h);
    lv_textarea_set_one_line(ta_inst_hostname, true);
    lv_textarea_set_text(ta_inst_hostname, "liwus-hd");
    lv_obj_set_flex_grow(ta_inst_hostname, 1);
    lv_obj_set_height(ta_inst_hostname, 30);
    lv_obj_set_style_radius(ta_inst_hostname, 3, 0);
    lv_obj_set_style_pad_hor(ta_inst_hostname, 8, 0);
    lv_obj_set_style_pad_ver(ta_inst_hostname, 5, 0);
    lv_obj_set_style_bg_color(ta_inst_hostname, lv_color_hex(BZ_WIN_BG), 0);
    lv_obj_set_style_text_color(ta_inst_hostname, lv_color_hex(BZ_TEXT), 0);
    lv_obj_set_style_border_width(ta_inst_hostname, 1, 0);
    lv_obj_set_style_border_color(ta_inst_hostname, lv_color_hex(BZ_BUTTON_LINE), 0);
    lv_obj_set_style_border_color(ta_inst_hostname, lv_color_hex(BZ_ACCENT), LV_STATE_FOCUSED);
    lv_obj_add_event_cb(ta_inst_hostname, installer_host_click_cb, LV_EVENT_PRESSED, win_installer);
    lv_obj_add_event_cb(ta_inst_hostname, installer_host_click_cb, LV_EVENT_CLICKED, win_installer);

    /* Pergunta 2: Layout do Teclado */
    lv_obj_t *row_kb = lv_obj_create(cfg_card);
    lv_obj_clear_flag(row_kb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row_kb, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(row_kb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_kb, 0, 0);
    lv_obj_set_style_pad_all(row_kb, 0, 0);
    lv_obj_set_style_pad_column(row_kb, 8, 0);
    lv_obj_set_flex_flow(row_kb, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_kb, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_q2 = lv_label_create(row_kb);
    lv_obj_set_width(lbl_q2, 225);
    lv_obj_set_style_text_font(lbl_q2, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_q2, lv_color_hex(BZ_TEXT), 0);
    lv_label_set_text(lbl_q2, "2. Layout do Teclado:");

    btn_inst_kb_br = lv_btn_create(row_kb);
    lv_obj_set_size(btn_inst_kb_br, 205, 30);
    bz_style_button(btn_inst_kb_br);
    lv_obj_t *l_kbr = lv_label_create(btn_inst_kb_br);
    lv_obj_set_style_text_font(l_kbr, &lv_font_montserrat_12, 0);
    lv_label_set_text(l_kbr, "ABNT2 (Portugues-BR)");
    lv_obj_center(l_kbr);
    lv_obj_add_event_cb(btn_inst_kb_br, installer_kb_br_cb, LV_EVENT_CLICKED, NULL);

    btn_inst_kb_us = lv_btn_create(row_kb);
    lv_obj_set_size(btn_inst_kb_us, 205, 30);
    bz_style_button(btn_inst_kb_us);
    lv_obj_t *l_kus = lv_label_create(btn_inst_kb_us);
    lv_obj_set_style_text_font(l_kus, &lv_font_montserrat_12, 0);
    lv_label_set_text(l_kus, "US Internacional");
    lv_obj_center(l_kus);
    lv_obj_add_event_cb(btn_inst_kb_us, installer_kb_us_cb, LV_EVENT_CLICKED, NULL);

    /* Pergunta 3: Perfil de Pacotes (Download GitHub HTTPS) */
    lv_obj_t *row_pf = lv_obj_create(cfg_card);
    lv_obj_clear_flag(row_pf, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(row_pf, lv_pct(100), 34);
    lv_obj_set_style_bg_opa(row_pf, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row_pf, 0, 0);
    lv_obj_set_style_pad_all(row_pf, 0, 0);
    lv_obj_set_style_pad_column(row_pf, 8, 0);
    lv_obj_set_flex_flow(row_pf, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row_pf, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *lbl_q3 = lv_label_create(row_pf);
    lv_obj_set_width(lbl_q3, 225);
    lv_obj_set_style_text_font(lbl_q3, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_q3, lv_color_hex(BZ_TEXT), 0);
    lv_label_set_text(lbl_q3, "3. Perfil de Download GitHub:");

    btn_inst_prof_full = lv_btn_create(row_pf);
    lv_obj_set_size(btn_inst_prof_full, 205, 30);
    bz_style_button(btn_inst_prof_full);
    lv_obj_t *l_pf1 = lv_label_create(btn_inst_prof_full);
    lv_obj_set_style_text_font(l_pf1, &lv_font_montserrat_12, 0);
    lv_label_set_text(l_pf1, "Completo (Apps + Jogos)");
    lv_obj_center(l_pf1);
    lv_obj_add_event_cb(btn_inst_prof_full, installer_prof_full_cb, LV_EVENT_CLICKED, NULL);

    btn_inst_prof_min = lv_btn_create(row_pf);
    lv_obj_set_size(btn_inst_prof_min, 205, 30);
    bz_style_button(btn_inst_prof_min);
    lv_obj_t *l_pf2 = lv_label_create(btn_inst_prof_min);
    lv_obj_set_style_text_font(l_pf2, &lv_font_montserrat_12, 0);
    lv_label_set_text(l_pf2, "Essencial (Base + Utils)");
    lv_obj_center(l_pf2);
    lv_obj_add_event_cb(btn_inst_prof_min, installer_prof_min_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl_note = lv_label_create(cfg_card);
    lv_obj_set_width(lbl_note, lv_pct(100));
    lv_obj_set_style_text_font(lbl_note, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_note, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text(lbl_note,
        "Nota: Apos instalar no HD, este instalador nao aparecera mais. Todas as atualizacoes futuras\n"
        "de aplicativos e do sistema operacional sao feitas direto pela Central de Pacotes (lpkg).");

    /* 3. Card de Progresso e Botoes de Acao */
    lv_obj_t *prog_card = lv_obj_create(content);
    lv_obj_clear_flag(prog_card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_width(prog_card, lv_pct(100));
    lv_obj_set_flex_grow(prog_card, 1);
    lv_obj_set_style_bg_color(prog_card, lv_color_hex(BZ_VIEW), 0);
    lv_obj_set_style_bg_opa(prog_card, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(prog_card, lv_color_hex(BZ_PANEL_LINE), 0);
    lv_obj_set_style_border_width(prog_card, 1, 0);
    lv_obj_set_style_radius(prog_card, 3, 0);
    lv_obj_set_style_pad_all(prog_card, 10, 0);
    lv_obj_set_style_pad_row(prog_card, 6, 0);
    lv_obj_set_flex_flow(prog_card, LV_FLEX_FLOW_COLUMN);

    lbl_inst_step = lv_label_create(prog_card);
    lv_obj_set_style_text_font(lbl_inst_step, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_inst_step, lv_color_hex(BZ_TEXT), 0);
    lv_label_set_text(lbl_inst_step, "Pronto para instalar o LiwusOS no Disco Rigido.");

    lbl_inst_log = lv_label_create(prog_card);
    lv_obj_set_style_text_font(lbl_inst_log, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_inst_log, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text(lbl_inst_log, "Revise as opcoes acima e clique em 'Instalar LiwusOS no HD Agora'.");

    bar_inst_prog = lv_bar_create(prog_card);
    lv_obj_set_size(bar_inst_prog, lv_pct(100), 14);
    lv_bar_set_range(bar_inst_prog, 0, 100);
    lv_bar_set_value(bar_inst_prog, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar_inst_prog, lv_color_hex(0x31363b), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar_inst_prog, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar_inst_prog, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar_inst_prog, lv_color_hex(BZ_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar_inst_prog, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar_inst_prog, 3, LV_PART_INDICATOR);

    lv_obj_t *act_row = lv_obj_create(prog_card);
    lv_obj_clear_flag(act_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(act_row, lv_pct(100), 36);
    lv_obj_set_style_bg_opa(act_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(act_row, 0, 0);
    lv_obj_set_style_pad_all(act_row, 0, 0);
    lv_obj_set_style_pad_column(act_row, 10, 0);
    lv_obj_set_flex_flow(act_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(act_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    btn_inst_start = lv_btn_create(act_row);
    lv_obj_set_size(btn_inst_start, 270, 32);
    bz_style_button(btn_inst_start);
    lv_obj_set_style_bg_color(btn_inst_start, lv_color_hex(0x215d80), 0);
    lv_obj_set_style_border_color(btn_inst_start, lv_color_hex(BZ_ACCENT), 0);
    lv_obj_t *l_st = lv_label_create(btn_inst_start);
    lv_label_set_text(l_st, LV_SYMBOL_DRIVE " Instalar LiwusOS no HD Agora");
    lv_obj_center(l_st);
    lv_obj_add_event_cb(btn_inst_start, installer_start_cb, LV_EVENT_CLICKED, NULL);

    btn_inst_reboot = lv_btn_create(act_row);
    lv_obj_set_size(btn_inst_reboot, 210, 32);
    bz_style_button(btn_inst_reboot);
    lv_obj_add_state(btn_inst_reboot, LV_STATE_DISABLED);
    lv_obj_t *l_rb = lv_label_create(btn_inst_reboot);
    lv_label_set_text(l_rb, LV_SYMBOL_REFRESH " Reiniciar pelo HD");
    lv_obj_center(l_rb);
    lv_obj_add_event_cb(btn_inst_reboot, installer_reboot_cb, LV_EVENT_CLICKED, NULL);

    installer_refresh_toggles();
    installer_detect_disk();
    lv_obj_add_flag(win_installer, LV_OBJ_FLAG_HIDDEN);
}

/* =========================================================================
 * Criação da Janela Sobre o LiwusOS
 * ========================================================================= */
static void create_about_window(void) {
    if (win_about) return;

    win_about = lv_win_create(lv_scr_act(), WIN_HEADER_H);
    setup_window(win_about, 440, 280, 292, 160);
    add_window_controls(win_about, "Sobre o LiwusOS");

    lv_obj_t *content = lv_win_get_content(win_about);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(content, 18, 0);

    lbl_about_title = lv_label_create(content);
    lv_obj_set_style_text_font(lbl_about_title, &lv_font_montserrat_20, 0);
    char cur_ver[16];
    lpkg_get_os_version(cur_ver, sizeof(cur_ver));
    lv_label_set_text_fmt(lbl_about_title, "LiwusOS v%s", cur_ver);

    lv_obj_t *lbl_desc = lv_label_create(content);
    lv_obj_align(lbl_desc, LV_ALIGN_TOP_LEFT, 0, 40);
    lv_obj_set_style_text_color(lbl_desc, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text(lbl_desc,
        "Sistema Operacional x86_64 Monolitico.\n\n"
        "Ambiente Grafico: LVGL v8.3 (tema Breeze)\n"
        "Editor Oficial: Kilo Text Editor\n"
        "Compilador: TinyCC (tcc) Nativo\n"
        "Rede: TCP/IP lwIP + TLS BearSSL");

    lv_obj_add_flag(win_about, LV_OBJ_FLAG_HIDDEN);
}

/* =========================================================================
 * Painel Inferior (Barra de Tarefas estilo KDE Plasma) e Menu Iniciar
 * ========================================================================= */
typedef struct {
    const char *icon;
    const char *label;
    lv_event_cb_t open_cb;
    lv_obj_t **win;
    lv_obj_t *btn;
    lv_obj_t *indicator;
    int state; /* -1 = desconhecido, 0 = fechado, 1 = aberto, 2 = ativo */
    bool live_only;
} task_item_t;

static task_item_t g_task_items[] = {
    { LV_SYMBOL_KEYBOARD, "Konsole",  dock_term_cb,      &win_terminal,  NULL, NULL, -1, false },
    { LV_SYMBOL_EYE_OPEN, "LiwusWeb", dock_browser_cb,   &win_browser,   NULL, NULL, -1, false },
    { LV_SYMBOL_DOWNLOAD, "Pacotes",  dock_pkgmgr_cb,    &win_pkgmgr,    NULL, NULL, -1, false },
    { LV_SYMBOL_DRIVE,    "Instalar", dock_installer_cb, &win_installer, NULL, NULL, -1, true  },
    { LV_SYMBOL_SETTINGS, "Monitor",  dock_sysmon_cb,    &win_sysmon,    NULL, NULL, -1, false },
    { LV_SYMBOL_PLUS,     "Calc",     dock_calc_cb,      &win_calc,      NULL, NULL, -1, false },
    { LV_SYMBOL_PLAY,     "DOOM",     dock_doom_cb,      NULL,           NULL, NULL, -1, false },
    { LV_SYMBOL_PLAY,     "SNES",     dock_snes_cb,      NULL,           NULL, NULL, -1, false },
};
#define TASK_ITEM_COUNT ((int)(sizeof(g_task_items) / sizeof(g_task_items[0])))

static lv_obj_t *g_taskbar = NULL;
static lv_obj_t *g_start_menu = NULL;
static lv_obj_t *g_launcher_btn = NULL;

static void start_menu_hide(void) {
    if (g_start_menu) lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
    if (g_launcher_btn) lv_obj_clear_state(g_launcher_btn, LV_STATE_CHECKED);
}

static void launcher_click_cb(lv_event_t *e) {
    (void)e;
    if (!g_start_menu) return;
    if (lv_obj_has_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(g_start_menu);
        if (g_launcher_btn) lv_obj_add_state(g_launcher_btn, LV_STATE_CHECKED);
    } else {
        start_menu_hide();
    }
}

static void desktop_bg_press_cb(lv_event_t *e) {
    (void)e;
    start_menu_hide();
}

/* Janela visivel mais ao topo (a "janela ativa"). */
static lv_obj_t *desktop_active_window(void) {
    lv_obj_t *wins[] = { win_terminal, win_browser, win_pkgmgr, win_installer, win_sysmon, win_calc, win_about };
    lv_obj_t *top = NULL;
    int32_t top_idx = -1;
    for (size_t i = 0; i < sizeof(wins) / sizeof(wins[0]); i++) {
        lv_obj_t *w = wins[i];
        if (!w || lv_obj_has_flag(w, LV_OBJ_FLAG_HIDDEN)) continue;
        int32_t idx = (int32_t)lv_obj_get_index(w);
        if (idx > top_idx) { top_idx = idx; top = w; }
    }
    return top;
}

static void task_btn_click_cb(lv_event_t *e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= TASK_ITEM_COUNT) return;
    task_item_t *it = &g_task_items[idx];
    start_menu_hide();
    if (it->win && *it->win && *it->win == desktop_active_window()) {
        /* Clicar na tarefa ativa minimiza, como no Plasma. */
        lv_obj_add_flag(*it->win, LV_OBJ_FLAG_HIDDEN);
        if (*it->win == win_browser) set_browser_url_focus(false);
        if (*it->win == win_pkgmgr) set_pkg_input_focus(false);
        if (*it->win == win_installer) set_inst_host_focus(false);
        return;
    }
    if (it->open_cb) it->open_cb(NULL);
}

static void taskbar_sync(void) {
    lv_obj_t *active = desktop_active_window();
    for (int i = 0; i < TASK_ITEM_COUNT; i++) {
        task_item_t *it = &g_task_items[i];
        if (!it->btn) continue;
        int st = 0;
        if (it->win && *it->win && !lv_obj_has_flag(*it->win, LV_OBJ_FLAG_HIDDEN)) {
            st = (*it->win == active) ? 2 : 1;
        }
        if (st == it->state) continue;
        it->state = st;
        if (st == 0) {
            lv_obj_add_flag(it->indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(it->btn, LV_OPA_TRANSP, 0);
        } else {
            lv_obj_clear_flag(it->indicator, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(it->indicator,
                lv_color_hex(st == 2 ? BZ_ACCENT : 0x7f8c8d), 0);
            lv_obj_set_style_bg_color(it->btn, lv_color_hex(BZ_HOVER), 0);
            lv_obj_set_style_bg_opa(it->btn, st == 2 ? LV_OPA_70 : LV_OPA_30, 0);
        }
    }
}

static lv_obj_t *panel_separator(lv_obj_t *parent) {
    lv_obj_t *sep = lv_obj_create(parent);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(sep, 1, PANEL_H - 16);
    lv_obj_set_style_bg_color(sep, lv_color_hex(BZ_PANEL_LINE), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);
    return sep;
}

static void create_taskbar(void) {
    uint32_t sw = desktop_screen_w();
    uint32_t sh = desktop_screen_h();

    g_taskbar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_taskbar, (lv_coord_t)sw, PANEL_H);
    lv_obj_set_pos(g_taskbar, 0, (lv_coord_t)(sh - PANEL_H));
    lv_obj_clear_flag(g_taskbar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_taskbar, lv_color_hex(BZ_PANEL), 0);
    lv_obj_set_style_bg_opa(g_taskbar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(g_taskbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(g_taskbar, lv_color_hex(BZ_PANEL_LINE), 0);
    lv_obj_set_style_border_width(g_taskbar, 1, 0);
    lv_obj_set_style_radius(g_taskbar, 0, 0);
    lv_obj_set_style_pad_all(g_taskbar, 0, 0);
    lv_obj_set_style_pad_left(g_taskbar, 2, 0);
    lv_obj_set_style_pad_column(g_taskbar, 3, 0);
    lv_obj_set_style_text_color(g_taskbar, lv_color_hex(BZ_TEXT), 0);
    lv_obj_set_flex_flow(g_taskbar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(g_taskbar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    /* Lancador de aplicativos (logotipo) */
    g_launcher_btn = lv_btn_create(g_taskbar);
    lv_obj_set_size(g_launcher_btn, PANEL_H, PANEL_H - 4);
    lv_obj_set_style_bg_opa(g_launcher_btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(g_launcher_btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(g_launcher_btn, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g_launcher_btn, lv_color_hex(BZ_HOVER), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(g_launcher_btn, lv_color_hex(BZ_HOVER), LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(g_launcher_btn, 0, 0);
    lv_obj_set_style_radius(g_launcher_btn, 3, 0);
    lv_obj_set_style_pad_all(g_launcher_btn, 0, 0);
    lv_obj_add_event_cb(g_launcher_btn, launcher_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *logo = lv_obj_create(g_launcher_btn);
    lv_obj_clear_flag(logo, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(logo, 26, 26);
    lv_obj_center(logo);
    lv_obj_set_style_radius(logo, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(logo, lv_color_hex(BZ_ACCENT), 0);
    lv_obj_set_style_bg_opa(logo, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(logo, 0, 0);
    lv_obj_set_style_pad_all(logo, 0, 0);
    lv_obj_t *logo_lbl = lv_label_create(logo);
    lv_obj_set_style_text_font(logo_lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(logo_lbl, lv_color_white(), 0);
    lv_label_set_text(logo_lbl, "L");
    lv_obj_center(logo_lbl);

    panel_separator(g_taskbar);

    /* Gerenciador de tarefas */
    lv_coord_t btn_w = is_live_mode ? 94 : 112;
    for (int i = 0; i < TASK_ITEM_COUNT; i++) {
        task_item_t *it = &g_task_items[i];
        if (it->live_only && !is_live_mode) {
            it->btn = NULL;
            it->indicator = NULL;
            continue;
        }
        lv_obj_t *btn = lv_btn_create(g_taskbar);
        lv_obj_set_size(btn, btn_w, PANEL_H - 6);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(btn, lv_color_hex(BZ_HOVER), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 2, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        lv_obj_set_style_pad_left(btn, 7, 0);
        lv_obj_add_event_cb(btn, task_btn_click_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(it->live_only ? BZ_ACCENT : BZ_TEXT), 0);
        lv_label_set_text_fmt(lbl, "%s  %s", it->icon, it->label);
        lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 0, 0);

        lv_obj_t *ind = lv_obj_create(btn);
        lv_obj_clear_flag(ind, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(ind, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_set_size(ind, btn_w - 10, 2);
        lv_obj_align(ind, LV_ALIGN_TOP_MID, -4, 0);
        lv_obj_set_style_bg_color(ind, lv_color_hex(BZ_ACCENT), 0);
        lv_obj_set_style_bg_opa(ind, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(ind, 0, 0);
        lv_obj_set_style_radius(ind, 0, 0);
        lv_obj_add_flag(ind, LV_OBJ_FLAG_HIDDEN);

        it->btn = btn;
        it->indicator = ind;
        it->state = -1;
    }

    /* Bandeja do sistema (alinhada a direita, fora do layout flex) */
    lbl_clock = lv_label_create(g_taskbar);
    lv_obj_add_flag(lbl_clock, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_text_font(lbl_clock, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_clock, lv_color_hex(BZ_TEXT), 0);
    lv_label_set_text(lbl_clock, "00:00");
    lv_obj_align(lbl_clock, LV_ALIGN_RIGHT_MID, -16, 0);

    lv_obj_t *sep = panel_separator(g_taskbar);
    lv_obj_add_flag(sep, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_align(sep, LV_ALIGN_RIGHT_MID, -80, 0);

    lbl_ram = lv_label_create(g_taskbar);
    lv_obj_add_flag(lbl_ram, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_text_color(lbl_ram, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text(lbl_ram, LV_SYMBOL_SD_CARD "  -- MB");
    lv_obj_align(lbl_ram, LV_ALIGN_RIGHT_MID, -96, 0);

    lbl_net = lv_label_create(g_taskbar);
    lv_obj_add_flag(lbl_net, LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING);
    lv_obj_set_style_text_color(lbl_net, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text(lbl_net, LV_SYMBOL_WIFI "  Conectando");
    lv_obj_align(lbl_net, LV_ALIGN_RIGHT_MID, -210, 0);
}

/* ----------------------------- Menu Iniciar ----------------------------- */
static void menu_power_cb(lv_event_t *e) {
    int action = (int)(intptr_t)lv_event_get_user_data(e);
    start_menu_hide();
    if (action == 1) acpi_reboot();
    else acpi_poweroff();
}

static void menu_item_cb(lv_event_t *e) {
    lv_event_cb_t cb = (lv_event_cb_t)lv_event_get_user_data(e);
    start_menu_hide();
    if (cb) cb(NULL);
}

static lv_obj_t *menu_add_entry(lv_obj_t *parent, const char *icon, const char *title,
                                const char *subtitle, lv_event_cb_t cb, void *user_data) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, lv_pct(100), subtitle ? 46 : 36);
    lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2c6b8f), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_radius(btn, 3, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_set_style_pad_left(btn, 12, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *ico = lv_label_create(btn);
    lv_obj_set_style_text_font(ico, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(ico, lv_color_hex(BZ_ACCENT), 0);
    lv_label_set_text(ico, icon);
    lv_obj_align(ico, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_color(lbl, lv_color_hex(BZ_TEXT), 0);
    lv_label_set_text(lbl, title);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 34, subtitle ? -8 : 0);

    if (subtitle) {
        lv_obj_t *sub = lv_label_create(btn);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(sub, lv_color_hex(BZ_TEXT_DIM), 0);
        lv_label_set_text(sub, subtitle);
        lv_obj_align(sub, LV_ALIGN_LEFT_MID, 34, 9);
    }
    return btn;
}

static void create_start_menu(void) {
    uint32_t sh = desktop_screen_h();
    const lv_coord_t mw = 320;
    const lv_coord_t mh = is_live_mode ? 514 : 466;

    g_start_menu = lv_obj_create(lv_layer_top());
    lv_obj_set_size(g_start_menu, mw, mh);
    lv_obj_set_pos(g_start_menu, 0, (lv_coord_t)(sh - PANEL_H - mh));
    lv_obj_clear_flag(g_start_menu, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_start_menu, lv_color_hex(BZ_WIN_BG), 0);
    lv_obj_set_style_bg_opa(g_start_menu, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(g_start_menu, lv_color_hex(BZ_TITLE_LINE), 0);
    lv_obj_set_style_border_width(g_start_menu, 1, 0);
    lv_obj_set_style_radius(g_start_menu, 0, 0);
    lv_obj_set_style_shadow_width(g_start_menu, 20, 0);
    lv_obj_set_style_shadow_color(g_start_menu, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(g_start_menu, LV_OPA_50, 0);
    lv_obj_set_style_pad_all(g_start_menu, 0, 0);
    lv_obj_set_style_text_color(g_start_menu, lv_color_hex(BZ_TEXT), 0);

    /* Cabecalho com usuario */
    lv_obj_t *head = lv_obj_create(g_start_menu);
    lv_obj_clear_flag(head, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(head, lv_pct(100), 64);
    lv_obj_align(head, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(head, lv_color_hex(BZ_TITLE), 0);
    lv_obj_set_style_bg_opa(head, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(head, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(head, lv_color_hex(BZ_TITLE_LINE), 0);
    lv_obj_set_style_border_width(head, 1, 0);
    lv_obj_set_style_radius(head, 0, 0);
    lv_obj_set_style_pad_all(head, 0, 0);

    lv_obj_t *avatar = lv_obj_create(head);
    lv_obj_clear_flag(avatar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(avatar, 38, 38);
    lv_obj_align(avatar, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_set_style_radius(avatar, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(avatar, lv_color_hex(0x4d5359), 0);
    lv_obj_set_style_bg_opa(avatar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(avatar, 0, 0);
    lv_obj_set_style_pad_all(avatar, 0, 0);
    lv_obj_t *av_lbl = lv_label_create(avatar);
    lv_obj_set_style_text_font(av_lbl, &lv_font_montserrat_18, 0);
    lv_label_set_text(av_lbl, "R");
    lv_obj_center(av_lbl);

    lv_obj_t *user = lv_label_create(head);
    lv_obj_set_style_text_font(user, &lv_font_montserrat_16, 0);
    lv_label_set_text(user, "root");
    lv_obj_align(user, LV_ALIGN_LEFT_MID, 64, -9);
    lv_obj_t *host = lv_label_create(head);
    lv_obj_set_style_text_font(host, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(host, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_label_set_text_fmt(host, "%s  -  %s",
                          g_system_hostname[0] ? g_system_hostname : "liwus",
                          is_live_mode ? "Modo Live USB" : "Instalado no HD");
    lv_obj_align(host, LV_ALIGN_LEFT_MID, 64, 10);

    /* Lista de aplicativos */
    lv_obj_t *list = lv_obj_create(g_start_menu);
    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(list, lv_pct(100), mh - 64 - 48);
    lv_obj_align(list, LV_ALIGN_TOP_LEFT, 0, 64);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_radius(list, 0, 0);
    lv_obj_set_style_pad_all(list, 6, 0);
    lv_obj_set_style_pad_row(list, 2, 0);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *cap = lv_label_create(list);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cap, lv_color_hex(BZ_TEXT_DIM), 0);
    lv_obj_set_style_pad_left(cap, 8, 0);
    lv_obj_set_style_pad_ver(cap, 4, 0);
    lv_label_set_text(cap, "Aplicativos");

    if (is_live_mode) {
        menu_add_entry(list, LV_SYMBOL_DRIVE, "Instalar Sistema", "Instalar LiwusOS no Disco Rigido (HD)", menu_item_cb, (void *)dock_installer_cb);
    }
    menu_add_entry(list, LV_SYMBOL_KEYBOARD, "Konsole", "Terminal do sistema", menu_item_cb, (void *)dock_term_cb);
    menu_add_entry(list, LV_SYMBOL_EYE_OPEN, "LiwusWeb", "Navegador de internet", menu_item_cb, (void *)dock_browser_cb);
    menu_add_entry(list, LV_SYMBOL_DOWNLOAD, "Central de Pacotes", "Baixar apps e atualizar sistema (lpkg)", menu_item_cb, (void *)dock_pkgmgr_cb);
    menu_add_entry(list, LV_SYMBOL_SETTINGS, "Monitor do Sistema", "Memoria, tarefas e uptime", menu_item_cb, (void *)dock_sysmon_cb);
    menu_add_entry(list, LV_SYMBOL_PLUS, "Calculadora", "Operacoes basicas", menu_item_cb, (void *)dock_calc_cb);
    menu_add_entry(list, LV_SYMBOL_PLAY, "DOOM", "Shareware (id Software, 1993)", menu_item_cb, (void *)dock_doom_cb);
    menu_add_entry(list, LV_SYMBOL_PLAY, "Emulador de SNES", "Jogar ROMs proprias de Super Nintendo", menu_item_cb, (void *)dock_snes_cb);
    menu_add_entry(list, LV_SYMBOL_FILE, "Sobre o LiwusOS", NULL, menu_item_cb, (void *)dock_about_cb);

    /* Rodape com energia */
    lv_obj_t *foot = lv_obj_create(g_start_menu);
    lv_obj_clear_flag(foot, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(foot, lv_pct(100), 48);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(foot, lv_color_hex(BZ_TITLE), 0);
    lv_obj_set_style_bg_opa(foot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(foot, lv_color_hex(BZ_TITLE_LINE), 0);
    lv_obj_set_style_border_width(foot, 1, 0);
    lv_obj_set_style_radius(foot, 0, 0);
    lv_obj_set_style_pad_all(foot, 8, 0);
    lv_obj_set_style_pad_column(foot, 6, 0);
    lv_obj_set_flex_flow(foot, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(foot, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *btn_reboot = lv_btn_create(foot);
    lv_obj_set_height(btn_reboot, 30);
    bz_style_button(btn_reboot);
    lv_obj_t *l1 = lv_label_create(btn_reboot);
    lv_label_set_text(l1, LV_SYMBOL_REFRESH "  Reiniciar");
    lv_obj_center(l1);
    lv_obj_add_event_cb(btn_reboot, menu_power_cb, LV_EVENT_CLICKED, (void *)(intptr_t)1);

    lv_obj_t *btn_off = lv_btn_create(foot);
    lv_obj_set_height(btn_off, 30);
    bz_style_button(btn_off);
    lv_obj_t *l2 = lv_label_create(btn_off);
    lv_label_set_text(l2, LV_SYMBOL_POWER "  Desligar");
    lv_obj_center(l2);
    lv_obj_add_event_cb(btn_off, menu_power_cb, LV_EVENT_CLICKED, (void *)(intptr_t)0);

    lv_obj_add_flag(g_start_menu, LV_OBJ_FLAG_HIDDEN);
}

/* =========================================================================
 * Inicializacao e Loop do Desktop
 * ========================================================================= */
void desktop_init(void) {
    lvgl_port_init();
    html_fonts_init();

    lv_obj_t *scr = lv_scr_act();
    /* Papel de parede: gradiente vertical escuro azul-acinzentado */
    lv_obj_set_style_bg_color(scr, lv_color_hex(BZ_WALL_TOP), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(BZ_WALL_BOTTOM), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    /* TRAVA O DESKTOP: impede absolutamente qualquer scroll ou deslocamento da tela inteira! */
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(scr, desktop_bg_press_cb, LV_EVENT_PRESSED, NULL);

    init_cursor();

    /* Pre-cria as janelas essenciais */
    create_terminal_window();
    create_sysmon_window();
    create_calc_window();
    create_browser_window();
    create_pkgmgr_window();
    if (is_live_mode) {
        create_installer_window();
    }
    create_about_window();

    create_taskbar();
    create_start_menu();

    if (is_live_mode) {
        term_append("LiwusOS (Modo Live USB / Breeze Dark) - Abra 'Instalar' para instalar no HD.\n");
    } else {
        term_append("LiwusOS (Instalado no Disco Rigido HD / Breeze Dark)\n");
    }
    term_append("Digite 'lpkg list' para ver pacotes ou 'help' para comandos.\n\n");
    terminal_update_view();
    desktop_update_topbar_status(true);

    g_desktop_running = true;
}

bool desktop_is_running(void) {
    return g_desktop_running;
}

static void desktop_update_topbar_status(bool force) {
    static uint32_t last_status_tick = 0;
    if (!force && (timer_ticks - last_status_tick < 100)) return;
    last_status_tick = timer_ticks;

    rtc_time_t t = rtc_read_time();
    char time_str[16];
    lv_snprintf(time_str, sizeof(time_str), "%02u:%02u", t.hour, t.minute);
    if (lbl_clock) lv_label_set_text(lbl_clock, time_str);

    uint64_t free_mb = pmm_get_free_memory() / (1024ULL * 1024ULL);
    char ram_str[32];
    lv_snprintf(ram_str, sizeof(ram_str), LV_SYMBOL_SD_CARD "  %u MB", (uint32_t)free_mb);
    if (lbl_ram) lv_label_set_text(lbl_ram, ram_str);

    /* Atualiza status da rede dinamicamente na bandeja */
    if (lbl_net) {
        if (net_has_ip()) {
            char ip[16];
            net_get_ip_strings(ip, NULL, NULL);
            char net_str[32];
            lv_snprintf(net_str, sizeof(net_str), LV_SYMBOL_WIFI "  %s", ip);
            lv_label_set_text(lbl_net, net_str);
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(BZ_TEXT_DIM), 0);
        } else if (net_get_link_status()) {
            lv_label_set_text(lbl_net, LV_SYMBOL_WIFI "  Sem IP");
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(BZ_AMBER), 0);
        } else {
            lv_label_set_text(lbl_net, LV_SYMBOL_WIFI "  Desconectado");
            lv_obj_set_style_text_color(lbl_net, lv_color_hex(BZ_RED), 0);
        }
    }

    if (lbl_sys_info && win_sysmon && !lv_obj_has_flag(win_sysmon, LV_OBJ_FLAG_HIDDEN)) {
        task_info_t tasks[16];
        int ntasks = task_snapshot(tasks, 16);
        char sys_buf[256];
        lv_snprintf(sys_buf, sizeof(sys_buf),
            "Uptime: %u s\nMemoria Livre: %u MB\nTarefas Ativas: %d",
            timer_ticks / 100, (uint32_t)free_mb, ntasks);
        lv_label_set_text(lbl_sys_info, sys_buf);
    }
}

void desktop_task(void) {
    if (!g_desktop_running) return;

    uint32_t sw = vga_get_fb_width();
    uint32_t sh = vga_get_fb_height();
    uint32_t *backbuffer = vga_get_backbuffer();

    if (!backbuffer || sw == 0 || sh == 0) return;

    for (;;) {
        net_poll();
        usb_ehci_service();
        usb_xhci_poll();
        browser_fetch_poll();
        browser_runtime_poll_update();

        /* Coleta de entradas do teclado */
        bool browser_visible = win_browser && !lv_obj_has_flag(win_browser, LV_OBJ_FLAG_HIDDEN);
        bool pkg_visible = win_pkgmgr && !lv_obj_has_flag(win_pkgmgr, LV_OBJ_FLAG_HIDDEN);
        bool inst_visible = win_installer && !lv_obj_has_flag(win_installer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *target_ta = NULL;
        bool target_is_url = false;
        bool target_is_pkg = false;
        if (inst_visible && g_inst_host_focused && ta_inst_hostname) {
            target_ta = ta_inst_hostname;
        } else if (pkg_visible && g_pkg_input_focused && ta_pkg_input) {
            target_ta = ta_pkg_input;
            target_is_pkg = true;
        } else if (browser_visible) {
            if (g_active_web_input) {
                target_ta = g_active_web_input;
            } else if (g_browser_url_focused && ta_browser_url) {
                target_ta = ta_browser_url;
                target_is_url = true;
            }
        }
        char k;
        while (vga_putc_capture_hook == NULL && keyboard_pop_char(&k)) {
            unsigned char uk = (unsigned char)k;
            if (browser_visible && obj_browser_view && !target_is_pkg) {
                int scroll_dy = 0;
                if (uk == 128) scroll_dy = 48;         /* Seta Cima */
                else if (uk == 129) scroll_dy = -48;   /* Seta Baixo */
                else if (uk == 134) scroll_dy = 180;   /* PageUp */
                else if (uk == 135) scroll_dy = -180;  /* PageDown */
                if (scroll_dy != 0) {
                    lv_obj_scroll_by_bounded(obj_browser_view, 0, scroll_dy, LV_ANIM_OFF);
                    continue;
                }
            }

            if (target_ta != NULL) {
                if (uk == '\n' || uk == '\r') {
                    if (target_is_url) {
                        const char *txt = lv_textarea_get_text(ta_browser_url);
                        if (txt) {
                            strncpy(g_browser_url, txt, sizeof(g_browser_url) - 1);
                            g_browser_url[sizeof(g_browser_url) - 1] = '\0';
                        }
                        browser_go_cb(NULL);
                    } else if (target_is_pkg) {
                        pkgmgr_install_input_cb(NULL);
                    } else {
                        lv_event_send(target_ta, LV_EVENT_READY, NULL);
                    }
                } else if (uk == '\b' || uk == 0x7F) {
                    lv_textarea_del_char(target_ta);
                } else if (uk == 136) { /* Delete */
                    lv_textarea_del_char_forward(target_ta);
                } else if (uk == 130) { /* Seta Esquerda */
                    lv_textarea_cursor_left(target_ta);
                } else if (uk == 131) { /* Seta Direita */
                    lv_textarea_cursor_right(target_ta);
                } else if (uk == 132) { /* Home */
                    lv_textarea_set_cursor_pos(target_ta, 0);
                } else if (uk == 133) { /* End */
                    lv_textarea_set_cursor_pos(target_ta, LV_TEXTAREA_CURSOR_LAST);
                } else if (uk == 21 || uk == 12) { /* Ctrl+U / Ctrl+L limpa campo */
                    lv_textarea_set_text(target_ta, "");
                } else if (uk >= 32 && uk < 127) {
                    lv_textarea_add_char(target_ta, uk);
                }
                continue;
            }

            if (k == '\n' || k == '\r') {
                if (g_konsole_input_len > 0) {
                    char to_exec[128];
                    strncpy(to_exec, g_konsole_input, sizeof(to_exec) - 1);
                    to_exec[sizeof(to_exec) - 1] = '\0';
                    g_konsole_input[0] = '\0';
                    g_konsole_input_len = 0;
                    term_exec_cmd(to_exec);
                } else {
                    term_append("\n");
                    terminal_update_view();
                }
            } else if (k == '\b') {
                if (g_konsole_input_len > 0) {
                    g_konsole_input_len--;
                    g_konsole_input[g_konsole_input_len] = '\0';
                    terminal_update_view();
                }
            } else if ((unsigned char)k >= 32 && g_konsole_input_len < (int)sizeof(g_konsole_input) - 1) {
                g_konsole_input[g_konsole_input_len++] = k;
                g_konsole_input[g_konsole_input_len] = '\0';
                terminal_update_view();
            }
        }

        /* Atualização periódica do relógio e status */
        desktop_update_topbar_status(false);
        taskbar_sync();

        /* Ciclo do motor gráfico LVGL */
        lvgl_port_update();
        vga_flip();
    }
}
