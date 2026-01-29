/*
 * gui/widgets/checkbutton.c
 *
 * Chicago95-themed checkbox and radio controls (16x16 sprites from
 * pixel_controls.h, label drawn in the 8x16 bitmap font).
 */
#include "checkbutton.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "../assets/pixel_controls.h"
#include "../assets/asset_manager.h"
#include "kheap.h"
#include "string.h"

typedef struct {
    const glyph_t     *font;
    char              *text;
    bool               checked;
    bool               radio;                 /* NODE_RADIO vs NODE_CHECKBOX */
    checkbutton_toggle_cb_t on_toggle;
    void              *toggle_ud;
} checkbutton_data_t;

static uint32_t sprite_id_for(const checkbutton_data_t *d) {
    if (d->radio) {
        return d->checked ? CONTROL_RADIO_SELECTED : CONTROL_RADIO_UNSELECTED;
    }
    return d->checked ? CONTROL_CHECKBOX_CHECKED : CONTROL_CHECKBOX_UNCHECKED;
}

static void checkbutton_draw(node_t *self, struct gui_renderer *r) {
    checkbutton_data_t *d = (checkbutton_data_t *)self->userdata;
    if (!d) return;

    if (!d->font) d->font = asset_manager_get_font(NULL);

    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int screen_x = camera_world_to_screen_x(cam, pt.x);
    int screen_y = camera_world_to_screen_y(cam, pt.y);
    int screen_w = camera_scale(cam, self->width);
    int screen_h = camera_scale(cam, self->height);

    self->screen_bounds = rect_make(screen_x, screen_y, screen_w, screen_h);

    const pixel_control_t *spr = &pixel_controls[sprite_id_for(d)];
    int sx = screen_x;
    int sy = screen_y + (screen_h - camera_scale(cam, spr->h)) / 2;
    renderer_blit(r, sx, sy, spr->data, spr->w, spr->h, spr->w,
                  0, 0, spr->w, spr->h);

    /* Label text, 8px per char / 16px tall, vertically centred. */
    int tx = screen_x + camera_scale(cam, spr->w) + camera_scale(cam, 4);
    int ty = screen_y + (screen_h - 16) / 2;
    if (d->font) {
        for (const char *c = d->text; c && *c; c++) {
            renderer_draw_glyph(r, tx, ty, 0xFF000000, 0x00000000, &d->font[(unsigned char)*c]);
            tx += 8;
        }
    }
}

static bool checkbutton_on_event(node_t *self, const gui_event_t *e) {
    checkbutton_data_t *d = (checkbutton_data_t *)self->userdata;
    if (!d) return false;

    if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        if (rect_contains_point(self->screen_bounds, e->mouse.x, e->mouse.y)) {
            d->checked = !d->checked;
            node_mark_dirty(self, NODE_DIRTY_PAINT);
            if (d->on_toggle) d->on_toggle(self, d->toggle_ud, d->checked);
            return true;
        }
    }
    return false;
}

static void checkbutton_destroy(node_t *self) {
    if (self->userdata) {
        checkbutton_data_t *d = (checkbutton_data_t *)self->userdata;
        if (d->text) kfree(d->text);
        kfree(d);
        self->userdata = NULL;
    }
}

static const node_vtable_t checkbutton_vtable = {
    .draw     = checkbutton_draw,
    .on_event = checkbutton_on_event,
    .layout   = NULL,
    .destroy  = checkbutton_destroy,
};

static node_t *checkbutton_create_common(node_type_t type, const char *name,
                                         int x, int y, const char *text, bool checked) {
    node_t *n = node_create(type, name);
    if (!n) return NULL;

    checkbutton_data_t *d = (checkbutton_data_t *)kmalloc(sizeof(checkbutton_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(checkbutton_data_t));

    if (text) {
        d->text = (char *)kmalloc(strlen(text) + 1);
        strcpy(d->text, text);
    }
    d->checked = checked;
    d->radio = (type == NODE_RADIO);

    n->userdata = d;
    n->vtable = &checkbutton_vtable;
    n->local_x = x;
    n->local_y = y;
    n->width  = (int)strlen(text ? text : "") * 8 + 24;   /* sprite 16 + gap 4 + text */
    n->height = 20;
    n->interactive = true;

    return n;
}

node_t *checkbox_create(const char *name, int x, int y, const char *text, bool checked) {
    return checkbutton_create_common(NODE_CHECKBOX, name, x, y, text, checked);
}

node_t *radio_create(const char *name, int x, int y, const char *text, bool checked) {
    return checkbutton_create_common(NODE_RADIO, name, x, y, text, checked);
}

bool checkbutton_get_checked(node_t *control) {
    checkbutton_data_t *d = control ? (checkbutton_data_t *)control->userdata : NULL;
    return d ? d->checked : false;
}

void checkbutton_set_checked(node_t *control, bool checked) {
    checkbutton_data_t *d = control ? (checkbutton_data_t *)control->userdata : NULL;
    if (!d || d->checked == checked) return;
    d->checked = checked;
    node_mark_dirty(control, NODE_DIRTY_PAINT);
}

void checkbutton_set_on_toggle(node_t *control, checkbutton_toggle_cb_t cb, void *userdata) {
    checkbutton_data_t *d = control ? (checkbutton_data_t *)control->userdata : NULL;
    if (!d) return;
    d->on_toggle = cb;
    d->toggle_ud = userdata;
}