/*
 * gui/math/transform.h
 *
 * 3×3 affine transform for 2D — drives the coordinate system chain:
 *   Local → World (Canvas) → Screen (Viewport / Camera)
 *
 * Stored as a column-major 3×3 matrix so it matches GPU conventions:
 *
 *   | a  c  tx |
 *   | b  d  ty |
 *   | 0  0   1 |
 *
 * a,d  = scale X,Y          b,c = shear (rarely non-zero in 2-D)
 * tx,ty = translation
 */
#ifndef GUI_TRANSFORM_H
#define GUI_TRANSFORM_H

#include <stdint.h>
#include "rect.h"

typedef struct {
    int32_t a, b;   /* column 0  (scale-x, shear-y)  [Fixed Point 10.22 or 16.16, we'll use 16.16: * 65536] */
    int32_t c, d;   /* column 1  (shear-x, scale-y) */
    int32_t tx, ty; /* column 2  (translation in pixels) */
} gui_transform_t;

#define TRANSFORM_SCALE 65536

/* --------------------------------------------------------------------------
 * Construction helpers
 * -------------------------------------------------------------------------- */

static inline gui_transform_t transform_identity(void) {
    gui_transform_t t;
    t.a = TRANSFORM_SCALE; t.b = 0;
    t.c = 0;               t.d = TRANSFORM_SCALE;
    t.tx = 0;              t.ty = 0;
    return t;
}

static inline gui_transform_t transform_translation(int32_t tx, int32_t ty) {
    gui_transform_t t = transform_identity();
    t.tx = tx; t.ty = ty;
    return t;
}

static inline gui_transform_t transform_scale(int32_t sx, int32_t sy) {
    gui_transform_t t = transform_identity();
    t.a = sx; t.d = sy;
    return t;
}

static inline gui_transform_t transform_uniform_scale(int32_t s) {
    return transform_scale(s, s);
}

/* --------------------------------------------------------------------------
 * Operations
 * -------------------------------------------------------------------------- */

/* Concatenate: apply A first, then B  (result = B ∘ A) */
static inline gui_transform_t transform_concat(gui_transform_t a, gui_transform_t b) {
    gui_transform_t r;
    r.a  = (int32_t)(((int64_t)b.a * a.a + (int64_t)b.c * a.b) / TRANSFORM_SCALE);
    r.b  = (int32_t)(((int64_t)b.b * a.a + (int64_t)b.d * a.b) / TRANSFORM_SCALE);
    r.c  = (int32_t)(((int64_t)b.a * a.c + (int64_t)b.c * a.d) / TRANSFORM_SCALE);
    r.d  = (int32_t)(((int64_t)b.b * a.c + (int64_t)b.d * a.d) / TRANSFORM_SCALE);
    r.tx = (int32_t)(((int64_t)b.a * a.tx + (int64_t)b.c * a.ty) / TRANSFORM_SCALE) + b.tx;
    r.ty = (int32_t)(((int64_t)b.b * a.tx + (int64_t)b.d * a.ty) / TRANSFORM_SCALE) + b.ty;
    return r;
}

/* Apply transform to an integer point */
static inline gui_pointi_t transform_apply(gui_transform_t t, int32_t px, int32_t py) {
    gui_pointi_t out;
    out.x = (int32_t)(((int64_t)t.a * px + (int64_t)t.c * py) / TRANSFORM_SCALE) + t.tx;
    out.y = (int32_t)(((int64_t)t.b * px + (int64_t)t.d * py) / TRANSFORM_SCALE) + t.ty;
    return out;
}

/* Apply transform to an integer point (compat alias) */
static inline gui_pointi_t transform_apply_i(gui_transform_t t, int px, int py) {
    return transform_apply(t, px, py);
}

/* Transform all four corners of rect, return their AABB in the new space. */
static inline gui_rect_t transform_apply_rect(gui_transform_t t, gui_rect_t r) {
    gui_pointi_t tl = transform_apply_i(t, r.x,           r.y);
    gui_pointi_t tr = transform_apply_i(t, r.x + r.width, r.y);
    gui_pointi_t bl = transform_apply_i(t, r.x,           r.y + r.height);
    gui_pointi_t br = transform_apply_i(t, r.x + r.width, r.y + r.height);

    int min_x = tl.x, max_x = tl.x;
    int min_y = tl.y, max_y = tl.y;

    #define _EXPAND(p) \
        if ((p).x < min_x) min_x = (p).x; \
        if ((p).x > max_x) max_x = (p).x; \
        if ((p).y < min_y) min_y = (p).y; \
        if ((p).y > max_y) max_y = (p).y;
    _EXPAND(tr) _EXPAND(bl) _EXPAND(br)
    #undef _EXPAND

    return rect_make(min_x, min_y, max_x - min_x, max_y - min_y);
}

/* Simple invert (only valid for non-sheared transforms: rotation not used yet) */
static inline gui_transform_t transform_invert_simple(gui_transform_t t) {
    float inv_a = (t.a != 0.0f) ? 1.0f / t.a : 0.0f;
    float inv_d = (t.d != 0.0f) ? 1.0f / t.d : 0.0f;
    gui_transform_t r = transform_identity();
    r.a  = inv_a;
    r.d  = inv_d;
    r.tx = -t.tx * inv_a;
    r.ty = -t.ty * inv_d;
    return r;
}

/* Coordinate space helpers — named so intent is clear at the call-site. */
#define SCREEN_TO_WORLD(cam_transform, sx, sy) \
    transform_apply(transform_invert_simple(cam_transform), (float)(sx), (float)(sy))

#define WORLD_TO_SCREEN(cam_transform, wx, wy) \
    transform_apply((cam_transform), (float)(wx), (float)(wy))

#endif /* GUI_TRANSFORM_H */
