/*
 * gui/widgets/window_node.c
 */
#include "window_node.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "../scene/camera.h"
#include "../assets/pixel_controls.h"
#include "kheap.h"
#include "string.h"
#include "../assets/asset_manager.h"
#include "../core/theme_engine.h"
#include "../window/focus_manager.h"
#include "../window/window_manager.h"
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
    bool     close_pressed;
    bool     min_pressed;
    bool     max_pressed;
    bool     close_hover;
    bool     min_hover;
    bool     max_hover;

    /* Minimize / Maximize state */
    bool     minimized;
    bool     maximized;
    int      saved_x;
    int      saved_y;
    int      saved_w;
    int      saved_h;

    /* Optional key handlers set by the application */
    bool (*key_down_cb)(node_t *self, uint8_t scancode, void *userctx);
    bool (*key_char_cb)(node_t *self, char c, void *userctx);
    void *key_userctx;
} window_node_data_t;

void window_node_draw(node_t *self, struct gui_renderer *r) {
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

    /* Window body — solid silver gray */
    gui_rect_t bg_rect = rect_make(screen_x, screen_y + title_h, screen_w, screen_h - title_h);
    renderer_fill_rect(r, bg_rect, theme_engine_get_color(THEME_COLOR_WINDOW_BG));

    /* Title Bar — navy blue */
    gui_rect_t title_rect = rect_make(screen_x, screen_y, screen_w, title_h);
    renderer_fill_rect(r, title_rect, theme_engine_get_color(THEME_COLOR_WINDOW_TITLEBAR));

    /* Title Text — white, centered */
    if (d->title) {
        int text_len = strlen(d->title);
        int text_w = text_len * 8;
        int text_h = 16;
        int cx = screen_x + (screen_w - text_w) / 2;
        int cy = screen_y + (title_h - text_h) / 2;

        for (int i = 0; i < text_len; i++) {
            unsigned char c = d->title[i];
            renderer_draw_glyph(r, cx, cy, 0xFFFFFFFF, 0x00000000, &d->font[c]);
            cx += 8;
        }
    }

    /* Window buttons: minimize, maximize, close — all 18x18 Chicago95 sprites (fixed screen pixels) */
    {
        int btn_w = 18;
        int btn_h = 18;
        int by = screen_y + 3;
        int bx = screen_x + screen_w - btn_w - 3;  // close

        // Close
        const pixel_control_t *cbtn = &pixel_controls[d->close_pressed
                                                         ? CONTROL_WIN_CLOSE_PRESSED
                                                         : CONTROL_WIN_CLOSE];
        renderer_blit(r, bx, by, cbtn->data, cbtn->w, cbtn->h, cbtn->w,
                      0, 0, cbtn->w, cbtn->h);

        // Maximize (middle)
        bx -= btn_w + 2;
        const pixel_control_t *mbtn = &pixel_controls[d->max_pressed
                                                         ? CONTROL_WIN_MAX_PRESSED
                                                         : CONTROL_WIN_MAX];
        renderer_blit(r, bx, by, mbtn->data, mbtn->w, mbtn->h, mbtn->w,
                      0, 0, mbtn->w, mbtn->h);

        // Minimize (leftmost)
        bx -= btn_w + 2;
        const pixel_control_t *nbtn = &pixel_controls[d->min_pressed
                                                         ? CONTROL_WIN_MIN_PRESSED
                                                         : CONTROL_WIN_MIN];
        renderer_blit(r, bx, by, nbtn->data, nbtn->w, nbtn->h, nbtn->w,
                      0, 0, nbtn->w, nbtn->h);
    }

    /* 3D beveled frame: white top/left, gray shadow bottom/right */
    renderer_fill_rect(r, rect_make(screen_x, screen_y, screen_w, 1), 0xFFFFFFFF);
    renderer_fill_rect(r, rect_make(screen_x, screen_y, 1, screen_h), 0xFFFFFFFF);
    renderer_fill_rect(r, rect_make(screen_x, screen_y + screen_h - 1, screen_w, 1), 0xFFC0C0C0);
    renderer_fill_rect(r, rect_make(screen_x + screen_w - 1, screen_y, 1, screen_h), 0xFFC0C0C0);
    /* Outer dark border */
    renderer_fill_rect(r, rect_make(screen_x - 1, screen_y - 1, screen_w + 2, 1), 0xFF000000);
    renderer_fill_rect(r, rect_make(screen_x - 1, screen_y, 1, screen_h + 1), 0xFF000000);
    renderer_fill_rect(r, rect_make(screen_x - 1, screen_y + screen_h, screen_w + 2, 1), 0xFF000000);
    renderer_fill_rect(r, rect_make(screen_x + screen_w, screen_y, 1, screen_h + 1), 0xFF000000);

    /* Separator under titlebar */
    gui_rect_t sep = rect_make(screen_x, screen_y + title_h, screen_w, 1);
    renderer_fill_rect(r, sep, 0xFF808080);
}

static void window_destroy(node_t *self) {
    if (self->userdata) {
        window_node_data_t *d = (window_node_data_t *)self->userdata;
        if (d->title) kfree(d->title);
        kfree(d);
        self->userdata = NULL;
    }
}

bool window_node_on_event(node_t *self, const gui_event_t *e) {
    window_node_data_t *d = (window_node_data_t *)self->userdata;
    if (!d) return false;

    if (e->type == GUI_EVENT_WIN_FOCUS) {
        d->focused = true;
        node_mark_dirty(self, NODE_DIRTY_PAINT);
        return false;
    }
    
    if (e->type == GUI_EVENT_WIN_BLUR) {
        d->focused = false;
        d->close_pressed = false;
        d->max_pressed = false;
        d->min_pressed = false;
        node_mark_dirty(self, NODE_DIRTY_PAINT);
        return false;
    }

    /* Key events: forward to application callback if installed */
    if (e->type == GUI_EVENT_KEY_DOWN) {
        if (d->key_down_cb) {
            return d->key_down_cb(self, e->key.scancode, d->key_userctx);
        }
        return false;
    }

    if (e->type == GUI_EVENT_KEY_CHAR) {
        if (d->key_char_cb) {
            return d->key_char_cb(self, (char)e->key.unicode, d->key_userctx);
        }
        return false;
    }

    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        extern compositor_t *g_compositor;
        if (!g_compositor) return false;
        camera_t *cam = g_compositor->camera;

        gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
        int screen_x = camera_world_to_screen_x(cam, pt.x);
        int screen_y = camera_world_to_screen_y(cam, pt.y);

        int title_h = camera_scale(cam, 24);
        int screen_w = camera_scale(cam, self->width);
        int btn_w = 18;  // fixed screen pixels
        int by = screen_y + 3;

        /* Three window buttons at right of titlebar */
        gui_rect_t close_rect = rect_make(screen_x + screen_w - btn_w - 3, by, btn_w, btn_w);
        gui_rect_t max_rect   = rect_make(screen_x + screen_w - 3*btn_w - 7, by, btn_w, btn_w);
        gui_rect_t min_rect   = rect_make(screen_x + screen_w - 5*btn_w - 11, by, btn_w, btn_w);

        if (rect_contains_point(close_rect, e->mouse.x, e->mouse.y)) {
            d->close_pressed = true;
            d->close_hover = true;
            node_mark_dirty(self, NODE_DIRTY_PAINT);
            return true;
        }
        if (rect_contains_point(max_rect, e->mouse.x, e->mouse.y)) {
            d->max_pressed = true;
            d->max_hover = true;
            node_mark_dirty(self, NODE_DIRTY_PAINT);
            return true;
        }
        if (rect_contains_point(min_rect, e->mouse.x, e->mouse.y)) {
            d->min_pressed = true;
            d->min_hover = true;
            node_mark_dirty(self, NODE_DIRTY_PAINT);
            return true;
        }

        /* Title bar drag detection (only if not on buttons) */
        gui_rect_t title_rect = rect_make(screen_x, screen_y, screen_w - 5*btn_w - 11, title_h);
        if (rect_contains_point(title_rect, e->mouse.x, e->mouse.y)) {
            d->dragging = true;
            d->drag_start_mx = e->mouse.x;
            d->drag_start_my = e->mouse.y;
            d->drag_start_win_x = self->local_x;
            d->drag_start_win_y = self->local_y;
            return true; /* consume */
        }
    } else if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        extern compositor_t *g_compositor;
        camera_t *cam = g_compositor ? g_compositor->camera : NULL;
        int screen_x = 0, screen_y = 0, screen_w = 0, btn_w = 18, by = 0;
        if (cam) {
            gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
            screen_x = camera_world_to_screen_x(cam, pt.x);
            screen_y = camera_world_to_screen_y(cam, pt.y);
            screen_w = camera_scale(cam, self->width);
            by = screen_y + 3;
        }
        gui_rect_t close_rect = rect_make(screen_x + screen_w - btn_w - 3, by, btn_w, btn_w);
        gui_rect_t max_rect   = rect_make(screen_x + screen_w - 3*btn_w - 7, by, btn_w, btn_w);
        gui_rect_t min_rect   = rect_make(screen_x + screen_w - 5*btn_w - 11, by, btn_w, btn_w);

        bool over_close = cam && rect_contains_point(close_rect, e->mouse.x, e->mouse.y);
        bool over_max   = cam && rect_contains_point(max_rect, e->mouse.x, e->mouse.y);
        bool over_min   = cam && rect_contains_point(min_rect, e->mouse.x, e->mouse.y);

        if (d->close_pressed && over_close) {
            extern gui_event_bus_t *g_event_bus;
            if (g_event_bus) {
                gui_event_t close_ev;
                memset(&close_ev, 0, sizeof(close_ev));
                close_ev.type = GUI_EVENT_WIN_CLOSE;
                close_ev.generic.a = (uint64_t)self;
                event_bus_post(g_event_bus, &close_ev);
            }
        } else if (d->max_pressed && over_max) {
            if (g_compositor) {
                int sw = g_compositor->camera->screen_w;
                int sh = g_compositor->camera->screen_h;
                if (d->maximized) {
                    node_set_position(self, d->saved_x, d->saved_y);
                    node_set_size(self, d->saved_w, d->saved_h);
                    d->maximized = false;
                } else {
                    d->saved_x = self->local_x;
                    d->saved_y = self->local_y;
                    d->saved_w = self->width;
                    d->saved_h = self->height;
                    node_set_position(self, 0, 0);
                    node_set_size(self, sw, sh - 46);
                    d->maximized = true;
                }
            }
        } else if (d->min_pressed && over_min) {
            self->visible = false;
            d->minimized = true;
            extern focus_manager_t *g_focus_manager;
            if (g_focus_manager) focus_manager_set_focus(g_focus_manager, NULL);
        }

        d->close_pressed = d->max_pressed = d->min_pressed = false;
        d->close_hover = d->max_hover = d->min_hover = false;
        node_mark_dirty(self, NODE_DIRTY_PAINT);

        if (d->dragging) {
            d->dragging = false;
            return true;
        }
    } else if (e->type == GUI_EVENT_MOUSE_MOVE) {
        extern compositor_t *g_compositor;
        if (!g_compositor) return false;
        camera_t *cam = g_compositor->camera;

        gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
        int screen_x = camera_world_to_screen_x(cam, pt.x);
        int screen_y = camera_world_to_screen_y(cam, pt.y);
        int screen_w = camera_scale(cam, self->width);
        int btn_w = 18;  // fixed screen pixels
        int by = screen_y + 3;

        gui_rect_t close_rect = rect_make(screen_x + screen_w - btn_w - 3, by, btn_w, btn_w);
        gui_rect_t max_rect   = rect_make(screen_x + screen_w - 3*btn_w - 7, by, btn_w, btn_w);
        gui_rect_t min_rect   = rect_make(screen_x + screen_w - 5*btn_w - 11, by, btn_w, btn_w);

        bool over_close = rect_contains_point(close_rect, e->mouse.x, e->mouse.y);
        bool over_max   = rect_contains_point(max_rect, e->mouse.x, e->mouse.y);
        bool over_min   = rect_contains_point(min_rect, e->mouse.x, e->mouse.y);

        if (over_close != d->close_hover) { d->close_hover = over_close; node_mark_dirty(self, NODE_DIRTY_PAINT); }
        if (over_max != d->max_hover)   { d->max_hover = over_max;   node_mark_dirty(self, NODE_DIRTY_PAINT); }
        if (over_min != d->min_hover)   { d->min_hover = over_min;   node_mark_dirty(self, NODE_DIRTY_PAINT); }

        if (d->close_pressed && !over_close) { d->close_pressed = false; node_mark_dirty(self, NODE_DIRTY_PAINT); }
        if (d->max_pressed && !over_max)   { d->max_pressed = false;   node_mark_dirty(self, NODE_DIRTY_PAINT); }
        if (d->min_pressed && !over_min)   { d->min_pressed = false;   node_mark_dirty(self, NODE_DIRTY_PAINT); }

        if (d->dragging) {
            int ddx = e->mouse.x - d->drag_start_mx;
            int ddy = e->mouse.y - d->drag_start_my;
            int world_dx = (int)((int64_t)ddx * CAMERA_ZOOM_SCALE / cam->zoom_fp);
            int world_dy = (int)((int64_t)ddy * CAMERA_ZOOM_SCALE / cam->zoom_fp);
            node_set_position(self, d->drag_start_win_x + world_dx, d->drag_start_win_y + world_dy);
            return true;
        }
    }
    return false;
}

static const node_vtable_t window_vtable = {
    .draw     = window_node_draw,
    .on_event = window_node_on_event,
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

const char *window_node_get_title(node_t *win) {
    if (win && win->type == NODE_WINDOW) {
        window_node_data_t *d = (window_node_data_t *)win->userdata;
        if (d) return d->title;
    }
    return NULL;
}

void window_node_set_key_handler(node_t *win,
    bool (*key_down_cb)(node_t *, uint8_t, void *),
    bool (*key_char_cb)(node_t *, char, void *),
    void *userctx) {
    if (!win || win->type != NODE_WINDOW) return;
    window_node_data_t *d = (window_node_data_t *)win->userdata;
    if (!d) return;
    d->key_down_cb  = key_down_cb;
    d->key_char_cb  = key_char_cb;
    d->key_userctx  = userctx;
}

void window_node_restore(node_t *win) {
    if (!win || win->type != NODE_WINDOW) return;
    window_node_data_t *d = (window_node_data_t *)win->userdata;
    if (!d || !d->minimized) return;
    
    win->visible = true;
    window_manager_bring_to_front(win);
    d->minimized = false;
    extern focus_manager_t *g_focus_manager;
    if (g_focus_manager) focus_manager_set_focus(g_focus_manager, win);
}

bool window_node_is_minimized(node_t *win) {
    if (!win || win->type != NODE_WINDOW) return false;
    window_node_data_t *d = (window_node_data_t *)win->userdata;
    return d ? d->minimized : false;
}
