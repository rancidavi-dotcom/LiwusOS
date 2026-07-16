/*
 * gui/widgets/image_node.c
 *
 * Image node — renders a raw ARGB pixel buffer in the scene graph.
 *
 * This is the missing piece that allows userspace apps (like Doom)
 * to push pixel data into a LGX compositor window. The pixel buffer
 * is stored in kernel-owned memory and blitted to the back-buffer
 * during the compositor frame pass.
 */
#include "image_node.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "../scene/camera.h"
#include "kheap.h"
#include "string.h"

/* --------------------------------------------------------------------------
 * Internal data stored in node->userdata
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t *pixels;       /* ARGB pixel buffer (width * height) */
    uint32_t  pixel_count;  /* number of uint32_t elements         */
    int       buf_width;    /* buffer width in pixels              */
    int       buf_height;   /* buffer height in pixels             */
} image_data_t;

/* --------------------------------------------------------------------------
 * vtable: draw
 *
 * Blits the pixel buffer into the back-buffer at the node's
 * world-transformed screen position, scaled to the node's display size.
 * -------------------------------------------------------------------------- */

static void image_draw(node_t *self, struct gui_renderer *r) {
    image_data_t *d = (image_data_t *)self->userdata;
    if (!d || !d->pixels || d->pixel_count == 0) return;

    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    /* Compute screen position from world transform */
    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int screen_x = camera_world_to_screen_x(cam, pt.x);
    int screen_y = camera_world_to_screen_y(cam, pt.y);

    /* Store screen bounds for hit-testing / culling */
    int screen_w = self->width;
    int screen_h = self->height;
    self->screen_bounds = rect_make(screen_x, screen_y, screen_w, screen_h);

    /* Blit the pixel buffer to the renderer back-buffer.
     * The buffer is buf_width x buf_height, but we display it at
     * self->width x self->height (the node's display size).
     * If the sizes match, it's a direct blit; otherwise fb_renderer
     * handles nearest-neighbour scaling via the blit path. */
    renderer_blit(r,
                  screen_x, screen_y,
                  d->pixels,
                  d->buf_width, d->buf_height,
                  d->buf_width,  /* source pitch = width (tightly packed) */
                  0, 0,          /* source x, y */
                  d->buf_width, d->buf_height);  /* copy width, height */
}

/* --------------------------------------------------------------------------
 * vtable: destroy
 * -------------------------------------------------------------------------- */

static void image_destroy(node_t *self) {
    if (self->userdata) {
        image_data_t *d = (image_data_t *)self->userdata;
        if (d->pixels) kfree(d->pixels);
        kfree(d);
        self->userdata = NULL;
    }
}

/* --------------------------------------------------------------------------
 * vtable
 * -------------------------------------------------------------------------- */

static const node_vtable_t image_vtable = {
    .draw     = image_draw,
    .on_event = NULL,
    .layout   = NULL,
    .destroy  = image_destroy,
};

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

node_t *image_node_create(const char *name, int x, int y,
                           int width, int height,
                           const uint32_t *pixels) {
    if (width <= 0 || height <= 0) return NULL;

    node_t *n = node_create(NODE_IMAGE, name ? name : "img");
    if (!n) return NULL;

    image_data_t *d = (image_data_t *)kmalloc(sizeof(image_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(image_data_t));

    uint32_t count = (uint32_t)width * (uint32_t)height;
    uint32_t *buf = (uint32_t *)kmalloc(count * sizeof(uint32_t));
    if (!buf) { kfree(d); node_destroy(n); return NULL; }

    if (pixels) {
        /* Copy user-provided pixels */
        for (uint32_t i = 0; i < count; i++) buf[i] = pixels[i];
    } else {
        /* Black framebuffer */
        for (uint32_t i = 0; i < count; i++) buf[i] = 0xFF000000;
    }

    d->pixels      = buf;
    d->pixel_count = count;
    d->buf_width   = width;
    d->buf_height  = height;

    n->userdata   = d;
    n->vtable     = &image_vtable;
    n->local_x    = x;
    n->local_y    = y;
    n->width      = width;
    n->height     = height;
    n->visible    = true;
    n->interactive = false;

    return n;
}

int image_node_update(node_t *node, const uint32_t *pixels, uint32_t count) {
    if (!node || node->type != NODE_IMAGE) return -1;
    if (!pixels || count == 0) return -1;

    image_data_t *d = (image_data_t *)node->userdata;
    if (!d) return -1;

    /* Reallocate if count changed */
    if (count != d->pixel_count) {
        uint32_t *new_buf = (uint32_t *)kmalloc(count * sizeof(uint32_t));
        if (!new_buf) return -1;
        if (d->pixels) kfree(d->pixels);
        d->pixels = new_buf;
        d->pixel_count = count;
        /* Derive width/height from count and current node dimensions */
        d->buf_width  = node->width;
        d->buf_height = node->height;
        if (d->buf_width * d->buf_height != (int)count) {
            /* count doesn't match current dims — assume square-ish */
            d->buf_width  = (int)count / node->height;
            if (d->buf_width <= 0) d->buf_width = (int)count;
            d->buf_height = (int)count / d->buf_width;
            if (d->buf_height <= 0) d->buf_height = 1;
        }
    }

    /* Copy pixels */
    for (uint32_t i = 0; i < count; i++) d->pixels[i] = pixels[i];

    /* Mark dirty so compositor repaints this node */
    node_mark_dirty(node, NODE_DIRTY_PAINT);
    return 0;
}

int image_node_resize(node_t *node, int new_width, int new_height) {
    if (!node || node->type != NODE_IMAGE) return -1;
    if (new_width <= 0 || new_height <= 0) return -1;

    image_data_t *d = (image_data_t *)node->userdata;
    if (!d) return -1;

    uint32_t new_count = (uint32_t)new_width * (uint32_t)new_height;
    uint32_t *new_buf = (uint32_t *)kmalloc(new_count * sizeof(uint32_t));
    if (!new_buf) return -1;

    /* Clear to black */
    for (uint32_t i = 0; i < new_count; i++) new_buf[i] = 0xFF000000;

    if (d->pixels) kfree(d->pixels);

    d->pixels      = new_buf;
    d->pixel_count = new_count;
    d->buf_width   = new_width;
    d->buf_height  = new_height;

    node->width  = new_width;
    node->height = new_height;
    node_mark_dirty(node, NODE_DIRTY_LAYOUT | NODE_DIRTY_PAINT);
    return 0;
}
