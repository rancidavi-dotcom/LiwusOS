/*
 * gui/apps/gui_terminal.c
 *
 * A real terminal emulator widget for LiwusOS GUI.
 *
 * Architecture:
 *  - Maintains a scrollback cell buffer: gterm_cell_t[GTERM_ROWS][GTERM_COLS]
 *  - Receives characters from user keyboard via window_node callbacks
 *  - On Enter: installs vga_output_hook, runs terminal_execute(), uninstalls hook
 *  - vga_output_hook feeds the command output into the cell buffer
 *  - Renders every cell directly in the node's draw vtable (no label widget needed)
 *  - Scroll: when cursor reaches bottom row, shifts all rows up by one
 */
#include "gui_terminal.h"
#include "../scene/node.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "../assets/asset_manager.h"
#include "../widgets/window_node.h"
#include "../window/focus_manager.h"
#include "../core/event_bus.h"
#include "kheap.h"
#include "string.h"
#include "vga.h"

/* Forward declarations from terminal subsystem */
extern int terminal_parse_line(char *line, char **argv);
extern void terminal_execute(int argc, char **argv);

/* Forward declaration */
extern gui_event_bus_t   *g_event_bus;
extern focus_manager_t   *g_focus_manager;

/* ---- Terminal state ---- */

#define GTERM_MAX_ARGS 16

typedef struct gui_terminal {
    gterm_cell_t  cells[GTERM_ROWS][GTERM_COLS];
    int           cur_row;      /* current write row */
    int           cur_col;      /* current write column */

    char          input_line[GTERM_COLS + 1];
    int           input_len;

    const glyph_t *font;

    /* The window node that owns us */
    node_t       *win_node;
} gui_terminal_t;

/* Global pointer so the vga hook can reach the active terminal */
static gui_terminal_t *s_active_terminal = NULL;

/* --------------------------------------------------------------------------
 * Cell buffer helpers
 * -------------------------------------------------------------------------- */

static void gterm_newline(gui_terminal_t *t) {
    t->cur_col = 0;
    t->cur_row++;
    if (t->cur_row >= GTERM_ROWS) {
        /* Scroll up: shift every row one step */
        for (int r = 0; r < GTERM_ROWS - 1; r++) {
            for (int c = 0; c < GTERM_COLS; c++) {
                t->cells[r][c] = t->cells[r + 1][c];
            }
        }
        /* Clear last row */
        for (int c = 0; c < GTERM_COLS; c++) {
            t->cells[GTERM_ROWS - 1][c].ch = ' ';
            t->cells[GTERM_ROWS - 1][c].fg = 0xFFCCCCCC;
        }
        t->cur_row = GTERM_ROWS - 1;
    }
}

static void gterm_putchar(gui_terminal_t *t, char ch, uint32_t color) {
    if (ch == '\n' || ch == '\r') {
        gterm_newline(t);
        return;
    }
    if (ch == '\b') {
        if (t->cur_col > 0) t->cur_col--;
        t->cells[t->cur_row][t->cur_col].ch = ' ';
        return;
    }
    if (ch == '\t') {
        int next = (t->cur_col + 8) & ~7;
        if (next >= GTERM_COLS) next = GTERM_COLS - 1;
        while (t->cur_col < next) {
            t->cells[t->cur_row][t->cur_col].ch = ' ';
            t->cells[t->cur_row][t->cur_col].fg = color;
            t->cur_col++;
        }
        return;
    }
    if (ch < 32) return; /* ignore other control chars */

    if (t->cur_col >= GTERM_COLS) {
        gterm_newline(t);
    }
    t->cells[t->cur_row][t->cur_col].ch = ch;
    t->cells[t->cur_row][t->cur_col].fg = color;
    t->cur_col++;
}

static void gterm_puts(gui_terminal_t *t, const char *str, uint32_t color) {
    while (*str) {
        gterm_putchar(t, *str++, color);
    }
}

/* --------------------------------------------------------------------------
 * VGA output hook — called by vga_puts when installed
 * -------------------------------------------------------------------------- */

static void gterm_vga_hook(const char *str) {
    if (!s_active_terminal) return;
    gterm_puts(s_active_terminal, str, 0xFFCCCCCC);
}

/* --------------------------------------------------------------------------
 * Print prompt
 * -------------------------------------------------------------------------- */

static void gterm_print_prompt(gui_terminal_t *t) {
    gterm_puts(t, "root@liwusos# ", 0xFF00FF88);
}

/* --------------------------------------------------------------------------
 * Execute a command and capture its output
 * -------------------------------------------------------------------------- */

static void gterm_execute(gui_terminal_t *t) {
    /* Echo the entered command in white */
    gterm_puts(t, "\n", 0xFFFFFFFF);

    char cmd_copy[GTERM_COLS + 1];
    strncpy(cmd_copy, t->input_line, GTERM_COLS);
    cmd_copy[GTERM_COLS] = '\0';

    /* Parse */
    char *argv[GTERM_MAX_ARGS];
    int argc = terminal_parse_line(cmd_copy, argv);

    if (argc > 0) {
        /* Handle 'clear' internally */
        if (strcmp(argv[0], "clear") == 0) {
            for (int r = 0; r < GTERM_ROWS; r++) {
                for (int c = 0; c < GTERM_COLS; c++) {
                    t->cells[r][c].ch = ' ';
                    t->cells[r][c].fg = 0xFFCCCCCC;
                }
            }
            t->cur_row = 0;
            t->cur_col = 0;
        } else {
            /* Install hook, run command, uninstall hook */
            extern void (*vga_output_hook)(const char *);
            s_active_terminal = t;
            vga_output_hook = gterm_vga_hook;

            terminal_execute(argc, argv);

            vga_output_hook = NULL;
            // s_active_terminal = NULL; /* Keep it so the terminal doesn't disappear! */
        }
    }

    gterm_print_prompt(t);
}

/* --------------------------------------------------------------------------
 * Key handlers registered via window_node_set_key_handler
 * -------------------------------------------------------------------------- */

static bool gterm_key_down(node_t *self, uint8_t sc, void *ctx) {
    (void)self;
    gui_terminal_t *t = (gui_terminal_t *)ctx;
    if (!t) return true;

    if (sc == 0x0E) { /* Backspace */
        if (t->input_len > 0) {
            t->input_len--;
            t->input_line[t->input_len] = '\0';
            /* Erase from cell buffer */
            if (t->cur_col > 0) {
                t->cur_col--;
                t->cells[t->cur_row][t->cur_col].ch = ' ';
            }
            node_mark_dirty(t->win_node, NODE_DIRTY_PAINT);
        }
        return true;
    }
    if (sc == 0x1C) { /* Enter */
        t->input_line[t->input_len] = '\0';
        gterm_execute(t);
        t->input_len = 0;
        t->input_line[0] = '\0';
        node_mark_dirty(t->win_node, NODE_DIRTY_PAINT);
        return true;
    }
    /* Consume everything so canvas doesn't pan */
    return true;
}

static bool gterm_key_char(node_t *self, char c, void *ctx) {
    (void)self;
    gui_terminal_t *t = (gui_terminal_t *)ctx;
    if (!t) return true;

    if (c >= 32 && c <= 126 && t->input_len < GTERM_COLS - 1) {
        t->input_line[t->input_len++] = c;
        t->input_line[t->input_len]   = '\0';
        gterm_putchar(t, c, 0xFFFFFFFF);
        node_mark_dirty(t->win_node, NODE_DIRTY_PAINT);
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Draw
 * -------------------------------------------------------------------------- */

#define GTERM_CHAR_W  8
#define GTERM_CHAR_H  16
#define GTERM_PAD_X   6
#define GTERM_PAD_Y   4

static void gterm_draw(node_t *self, struct gui_renderer *r) {
    gui_terminal_t *t = s_active_terminal; /* We rely on the global for now */
    if (!t) return;

    if (!t->font) t->font = asset_manager_get_font(NULL);

    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    /* --- Draw the window chrome (titlebar, borders) via window_node draw --- */
    extern void window_node_draw(node_t *self, struct gui_renderer *r);
    window_node_draw(self, r);
    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int screen_x = camera_world_to_screen_x(cam, pt.x);
    int screen_y = camera_world_to_screen_y(cam, pt.y);
    int screen_w = camera_scale(cam, self->width);
    int screen_h = camera_scale(cam, self->height);

    self->screen_bounds = rect_make(screen_x, screen_y, screen_w, screen_h);

    int top_margin = camera_scale(cam, 24); /* Leave space for watermark title */

    /* Terminal background — deep blue/black to blend with glass */
    gui_rect_t content = rect_make(screen_x, screen_y + top_margin,
                                    screen_w, screen_h - top_margin);
    renderer_fill_rect(r, content, 0xAA0A0A15); /* Slightly translucent */

    /* --- Render all cells --- */
    if (!t->font) return;

    /* Compute how many chars fit in the content area */
    int avail_w = screen_w - GTERM_PAD_X * 2;
    int avail_h = screen_h - top_margin - GTERM_PAD_Y * 2;
    int cols_vis = avail_w / GTERM_CHAR_W;
    int rows_vis = avail_h / GTERM_CHAR_H;
    if (cols_vis > GTERM_COLS) cols_vis = GTERM_COLS;
    if (rows_vis > GTERM_ROWS) rows_vis = GTERM_ROWS;

    int base_x = screen_x + GTERM_PAD_X;
    int base_y = screen_y + top_margin + GTERM_PAD_Y;

    for (int row = 0; row < rows_vis; row++) {
        for (int col = 0; col < cols_vis; col++) {
            char ch = t->cells[row][col].ch;
            uint32_t fg = t->cells[row][col].fg;
            if (ch < 32 || ch > 126) ch = ' ';
            int px = base_x + col * GTERM_CHAR_W;
            int py = base_y + row * GTERM_CHAR_H;
            renderer_draw_glyph(r, px, py, fg, 0x00000000, &t->font[(unsigned char)ch]);
        }
    }

    /* Blinking cursor — draw a block on the current cell */
    if (t->cur_row < rows_vis && t->cur_col < cols_vis) {
        int cx = base_x + t->cur_col * GTERM_CHAR_W;
        int cy = base_y + t->cur_row * GTERM_CHAR_H;
        renderer_fill_rect(r, rect_make(cx, cy, GTERM_CHAR_W, 2), 0xFFDDDDDD);
    }
}

/* --------------------------------------------------------------------------
 * Combined vtable — reuses window chrome but overrides draw and destroy
 * -------------------------------------------------------------------------- */

static void gterm_node_destroy(node_t *self) {
    if (self->userdata) {
        kfree(self->userdata);
        self->userdata = NULL;
    }
}

/* We borrow window_on_event from window_node by linking it here */
extern bool window_node_on_event(node_t *self, const gui_event_t *e);

static const node_vtable_t gterm_vtable = {
    .draw     = gterm_draw,
    .on_event = window_node_on_event,   /* drag, resize, close all still work */
    .layout   = NULL,
    .destroy  = gterm_node_destroy,
};

/* --------------------------------------------------------------------------
 * Public constructor
 * -------------------------------------------------------------------------- */

node_t *gui_terminal_create(const char *win_name, int x, int y, int w, int h) {
    /* Create using window_node_create to get the titlebar chrome */
    node_t *win = window_node_create(win_name, x, y, w, h, "Terminal");
    if (!win) return NULL;

    /* Allocate terminal state */
    gui_terminal_t *t = (gui_terminal_t *)kmalloc(sizeof(gui_terminal_t));
    if (!t) return win; /* fallback to plain window */
    memset(t, 0, sizeof(gui_terminal_t));
    t->win_node = win;
    t->font = asset_manager_get_font(NULL);

    /* Initialize cells to spaces */
    for (int r = 0; r < GTERM_ROWS; r++) {
        for (int c = 0; c < GTERM_COLS; c++) {
            t->cells[r][c].ch = ' ';
            t->cells[r][c].fg = 0xFFCCCCCC;
        }
    }

    /* Print welcome banner */
    gterm_puts(t, "LiwusOS Terminal v1.0\n", 0xFF00FFFF);
    gterm_puts(t, "Type 'help' for available commands.\n\n", 0xFF888888);
    gterm_print_prompt(t);

    /* Override vtable for custom drawing */
    win->vtable = &gterm_vtable;

    /* Install key handlers */
    window_node_set_key_handler(win, gterm_key_down, gterm_key_char, t);

    /*
     * We need to store gui_terminal_t (t) somewhere so gterm_draw can find it.
     * Since window_node already populated self->userdata with window_node_data_t,
     * we will keep a static array of active terminals. For a single terminal app,
     * a single global pointer is enough.
     */
    s_active_terminal = t;

    return win;
}
