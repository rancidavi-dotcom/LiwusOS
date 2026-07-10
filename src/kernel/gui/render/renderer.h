/*
 * gui/render/renderer.h
 *
 * Abstract Renderer interface.
 *
 * The compositor and all widgets call ONLY these functions.
 * The concrete backend (framebuffer, future Vulkan, etc.) is selected
 * at initialisation by filling in renderer_t.ops.
 *
 * Adding a new backend means:
 *   1. Create fb_renderer.c (or vulkan_renderer.c, etc.)
 *   2. Fill a renderer_ops_t table.
 *   3. Call renderer_init() with that table.
 *   4. Zero code-changes elsewhere.
 */
#ifndef GUI_RENDERER_H
#define GUI_RENDERER_H

#include <stdint.h>
#include "../math/rect.h"

/* --------------------------------------------------------------------------
 * Draw primitives
 * -------------------------------------------------------------------------- */

/* Bitmask font glyph (8×16) */
typedef struct {
    const uint8_t *bitmap; /* 16 rows × 1 byte each */
    int cell_w, cell_h;
} glyph_t;

/* --------------------------------------------------------------------------
 * Backend operations table (vtable for the renderer backend)
 * -------------------------------------------------------------------------- */

typedef struct gui_renderer gui_renderer_t;

typedef struct {
    /* Fill a solid-colour rectangle.  Color is 0xAARRGGBB. */
    void (*fill_rect)(gui_renderer_t *r, gui_rect_t rect, uint32_t color);

    /* Draw a 1-pixel border around rect. */
    void (*draw_rect)(gui_renderer_t *r, gui_rect_t rect,
                       uint32_t color, int thickness);

    /* Alpha-blend a source pixel array onto the back-buffer. */
    void (*blit)(gui_renderer_t *r,
                 int dest_x, int dest_y,
                 const uint32_t *src, int src_w, int src_h, int src_pitch,
                 int src_x, int src_y, int copy_w, int copy_h);

    /* Scaled blit (nearest-neighbour). */
    void (*blit_scaled)(gui_renderer_t *r,
                         int dest_x, int dest_y,
                         const uint32_t *src, int src_w, int src_h,
                         float scale_x, float scale_y);

    /* Draw a PSF glyph at pixel position (x, y).  fg/bg are 0xAARRGGBB.
     * Pass bg=0 for transparent background. */
    void (*draw_glyph)(gui_renderer_t *r, int x, int y,
                        uint32_t fg, uint32_t bg, const glyph_t *g);

    /* Clip all further draws to this rectangle.
     * Pass rect_zero() to clear clipping. */
    void (*set_clip)(gui_renderer_t *r, gui_rect_t clip);

    /* Set global opacity multiplier for subsequent draws. */
    void (*set_opacity)(gui_renderer_t *r, float opacity);

    /* Flip back-buffer to screen (called once per frame). */
    void (*present)(gui_renderer_t *r);

    /* Backend-specific teardown. */
    void (*destroy)(gui_renderer_t *r);
} renderer_ops_t;

/* --------------------------------------------------------------------------
 * Renderer object
 * -------------------------------------------------------------------------- */

struct gui_renderer {
    const renderer_ops_t *ops;
    void                 *backend;  /* backend-private state */
    gui_rect_t            clip;
    float                 opacity;
    int                   screen_w;
    int                   screen_h;
};

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

gui_renderer_t *renderer_create(const renderer_ops_t *ops, void *backend_state,
                                  int screen_w, int screen_h);
void            renderer_destroy(gui_renderer_t *r);

/* --------------------------------------------------------------------------
 * Convenience wrappers (inline — dispatch through ops)
 * -------------------------------------------------------------------------- */

static inline void renderer_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color) {
    if (r && r->ops->fill_rect) r->ops->fill_rect(r, rect, color);
}

static inline void renderer_draw_rect(gui_renderer_t *r, gui_rect_t rect,
                                        uint32_t color, int thickness) {
    if (r && r->ops->draw_rect) r->ops->draw_rect(r, rect, color, thickness);
}

static inline void renderer_blit(gui_renderer_t *r,
                                 int dest_x, int dest_y,
                                 const uint32_t *src, int src_w, int src_h, int src_pitch,
                                 int src_x, int src_y, int copy_w, int copy_h) {
    if (r && r->ops->blit)
        r->ops->blit(r, dest_x, dest_y, src, src_w, src_h, src_pitch, src_x, src_y, copy_w, copy_h);
}

static inline void renderer_draw_glyph(gui_renderer_t *r, int x, int y,
                                       uint32_t fg, uint32_t bg, const glyph_t *g) {
    if (r && r->ops->draw_glyph)
        r->ops->draw_glyph(r, x, y, fg, bg, g);
}

static inline void renderer_set_clip(gui_renderer_t *r, gui_rect_t clip) {
    if (r && r->ops->set_clip) r->ops->set_clip(r, clip);
    if (r) r->clip = clip;
}

static inline void renderer_present(gui_renderer_t *r) {
    if (r && r->ops->present) r->ops->present(r);
}

#endif /* GUI_RENDERER_H */
