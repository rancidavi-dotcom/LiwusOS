/*
 * gui/render/renderer.c  —  Renderer lifecycle (backend-agnostic)
 */
#include "renderer.h"
#include "kheap.h"
#include "string.h"

gui_renderer_t *renderer_create(const renderer_ops_t *ops, void *backend_state,
                                  int screen_w, int screen_h) {
    if (!ops) return NULL;
    gui_renderer_t *r = (gui_renderer_t *)kmalloc(sizeof(gui_renderer_t));
    if (!r) return NULL;
    r->ops      = ops;
    r->backend  = backend_state;
    r->clip     = rect_make(0, 0, screen_w, screen_h);
    r->opacity  = 1.0f;
    r->screen_w = screen_w;
    r->screen_h = screen_h;
    return r;
}

void renderer_destroy(gui_renderer_t *r) {
    if (!r) return;
    if (r->ops->destroy) r->ops->destroy(r);
    kfree(r);
}
