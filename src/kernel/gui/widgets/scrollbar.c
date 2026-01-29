/*
 * gui/widgets/scrollbar.c
 *
 * Chicago95 vertical scrollbar (steppers + tiled thumb).
 * Fixed-point arithmetic (value 0..1000) to avoid -mno-sse.
 */
#include "scrollbar.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "../assets/pixel_controls.h"
#include "../assets/asset_manager.h"
#include "kheap.h"
#include "string.h"

#define SB_SCALE 1000

typedef struct {
    const glyph_t     *font;
    int                value;        // 0 .. SB_SCALE
    int                range_ratio;  // thumb size / track size (0..SB_SCALE)
    bool               dragging;     // thumb drag active
    int                drag_start_y; // screen y at drag start
    int                drag_start_val;
    scrollbar_change_cb_t on_change;
    void              *change_ud;
} scrollbar_data_t;

static int clamp_int(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void scrollbar_draw(node_t *self, struct gui_renderer *r) {
    scrollbar_data_t *d = (scrollbar_data_t *)self->userdata;
    if (!d) return;

    if (!d->font) d->font = asset_manager_get_font(NULL);

    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int sx = camera_world_to_screen_x(cam, pt.x);
    int sy = camera_world_to_screen_y(cam, pt.y);
    int sw = camera_scale(cam, self->width);
    int sh = camera_scale(cam, self->height);

    self->screen_bounds = rect_make(sx, sy, sw, sh);

    // Steppers: 16x16 each at top/bottom
    const pixel_control_t *up = &pixel_controls[CONTROL_STEPPER_UP];
    const pixel_control_t *down = &pixel_controls[CONTROL_STEPPER_DOWN];
    const pixel_control_t *btn = &pixel_controls[CONTROL_SCROLL_BTN];

    int step_w = camera_scale(cam, up->w);
    int step_h = camera_scale(cam, up->h);
    int track_x = sx + (sw - step_w) / 2;
    int track_y = sy + step_h;
    int track_h = sh - 2 * step_h;

    // Up stepper
    renderer_blit(r, track_x, sy, up->data, up->w, up->h, up->w, 0, 0, up->w, up->h);
    // Down stepper
    renderer_blit(r, track_x, sy + sh - step_h, down->data, down->w, down->h, down->w, 0, 0, down->w, down->h);

    // Track background (sunken bevel like Chicago95 scroll track)
    uint32_t track_bg = 0xFFC0C0C0; // silver
    uint32_t white = 0xFFFFFFFF;
    uint32_t dk = 0xFF808080;
    renderer_fill_rect(r, rect_make(track_x, track_y, step_w, track_h), track_bg);
    // Bevel: top/left white, bottom/right gray
    renderer_fill_rect(r, rect_make(track_x, track_y, step_w, 1), white);
    renderer_fill_rect(r, rect_make(track_x, track_y, 1, track_h), white);
    renderer_fill_rect(r, rect_make(track_x, track_y + track_h - 1, step_w, 1), dk);
    renderer_fill_rect(r, rect_make(track_x + step_w - 1, track_y, 1, track_h), dk);

    // Thumb
    int rr = d->range_ratio > 0 ? d->range_ratio : 200; // default 0.2 * 1000
    int thumb_h = (track_h * rr) / SB_SCALE;
    if (thumb_h < step_h) thumb_h = step_h;
    if (thumb_h > track_h) thumb_h = track_h;
    int thumb_y = track_y + ((track_h - thumb_h) * d->value) / SB_SCALE;

    // Tile scroll_btn vertically to fill thumb height
    int tile_h = camera_scale(cam, btn->h);
    for (int ty = thumb_y; ty < thumb_y + thumb_h; ty += tile_h) {
        int draw_h = tile_h;
        if (ty + draw_h > thumb_y + thumb_h) draw_h = thumb_y + thumb_h - ty;
        renderer_blit(r, track_x, ty, btn->data, btn->w, btn->h, btn->w,
                      0, 0, btn->w, draw_h);
    }
}

static bool scrollbar_on_event(node_t *self, const gui_event_t *e) {
    scrollbar_data_t *d = (scrollbar_data_t *)self->userdata;
    if (!d) return false;

    extern compositor_t *g_compositor;
    if (!g_compositor) return false;
    camera_t *cam = g_compositor->camera;

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int sx = camera_world_to_screen_x(cam, pt.x);
    int sy = camera_world_to_screen_y(cam, pt.y);
    int sw = camera_scale(cam, self->width);
    int sh = camera_scale(cam, self->height);

    const pixel_control_t *up = &pixel_controls[CONTROL_STEPPER_UP];
    int step_h = camera_scale(cam, up->h);
    int track_x = sx + (sw - step_h) / 2;
    int track_y = sy + step_h;
    int track_h = sh - 2 * step_h;

    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        // Up stepper
        gui_rect_t up_rect = rect_make(track_x, sy, step_h, step_h);
        if (rect_contains_point(up_rect, e->mouse.x, e->mouse.y)) {
            d->value -= SB_SCALE / 10; // 0.1
            d->value = clamp_int(d->value, 0, SB_SCALE);
            node_mark_dirty(self, NODE_DIRTY_PAINT);
            if (d->on_change) d->on_change(self, d->change_ud, d->value);
            return true;
        }
        // Down stepper
        gui_rect_t down_rect = rect_make(track_x, sy + sh - step_h, step_h, step_h);
        if (rect_contains_point(down_rect, e->mouse.x, e->mouse.y)) {
            d->value += SB_SCALE / 10;
            d->value = clamp_int(d->value, 0, SB_SCALE);
            node_mark_dirty(self, NODE_DIRTY_PAINT);
            if (d->on_change) d->on_change(self, d->change_ud, d->value);
            return true;
        }
        // Thumb drag
        int rr = d->range_ratio > 0 ? d->range_ratio : 200;
        int thumb_h = (track_h * rr) / SB_SCALE;
        if (thumb_h < step_h) thumb_h = step_h;
        int thumb_y = track_y + ((track_h - thumb_h) * d->value) / SB_SCALE;
        gui_rect_t thumb_rect = rect_make(track_x, thumb_y, step_h, thumb_h);
        if (rect_contains_point(thumb_rect, e->mouse.x, e->mouse.y)) {
            d->dragging = true;
            d->drag_start_y = e->mouse.y;
            d->drag_start_val = d->value;
            return true;
        }
        // Click track to jump
        if (e->mouse.y < thumb_y) d->value -= SB_SCALE / 2;
        else if (e->mouse.y > thumb_y + thumb_h) d->value += SB_SCALE / 2;
        d->value = clamp_int(d->value, 0, SB_SCALE);
        node_mark_dirty(self, NODE_DIRTY_PAINT);
        if (d->on_change) d->on_change(self, d->change_ud, d->value);
        return true;
    } else if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        if (d->dragging) {
            d->dragging = false;
            return true;
        }
    } else if (e->type == GUI_EVENT_MOUSE_MOVE && d->dragging) {
        int delta = ((e->mouse.y - d->drag_start_y) * SB_SCALE) / track_h;
        d->value = d->drag_start_val + delta;
        d->value = clamp_int(d->value, 0, SB_SCALE);
        node_mark_dirty(self, NODE_DIRTY_PAINT);
        if (d->on_change) d->on_change(self, d->change_ud, d->value);
        return true;
    }
    return false;
}

static void scrollbar_destroy(node_t *self) {
    if (self->userdata) {
        kfree(self->userdata);
        self->userdata = NULL;
    }
}

static const node_vtable_t scrollbar_vtable = {
    .draw     = scrollbar_draw,
    .on_event = scrollbar_on_event,
    .layout   = NULL,
    .destroy  = scrollbar_destroy,
};

node_t *scrollbar_create(const char *name, int x, int y, int w, int h) {
    node_t *n = node_create(NODE_SCROLLBAR, name);
    if (!n) return NULL;

    scrollbar_data_t *d = (scrollbar_data_t *)kmalloc(sizeof(scrollbar_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(scrollbar_data_t));
    d->value = 0;
    d->range_ratio = 200; // 0.2

    n->userdata = d;
    n->vtable = &scrollbar_vtable;
    n->local_x = x;
    n->local_y = y;
    n->width = w;
    n->height = h;
    n->interactive = true;
    return n;
}

void scrollbar_set_value(node_t *sb, int value) {
    scrollbar_data_t *d = sb ? (scrollbar_data_t *)sb->userdata : NULL;
    if (!d) return;
    d->value = clamp_int(value, 0, SB_SCALE);
    node_mark_dirty(sb, NODE_DIRTY_PAINT);
}

int scrollbar_get_value(node_t *sb) {
    scrollbar_data_t *d = sb ? (scrollbar_data_t *)sb->userdata : NULL;
    return d ? d->value : 0;
}

void scrollbar_set_range(node_t *sb, int min, int max) {
    scrollbar_data_t *d = sb ? (scrollbar_data_t *)sb->userdata : NULL;
    if (!d) return;
    int r = max - min;
    d->range_ratio = clamp_int(r, 0, SB_SCALE);
    node_mark_dirty(sb, NODE_DIRTY_PAINT);
}

void scrollbar_set_on_change(node_t *sb, scrollbar_change_cb_t cb, void *userdata) {
    scrollbar_data_t *d = sb ? (scrollbar_data_t *)sb->userdata : NULL;
    if (!d) return;
    d->on_change = cb;
    d->change_ud = userdata;
}