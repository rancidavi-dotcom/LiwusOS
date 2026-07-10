/*
 * gui/widgets/window_node.c
 */
#include "window_node.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "kheap.h"
#include "string.h"
#include "../assets/asset_manager.h"
#include "../core/theme_engine.h"
typedef struct {
    char    *title;
    const glyph_t *font;
    int      process_id;
    
    bool     dragging;
    int      drag_start_mx;
    int      drag_start_my;
    int      drag_start_win_x;
    int      drag_start_win_y;
    bool     focused;
} window_node_data_t;

static void window_draw(node_t *self, struct gui_renderer *r) {
    window_node_data_t *d = (window_node_data_t *)self->userdata;
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

    /* Title bar height */
    int title_h = camera_scale(cam, 24);

    /* Background */
    gui_rect_t bg_rect = rect_make(screen_x, screen_y + title_h, screen_w, screen_h - title_h);
    renderer_fill_rect(r, bg_rect, theme_engine_get_color(THEME_COLOR_WINDOW_BG));

    /* Title Bar */
    gui_rect_t title_rect = rect_make(screen_x, screen_y, screen_w, title_h);
    renderer_fill_rect(r, title_rect, theme_engine_get_color(THEME_COLOR_WINDOW_TITLEBAR));

    /* Coloured traffic light (Close only) */
    int dot_s = camera_scale(cam, 12);
    int dot_y = screen_y + camera_scale(cam, 6);
    renderer_fill_rect(r, rect_make(screen_x + camera_scale(cam, 8),  dot_y, dot_s, dot_s), theme_engine_get_color(THEME_COLOR_CLOSE_BTN));

    /* Title Text */
    if (d->title) {
        int text_len = strlen(d->title);
        int text_w = text_len * 8;
        int text_h = 16;
        int cx = screen_x + (screen_w - text_w) / 2;
        int cy = screen_y + (title_h - text_h) / 2;

        for (int i = 0; i < text_len; i++) {
            unsigned char c = d->title[i];
            renderer_draw_glyph(r, cx, cy, theme_engine_get_color(THEME_COLOR_TEXT_SECONDARY), 0x00000000, &d->font[c]);
            cx += 8;
        }
    }

    /* Outer border */
    gui_rect_t outer = rect_make(screen_x - 1, screen_y - 1, screen_w + 2, screen_h + 2);
    if (d->focused) {
        renderer_draw_rect(r, outer, 0xFFFFFFFF, 2); // Bright white, 2px thick
    } else {
        renderer_draw_rect(r, outer, theme_engine_get_color(THEME_COLOR_WINDOW_BORDER), 1);
    }
}

static void window_destroy(node_t *self) {
    if (self->userdata) {
        window_node_data_t *d = (window_node_data_t *)self->userdata;
        if (d->title) kfree(d->title);
        kfree(d);
        self->userdata = NULL;
    }
}

static bool window_on_event(node_t *self, const gui_event_t *e) {
    window_node_data_t *d = (window_node_data_t *)self->userdata;
    if (!d) return false;

    if (e->type == GUI_EVENT_WIN_FOCUS) {
        d->focused = true;
        node_mark_dirty(self, NODE_DIRTY_PAINT);
        return false;
    }
    
    if (e->type == GUI_EVENT_WIN_BLUR) {
        d->focused = false;
        node_mark_dirty(self, NODE_DIRTY_PAINT);
        return false;
    }

    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        extern compositor_t *g_compositor;
        if (!g_compositor) return false;
        camera_t *cam = g_compositor->camera;

        gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
        int screen_x = camera_world_to_screen_x(cam, pt.x);
        int screen_y = camera_world_to_screen_y(cam, pt.y);

        int dot_s = camera_scale(cam, 12);
        int dot_x = screen_x + camera_scale(cam, 8);
        int dot_y = screen_y + camera_scale(cam, 6);
        gui_rect_t dot_rect = rect_make(dot_x, dot_y, dot_s, dot_s);

        if (rect_contains_point(dot_rect, e->mouse.x, e->mouse.y)) {
            // Post window close event
            extern gui_event_bus_t *g_event_bus;
            if (g_event_bus) {
                gui_event_t close_ev;
                memset(&close_ev, 0, sizeof(close_ev));
                close_ev.type = GUI_EVENT_WIN_CLOSE;
                close_ev.generic.a = (uint64_t)self;
                event_bus_post(g_event_bus, &close_ev);
            }
            return true;
        }

        /* Title bar drag detection */
        int title_h = camera_scale(cam, 24);
        int screen_w = camera_scale(cam, self->width);
        gui_rect_t title_rect = rect_make(screen_x, screen_y, screen_w, title_h);
        if (rect_contains_point(title_rect, e->mouse.x, e->mouse.y)) {
            d->dragging = true;
            d->drag_start_mx = e->mouse.x;
            d->drag_start_my = e->mouse.y;
            d->drag_start_win_x = self->local_x;
            d->drag_start_win_y = self->local_y;
            return true; /* consume */
        }
    } else if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        if (d->dragging) {
            d->dragging = false;
            return true;
        }
    } else if (e->type == GUI_EVENT_MOUSE_MOVE) {
        if (d->dragging) {
            extern compositor_t *g_compositor;
            if (g_compositor) {
                camera_t *cam = g_compositor->camera;
                int ddx = e->mouse.x - d->drag_start_mx;
                int ddy = e->mouse.y - d->drag_start_my;
                int world_dx = (int)((int64_t)ddx * CAMERA_ZOOM_SCALE / cam->zoom_fp);
                int world_dy = (int)((int64_t)ddy * CAMERA_ZOOM_SCALE / cam->zoom_fp);
                
                node_set_position(self, d->drag_start_win_x + world_dx, d->drag_start_win_y + world_dy);
            }
            return true;
        }
    }
    return false;
}

static const node_vtable_t window_vtable = {
    .draw     = window_draw,
    .on_event = window_on_event,
    .layout   = NULL,
    .destroy  = window_destroy,
};

node_t *window_node_create(const char *name, int x, int y, int w, int h, const char *title) {
    node_t *n = node_create(NODE_WINDOW, name);
    if (!n) return NULL;

    window_node_data_t *d = (window_node_data_t *)kmalloc(sizeof(window_node_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(window_node_data_t));

    if (title) {
        d->title = (char *)kmalloc(strlen(title) + 1);
        strcpy(d->title, title);
    }
    
    n->userdata = d;
    n->vtable = &window_vtable;
    n->local_x = x;
    n->local_y = y;
    n->width = w;
    n->height = h;
    n->interactive = true;

    return n;
}

void window_node_set_title(node_t *win, const char *title) {
    if (win && win->type == NODE_WINDOW) {
        window_node_data_t *d = (window_node_data_t *)win->userdata;
        if (d->title) kfree(d->title);
        if (title) {
            d->title = (char *)kmalloc(strlen(title) + 1);
            strcpy(d->title, title);
        } else {
            d->title = NULL;
        }
        node_mark_dirty(win, NODE_DIRTY_PAINT);
    }
}

void window_node_set_pid(node_t *win, int pid) {
    if (win && win->type == NODE_WINDOW) {
        window_node_data_t *d = (window_node_data_t *)win->userdata;
        if (d) d->process_id = pid;
    }
}
