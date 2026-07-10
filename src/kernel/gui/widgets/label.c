/*
 * gui/widgets/label.c
 */
#include "label.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "kheap.h"
#include "string.h"
#include "../assets/asset_manager.h"
#include "../core/theme_engine.h"
typedef struct {
    char    *text;
    uint32_t color;
    const glyph_t *font;
} label_data_t;

static void label_draw(node_t *self, struct gui_renderer *r) {
    label_data_t *d = (label_data_t *)self->userdata;
    if (!d || !d->text) return;

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

    /* O renderer atual não tem scale glyph. Então só desenhamos se zoom == 1,
     * ou precisamos escalar via blit_scaled.
     * Na Fase 3, vamos apenas usar draw_glyph. Zoom fará o texto ficar no
     * tamanho fixo até implementarmos Signed Distance Fields na Fase 4.
     */
    int cx = screen_x;
    int cy = screen_y;

    for (int i = 0; d->text[i] != '\0'; i++) {
        unsigned char c = d->text[i];
        renderer_draw_glyph(r, cx, cy, d->color, 0x00000000, &d->font[c]);
        cx += 8; /* font_width */
    }
}

static void label_destroy(node_t *self) {
    if (self->userdata) {
        label_data_t *d = (label_data_t *)self->userdata;
        if (d->text) kfree(d->text);
        kfree(d);
        self->userdata = NULL;
    }
}

static const node_vtable_t label_vtable = {
    .draw     = label_draw,
    .on_event = NULL,
    .layout   = NULL,
    .destroy  = label_destroy,
};

node_t *label_create(const char *name, int x, int y, const char *text, uint32_t color) {
    node_t *n = node_create(NODE_LABEL, name);
    if (!n) return NULL;

    label_data_t *d = (label_data_t *)kmalloc(sizeof(label_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(label_data_t));

    if (text) {
        d->text = (char *)kmalloc(strlen(text) + 1);
        strcpy(d->text, text);
    }
    d->color = color;
    
    n->userdata = d;
    n->vtable = &label_vtable;
    n->local_x = x;
    n->local_y = y;
    n->width = text ? (strlen(text) * 8) : 8;
    n->height = 16;

    return n;
}

void label_set_text(node_t *label, const char *text) {
    if (label && label->type == NODE_LABEL) {
        label_data_t *d = (label_data_t *)label->userdata;
        if (d->text) kfree(d->text);
        if (text) {
            d->text = (char *)kmalloc(strlen(text) + 1);
            strcpy(d->text, text);
            label->width = strlen(text) * 8;
        } else {
            d->text = NULL;
            label->width = 8;
        }
        node_mark_dirty(label, NODE_DIRTY_PAINT);
    }
}

void label_set_color(node_t *label, uint32_t color) {
    if (label && label->type == NODE_LABEL) {
        label_data_t *d = (label_data_t *)label->userdata;
        d->color = color;
        node_mark_dirty(label, NODE_DIRTY_PAINT);
    }
}
