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
#include "vfs.h"
#include "task.h"
#include "vga.h"

/* Forward declarations from terminal subsystem */
extern int terminal_parse_line(char *line, char **argv);
extern void terminal_execute(int argc, char **argv);

/* Forward declaration */
extern gui_event_bus_t   *g_event_bus;
extern focus_manager_t   *g_focus_manager;

/* ---- Terminal state ---- */

#define GTERM_MAX_ARGS 16
#define GTERM_HISTORY_MAX 32

typedef struct gui_terminal {
    gterm_cell_t  cells[GTERM_ROWS][GTERM_COLS];
    int           cur_row;      /* current write row */
    int           cur_col;      /* current write column */

    char          input_line[GTERM_COLS + 1];
    int           input_len;

    /* Command history */
    char          history[GTERM_HISTORY_MAX][GTERM_COLS + 1];
    int           hist_count;
    int           hist_index;   /* == hist_count when at a fresh prompt */
    char          hist_draft[GTERM_COLS + 1]; /* saved input when first pressing up */

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
            t->cells[GTERM_ROWS - 1][c].fg = 0xFFBFC6CF;
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

static void gterm_print_prompt(gui_terminal_t *t); /* forward */

/* --------------------------------------------------------------------------
 * Helpers: erase / replace input line
 * -------------------------------------------------------------------------- */

#define GTERM_BG 0xFF1E2229
#define GTERM_FG_INPUT 0xFFE6E8EB
#define GTERM_FG_DIM  0xFF99A1AF

/* Erase the currently displayed input text from the cell buffer.
 * Walks backwards from cur_col input_len times, blanking each cell. */
static void gterm_erase_input(gui_terminal_t *t) {
    int len = t->input_len;
    for (int i = 0; i < len; i++) {
        if (t->cur_col == 0) {
            if (t->cur_row > 0) {
                t->cur_row--;
                t->cur_col = GTERM_COLS - 1;
            } else {
                t->cur_col = 0;
                break;
            }
        } else {
            t->cur_col--;
        }
        t->cells[t->cur_row][t->cur_col].ch = ' ';
        t->cells[t->cur_row][t->cur_col].fg = GTERM_BG;
    }
}

/* Replace the current input with a new string (redraws from cursor). */
static void gterm_set_input(gui_terminal_t *t, const char *s) {
    gterm_erase_input(t);
    t->input_len = 0;
    while (*s && t->input_len < GTERM_COLS - 1) {
        t->input_line[t->input_len++] = *s;
        gterm_putchar(t, *s, GTERM_FG_INPUT);
        s++;
    }
    t->input_line[t->input_len] = '\0';
    node_mark_dirty(t->win_node, NODE_DIRTY_PAINT);
}

/* Join base + "/" + rel into out. If rel is absolute, use it as-is. */
static void gterm_join(const char *base, const char *rel, char *out, size_t n) {
    if (rel[0] == '/') {
        strncpy(out, rel, n - 1);
    } else {
        strncpy(out, base, n - 1);
        size_t bl = strlen(out);
        if (bl > 0 && out[bl - 1] != '/' && bl < n - 2) { out[bl] = '/'; bl++; }
        strncpy(out + bl, rel, n - 1 - bl);
    }
    out[n - 1] = '\0';
}

/* --------------------------------------------------------------------------
 * TAB completion
 * -------------------------------------------------------------------------- */

static void gterm_complete(gui_terminal_t *t) {
    const char *line = t->input_line;
    int len = t->input_len;

    /* Find the start of the last token */
    int tok_start = len;
    while (tok_start > 0 && line[tok_start - 1] != ' ') tok_start--;
    if (tok_start == 0) return;   /* only a command word — nothing to complete */

    /* Copy the token being completed */
    char token[GTERM_COLS];
    int tlen = len - tok_start;
    memcpy(token, line + tok_start, (size_t)tlen);
    token[tlen] = '\0';

    /* Strip leading quote for matching */
    int q = (token[0] == '"') ? 1 : 0;
    const char *prefix = token + q;
    int plen = (int)strlen(prefix);

    /* Split at last '/' inside the prefix to get dir + name */
    char dir_part[GTERM_COLS];
    const char *name_part = prefix;
    const char *slash = strrchr(prefix, '/');
    if (slash) {
        int dl = (int)(slash - prefix);
        memcpy(dir_part, prefix, (size_t)dl);
        dir_part[dl] = '\0';
        name_part = slash + 1;
    } else {
        dir_part[0] = '\0';
    }

    /* Resolve the directory to list */
    char dir_path[256];
    if (dir_part[0] == '\0') {
        strncpy(dir_path, current_task ? current_task->cwd : "/", sizeof(dir_path) - 1);
    } else if (dir_part[0] == '/') {
        strncpy(dir_path, dir_part, sizeof(dir_path) - 1);
    } else {
        gterm_join(current_task ? current_task->cwd : "/", dir_part, dir_path, sizeof(dir_path));
    }
    dir_path[sizeof(dir_path) - 1] = '\0';

    fs_node_t *d = vfs_open(dir_path);
    if (!d || !(d->flags & FS_DIRECTORY)) return;

    /* Collect matching entries */
    struct dirent *entry;
    uint32_t idx = 0;
    int matches = 0;
    static char first_name[256];

    while ((entry = readdir_fs(d, idx)) != 0) {
        idx++;
        if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) continue;
        if (strncmp(entry->name, name_part, (size_t)plen) == 0) {
            if (matches == 0) strncpy(first_name, entry->name, sizeof(first_name) - 1);
            matches++;
        }
    }

    if (matches == 0) return;

    /* Multiple matches — print them like bash, then redraw prompt + input */
    if (matches > 1) {
        idx = 0;
        while ((entry = readdir_fs(d, idx)) != 0) {
            idx++;
            if (strcmp(entry->name, ".") == 0 || strcmp(entry->name, "..") == 0) continue;
            if (strncmp(entry->name, name_part, (size_t)plen) == 0) {
                gterm_puts(t, entry->name, GTERM_FG_DIM);
                gterm_puts(t, "  ", GTERM_FG_DIM);
            }
        }
        gterm_puts(t, "\n", GTERM_FG_DIM);
        gterm_print_prompt(t);
        gterm_puts(t, line, GTERM_FG_INPUT);
        node_mark_dirty(t->win_node, NODE_DIRTY_PAINT);
        return;
    }

    /* Unique match — build the completed token */
    int nlen = (int)strlen(first_name);

    /* Is it a directory? */
    bool is_dir = false;
    {
        char probe[256];
        gterm_join(dir_path, first_name, probe, sizeof(probe));
        fs_node_t *pf = vfs_open(probe);
        if (pf && (pf->flags & FS_DIRECTORY)) is_dir = true;
    }

    /* Decide whether to add quoting (name has a space and user didn't type one) */
    bool has_space = false;
    for (int k = 0; k < nlen; k++) { if (first_name[k] == ' ') { has_space = true; break; } }

    /* Build the full replacement token.
     * If the name has a space (or the user already opened a quote), wrap it
     * in quotes so the parser sees it as a single argument. */
    char comp[GTERM_COLS];
    int cn = 0;
    if (q || has_space)  comp[cn++] = '"';   /* opening quote */
    for (int k = 0; k < nlen && cn < GTERM_COLS - 4; k++) {
        comp[cn++] = first_name[k];
    }
    if (q || has_space)  comp[cn++] = '"';   /* closing quote */
    if (is_dir && cn < GTERM_COLS - 2) comp[cn++] = '/';
    comp[cn] = '\0';

    /* Build the full new input line */
    char newline[GTERM_COLS + 1];
    memcpy(newline, line, (size_t)tok_start);
    newline[tok_start] = '\0';
    strncat(newline, comp, sizeof(newline) - strlen(newline) - 1);

    gterm_erase_input(t);
    t->input_len = (int)strlen(newline);
    memcpy(t->input_line, newline, (size_t)(t->input_len + 1));
    gterm_puts(t, newline, GTERM_FG_INPUT);
    node_mark_dirty(t->win_node, NODE_DIRTY_PAINT);
}

/* --------------------------------------------------------------------------
 * VGA output hook — called by vga_puts when installed
 * -------------------------------------------------------------------------- */

static void gterm_vga_hook(const char *str) {
    if (!s_active_terminal) return;
    gterm_puts(s_active_terminal, str, 0xFFC9CED6); /* Light gray for output */
}

/* --------------------------------------------------------------------------
 * Print prompt
 * -------------------------------------------------------------------------- */

static void gterm_print_prompt(gui_terminal_t *t) {
    gterm_puts(t, "root@liwusos:", 0xFF4A9EFF); /* Blue prompt */

    const char *cwd = current_task ? current_task->cwd : "/";
    /* Show the full path, truncating the head when it gets too long. */
    int clen = (int)strlen(cwd);
    if (clen > 40) {
        gterm_puts(t, "...", 0xFF4A9EFF);
        gterm_puts(t, cwd + (clen - 37), 0xFF4A9EFF);
    } else {
        gterm_puts(t, cwd, 0xFF4A9EFF);
    }

    gterm_puts(t, "# ", 0xFF4A9EFF);
}

/* --------------------------------------------------------------------------
 * Execute a command and capture its output
 * -------------------------------------------------------------------------- */

static void gterm_execute(gui_terminal_t *t) {
    /* Echo the entered command in white */
    gterm_puts(t, "\n", 0xFFFFFFFF);

    /* ---- Save to command history ---- */
    if (t->input_len > 0) {
        if (t->hist_count == 0 ||
            strcmp(t->history[t->hist_count - 1], t->input_line) != 0)
        {
            if (t->hist_count == GTERM_HISTORY_MAX) {
                /* Drop oldest entry */
                for (int i = 1; i < GTERM_HISTORY_MAX; i++)
                    strcpy(t->history[i - 1], t->history[i]);
                t->hist_count--;
            }
            strncpy(t->history[t->hist_count], t->input_line, GTERM_COLS);
            t->history[t->hist_count][GTERM_COLS] = '\0';
            t->hist_count++;
        }
    }
    t->hist_index = t->hist_count;
    t->hist_draft[0] = '\0';

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
                        t->cells[r][c].fg = 0xFFC9CED6;
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

    /* Up arrow — previous command */
    if (sc == 0x48) {
        if (t->hist_count == 0) return true;
        if (t->hist_index == t->hist_count) {
            /* First time pressing up: save what was being typed */
            strncpy(t->hist_draft, t->input_line, GTERM_COLS);
            t->hist_draft[GTERM_COLS] = '\0';
            t->hist_index = t->hist_count - 1;
        } else if (t->hist_index > 0) {
            t->hist_index--;
        }
        gterm_set_input(t, t->history[t->hist_index]);
        return true;
    }

    /* Down arrow — next command */
    if (sc == 0x50) {
        if (t->hist_index == t->hist_count) return true;
        t->hist_index++;
        if (t->hist_index == t->hist_count)
            gterm_set_input(t, t->hist_draft);
        else
            gterm_set_input(t, t->history[t->hist_index]);
        return true;
    }

    /* Tab — autocompletion */
    if (sc == 0x0F) {
        gterm_complete(t);
        return true;
    }

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
        gterm_putchar(t, c, 0xFFE6E8EB); /* Near-white for user input */
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

    /* Terminal background — solid dark green-black for CRT look */
    gui_rect_t content = rect_make(screen_x, screen_y + top_margin,
                                    screen_w, screen_h - top_margin);
    renderer_fill_rect(r, content, 0xFF1E2229); /* Solid dark background */

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

        /* Blinking cursor — CRT block cursor */
        if (t->cur_row < rows_vis && t->cur_col < cols_vis) {
            int cx = base_x + t->cur_col * GTERM_CHAR_W;
            int cy = base_y + t->cur_row * GTERM_CHAR_H;
            renderer_fill_rect(r, rect_make(cx, cy, GTERM_CHAR_W, GTERM_CHAR_H), 0xFF4A9EFF);
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
            t->cells[r][c].fg = 0xFFC9CED6;
        }
    }

    /* Print welcome banner */
    gterm_puts(t, "LiwusOS Terminal v1.0\n", 0xFFE6E8EB);
    gterm_puts(t, "Type 'help' for available commands.\n\n", 0xFF99A1AF);
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
