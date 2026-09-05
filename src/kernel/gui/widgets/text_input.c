/*
 * gui/widgets/text_input.c
 * Simple multi-line text input widget.
 */
#include "text_input.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "kheap.h"
#include "string.h"
#include "../assets/asset_manager.h"
#include "../core/theme_engine.h"
#include "../core/animation_engine.h"

typedef struct {
    char *text;
    uint32_t capacity;
    uint32_t cursor_pos;
    bool focused;
    text_input_change_cb_t on_change;
    void *change_ud;
    const glyph_t *font;
    uint32_t current_bg_color;
    uint32_t scroll_offset;
} text_input_data_t;

static void text_input_draw(node_t *self, struct gui_renderer *r) {
    text_input_data_t *d = (text_input_data_t *)self->userdata;
    if (!d) return;

    if (!d->font) {
        d->font = asset_manager_get_font(NULL);
    }

    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int screen_x = camera_world_to_screen_x(cam, pt.x);
    int screen_y = camera_world_to_screen_y(cam, pt.y);
    int screen_w = camera_scale(cam, self->width);
    int screen_h = camera_scale(cam, self->height);
    
    self->screen_bounds = rect_make(screen_x, screen_y, screen_w, screen_h);

    /* Solid background — CRT style */
    uint32_t bg = d->focused ? theme_engine_get_color(THEME_COLOR_INPUT_BG_FOCUS) : theme_engine_get_color(THEME_COLOR_INPUT_BG);
    renderer_fill_rect(r, self->screen_bounds, bg | 0xFF000000);
    renderer_draw_rect(r, self->screen_bounds,
        d->focused ? theme_engine_get_color(THEME_COLOR_INPUT_BORDER) : theme_engine_get_color(THEME_COLOR_BUTTON_BG_PRESS), 1);

    if (d->text) {
        int char_w = 8;
        int char_h = 16;
        int start_x = screen_x + 4;
        int start_y = screen_y + 4;
        int max_chars_per_line = (screen_w - 8) / char_w;
        int max_lines = (screen_h - 8) / char_h;

        const char *p = d->text;
        int line = 0;
        int col = 0;
        int cx = start_x;
        int cy = start_y + d->scroll_offset;

        while (*p && line < max_lines) {
            if (*p == '\n') {
                line++;
                col = 0;
                cx = start_x;
                cy += char_h;
                p++;
                continue;
            }
            if (col >= max_chars_per_line) {
                col = 0;
                line++;
                cx = start_x;
                cy += char_h;
                continue;
            }
            if (cy >= start_y && cy < start_y + screen_h - char_h) {
                renderer_draw_glyph(r, cx, cy, theme_engine_get_color(THEME_COLOR_INPUT_TEXT), 0x00000000, &d->font[(unsigned char)*p]);
            }
            cx += char_w;
            col++;
            p++;
        }

        /* CRT block cursor — full character cell, not a thin line */
        if (d->focused) {
            int cursor_x = start_x + (d->cursor_pos % max_chars_per_line) * char_w;
            int cursor_y = start_y + (d->cursor_pos / max_chars_per_line) * char_h + d->scroll_offset;
            if (cursor_y >= start_y && cursor_y < start_y + screen_h) {
                /* Draw solid block cursor (8x16) */
                gui_rect_t cursor_rect = rect_make(cursor_x, cursor_y, char_w, char_h);
                renderer_fill_rect(r, cursor_rect, theme_engine_get_color(THEME_COLOR_INPUT_CURSOR));
            }
        }
    }
}

static bool text_input_on_event(node_t *self, const gui_event_t *e) {
    text_input_data_t *d = (text_input_data_t *)self->userdata;
    if (!d) return false;

    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        bool inside = rect_contains_point(self->screen_bounds, e->mouse.x, e->mouse.y);
        if (inside != d->focused) {
            d->focused = inside;
            node_mark_dirty(self, NODE_DIRTY_PAINT);
        }
        if (inside) {
            extern void focus_manager_set_focus(void *, node_t *);
            extern void *g_focus_manager;
            focus_manager_set_focus(g_focus_manager, self);
        }
        return inside;
    } else if (e->type == GUI_EVENT_KEY_CHAR && d->focused) {
        if (!d->text) return true;
        
        uint32_t len = strlen(d->text);
        uint32_t unicode = e->key.unicode;
        
        if (unicode == '\b' || unicode == 127) {
            if (d->cursor_pos > 0) {
                memmove(d->text + d->cursor_pos - 1, d->text + d->cursor_pos, len - d->cursor_pos + 1);
                d->cursor_pos--;
                d->text[len - 1] = '\0';
                if (d->on_change) d->on_change(self, d->change_ud);
                node_mark_dirty(self, NODE_DIRTY_PAINT);
            }
        } else if (unicode == '\n' || unicode == '\r') {
            if (len + 1 < d->capacity) {
                memmove(d->text + d->cursor_pos + 1, d->text + d->cursor_pos, len - d->cursor_pos + 1);
                d->text[d->cursor_pos] = '\n';
                d->cursor_pos++;
                if (d->on_change) d->on_change(self, d->change_ud);
                node_mark_dirty(self, NODE_DIRTY_PAINT);
            }
        } else if (unicode >= 32 && unicode <= 126) {
            if (len + 1 < d->capacity) {
                memmove(d->text + d->cursor_pos + 1, d->text + d->cursor_pos, len - d->cursor_pos + 1);
                d->text[d->cursor_pos] = (char)unicode;
                d->cursor_pos++;
                if (d->on_change) d->on_change(self, d->change_ud);
                node_mark_dirty(self, NODE_DIRTY_PAINT);
            }
        }
        return true;
    } else if (e->type == GUI_EVENT_KEY_DOWN && d->focused) {
        if (!d->text) return true;
        
        uint32_t len = strlen(d->text);
        uint32_t keycode = e->key.keycode;
        
        if (keycode == 0x48) { // Up arrow
            int line_start = d->cursor_pos;
            while (line_start > 0 && d->text[line_start - 1] != '\n') line_start--;
            if (line_start > 0) {
                int prev_line_start = line_start - 2;
                while (prev_line_start >= 0 && d->text[prev_line_start] != '\n') prev_line_start--;
                int col = d->cursor_pos - line_start;
                d->cursor_pos = (prev_line_start + 1) + col;
                if (d->cursor_pos > len) d->cursor_pos = len;
                node_mark_dirty(self, NODE_DIRTY_PAINT);
            }
        } else if (keycode == 0x50) { // Down arrow
            int line_start = d->cursor_pos;
            while (line_start > 0 && d->text[line_start - 1] != '\n') line_start--;
            int line_end = d->cursor_pos;
            while (line_end < len && d->text[line_end] != '\n') line_end++;
            if (line_end < len) {
                int next_line_start = line_end + 1;
                int next_line_end = next_line_start;
                while (next_line_end < len && d->text[next_line_end] != '\n') next_line_end++;
                int col = d->cursor_pos - line_start;
                d->cursor_pos = next_line_start + col;
                if (d->cursor_pos > next_line_end) d->cursor_pos = next_line_end;
                if (d->cursor_pos > len) d->cursor_pos = len;
                node_mark_dirty(self, NODE_DIRTY_PAINT);
            }
        } else if (keycode == 0x4B) { // Left arrow
            if (d->cursor_pos > 0) {
                d->cursor_pos--;
                node_mark_dirty(self, NODE_DIRTY_PAINT);
            }
        } else if (keycode == 0x4D) { // Right arrow
            if (d->cursor_pos < len) {
                d->cursor_pos++;
                node_mark_dirty(self, NODE_DIRTY_PAINT);
            }
        }
        return true;
    }
    return false;
}

static void text_input_destroy(node_t *self) {
    if (self->userdata) {
        text_input_data_t *d = (text_input_data_t *)self->userdata;
        if (d->text) kfree(d->text);
        kfree(d);
        self->userdata = NULL;
    }
}

static const node_vtable_t text_input_vtable = {
    .draw     = text_input_draw,
    .on_event = text_input_on_event,
    .layout   = NULL,
    .destroy  = text_input_destroy,
};

node_t *text_input_create(const char *name, int x, int y, int w, int h, const char *initial_text) {
    node_t *n = node_create(NODE_PANEL, name);
    if (!n) return NULL;

    text_input_data_t *d = (text_input_data_t *)kmalloc(sizeof(text_input_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(text_input_data_t));

    d->capacity = EDITOR_MAX_FILE_SIZE;
    d->text = (char *)kmalloc(d->capacity);
    if (initial_text) {
        strncpy(d->text, initial_text, d->capacity - 1);
        d->text[d->capacity - 1] = '\0';
        d->cursor_pos = strlen(d->text);
    } else {
        d->text[0] = '\0';
    }
    d->current_bg_color = theme_engine_get_color(THEME_COLOR_INPUT_BG);

    n->userdata = d;
    n->vtable = &text_input_vtable;
    n->local_x = x;
    n->local_y = y;
    n->width = w;
    n->height = h;
    n->interactive = true;

    return n;
}

void text_input_set_text(node_t *input, const char *text) {
    if (input && input->type == NODE_PANEL && input->vtable == &text_input_vtable) {
        text_input_data_t *d = (text_input_data_t *)input->userdata;
        if (d->text) {
            strncpy(d->text, text ? text : "", d->capacity - 1);
            d->text[d->capacity - 1] = '\0';
            d->cursor_pos = strlen(d->text);
            node_mark_dirty(input, NODE_DIRTY_PAINT);
        }
    }
}

const char *text_input_get_text(node_t *input) {
    if (input && input->type == NODE_PANEL && input->vtable == &text_input_vtable) {
        text_input_data_t *d = (text_input_data_t *)input->userdata;
        return d->text;
    }
    return "";
}

void text_input_set_on_change(node_t *input, text_input_change_cb_t cb, void *userdata) {
    if (input && input->type == NODE_PANEL && input->vtable == &text_input_vtable) {
        text_input_data_t *d = (text_input_data_t *)input->userdata;
        d->on_change = cb;
        d->change_ud = userdata;
    }
}

void text_input_set_cursor_pos(node_t *input, uint32_t pos) {
    if (input && input->type == NODE_PANEL && input->vtable == &text_input_vtable) {
        text_input_data_t *d = (text_input_data_t *)input->userdata;
        uint32_t len = strlen(d->text);
        if (pos > len) pos = len;
        d->cursor_pos = pos;
        node_mark_dirty(input, NODE_DIRTY_PAINT);
    }
}

void text_input_focus(node_t *input) {
    if (input && input->type == NODE_PANEL && input->vtable == &text_input_vtable) {
        text_input_data_t *d = (text_input_data_t *)input->userdata;
        d->focused = true;
        extern void focus_manager_set_focus(void *, node_t *);
        extern void *g_focus_manager;
        focus_manager_set_focus(g_focus_manager, input);
        node_mark_dirty(input, NODE_DIRTY_PAINT);
    }
}

void text_input_type_char(node_t *input, uint32_t unicode) {
    if (!input || input->type != NODE_PANEL || input->vtable != &text_input_vtable) return;
    text_input_data_t *d = (text_input_data_t *)input->userdata;
    if (!d->text) return;
    d->focused = true;
    uint32_t len = strlen(d->text);

    if (unicode == '\b' || unicode == 127) {
        if (d->cursor_pos > 0) {
            memmove(d->text + d->cursor_pos - 1, d->text + d->cursor_pos, len - d->cursor_pos + 1);
            d->cursor_pos--;
            d->text[len - 1] = '\0';
            if (d->on_change) d->on_change(input, d->change_ud);
            node_mark_dirty(input, NODE_DIRTY_PAINT);
        }
    } else if (unicode == '\n' || unicode == '\r') {
        if (len + 1 < d->capacity) {
            memmove(d->text + d->cursor_pos + 1, d->text + d->cursor_pos, len - d->cursor_pos + 1);
            d->text[d->cursor_pos] = '\n';
            d->cursor_pos++;
            if (d->on_change) d->on_change(input, d->change_ud);
            node_mark_dirty(input, NODE_DIRTY_PAINT);
        }
    } else if (unicode >= 32 && unicode <= 126) {
        if (len + 1 < d->capacity) {
            memmove(d->text + d->cursor_pos + 1, d->text + d->cursor_pos, len - d->cursor_pos + 1);
            d->text[d->cursor_pos] = (char)unicode;
            d->cursor_pos++;
            if (d->on_change) d->on_change(input, d->change_ud);
            node_mark_dirty(input, NODE_DIRTY_PAINT);
        }
    }
}