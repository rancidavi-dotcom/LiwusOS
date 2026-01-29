/*
 * gui/widgets/panel.c
 */
#include "panel.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "kheap.h"
#include "string.h"
#include "../core/theme_engine.h"

typedef struct {
    uint32_t bg_color;
    uint32_t border_color;
    int      border_thickness;
    bool     sunken;
} panel_data_t;

static void panel_draw(node_t *self, struct gui_renderer *r) {
    panel_data_t *d = (panel_data_t *)self->userdata;
    if (!d) return;

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int sx = pt.x;
    int sy = pt.y;

    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    /* Get screen bounds of this node */
    int screen_x = camera_world_to_screen_x(cam, sx);
    int screen_y = camera_world_to_screen_y(cam, sy);
    int screen_w = camera_scale(cam, self->width);
    int screen_h = camera_scale(cam, self->height);
    
    /* Store for hit testing */
    self->screen_bounds = rect_make(screen_x, screen_y, screen_w, screen_h);

    /* Solid background — no transparency for classic look */
    if (d->bg_color & 0xFF000000) {
        renderer_fill_rect(r, self->screen_bounds, d->bg_color | 0xFF000000);
    }

    /* 3D beveled border — classic raised frame (sunken = inverted) */
    if (d->border_thickness > 0 && (d->border_color & 0xFF000000)) {
        uint32_t bc = d->border_color | 0xFF000000;
        int bt = d->border_thickness;
        uint32_t dark = 0xFF808080;

        if (d->sunken) {
            /* dark top/left, light bottom/right */
            renderer_fill_rect(r, rect_make(screen_x, screen_y, screen_w, bt), dark);
            renderer_fill_rect(r, rect_make(screen_x, screen_y, bt, screen_h), dark);
            renderer_fill_rect(r, rect_make(screen_x, screen_y + screen_h - bt, screen_w, bt), bc);
            renderer_fill_rect(r, rect_make(screen_x + screen_w - bt, screen_y, bt, screen_h), bc);
        } else {
            /* white top/left */
            renderer_fill_rect(r, rect_make(screen_x, screen_y, screen_w, bt), bc);
            renderer_fill_rect(r, rect_make(screen_x, screen_y, bt, screen_h), bc);
            /* gray shadow bottom/right */
            renderer_fill_rect(r, rect_make(screen_x, screen_y + screen_h - bt, screen_w, bt), dark);
            renderer_fill_rect(r, rect_make(screen_x + screen_w - bt, screen_y, bt, screen_h), dark);
        }
    }
}

static void panel_destroy(node_t *self) {
    if (self->userdata) {
        kfree(self->userdata);
        self->userdata = NULL;
    }
}

static const node_vtable_t panel_vtable = {
    .draw     = panel_draw,
    .on_event = NULL,
    .layout   = NULL,
    .destroy  = panel_destroy,
};

node_t *panel_create(const char *name, int x, int y, int w, int h, uint32_t bg_color) {
    node_t *n = node_create(NODE_PANEL, name);
    if (!n) return NULL;

    panel_data_t *d = (panel_data_t *)kmalloc(sizeof(panel_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(panel_data_t));

    d->bg_color = bg_color;
    
    n->userdata = d;
    n->vtable = &panel_vtable;
    n->local_x = x;
    n->local_y = y;
    n->width = w;
    n->height = h;

    return n;
}

void panel_set_bg_color(node_t *panel, uint32_t color) {
    if (panel && panel->type == NODE_PANEL) {
        panel_data_t *d = (panel_data_t *)panel->userdata;
        d->bg_color = color;
        node_mark_dirty(panel, NODE_DIRTY_PAINT);
    }
}

void panel_set_border(node_t *panel, uint32_t color, int thickness) {
    if (panel && panel->type == NODE_PANEL) {
        panel_data_t *d = (panel_data_t *)panel->userdata;
        d->border_color = color;
        d->border_thickness = thickness;
        node_mark_dirty(panel, NODE_DIRTY_PAINT);
    }
}

void panel_set_sunken(node_t *panel, bool sunken) {
    if (panel && panel->type == NODE_PANEL) {
        panel_data_t *d = (panel_data_t *)panel->userdata;
        d->sunken = sunken;
        node_mark_dirty(panel, NODE_DIRTY_PAINT);
    }
}
