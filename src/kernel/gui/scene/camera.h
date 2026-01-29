/*
 * gui/scene/camera.h  —  Infinite Canvas Camera
 *
 * Zoom is stored as a fixed-point integer (zoom_fp) scaled by
 * CAMERA_ZOOM_SCALE = 1024, so zoom 1.0 = 1024, 0.5 = 512, 2.0 = 2048.
 * This avoids SSE float-return errors in the -mno-sse kernel build.
 *
 * Pan coordinates are also stored as fixed-point (pos_fp) scaled by 256
 * (i.e. 8 fractional bits), giving sub-pixel precision without floats.
 *
 * External API uses plain int for all values; helper macros convert.
 */
#ifndef GUI_CAMERA_H
#define GUI_CAMERA_H

#include <stdint.h>
#include <stdbool.h>
#include "../math/rect.h"

/* --------------------------------------------------------------------------
 * Fixed-point scales
 * -------------------------------------------------------------------------- */

#define CAMERA_ZOOM_SCALE  1024          /* fp unit for zoom              */
#define CAMERA_POS_SCALE   256           /* fp unit for position (8-bit)  */

#define CAMERA_ZOOM_MIN_FP  (CAMERA_ZOOM_SCALE / 10)   /* 0.1× */
#define CAMERA_ZOOM_MAX_FP  (CAMERA_ZOOM_SCALE * 8)    /* 8.0× */
#define CAMERA_ZOOM_DEF_FP  (CAMERA_ZOOM_SCALE)        /* 1.0× */

/* Friction: vel decays to ~85% each frame (stored as 870/1024 ≈ 0.85). */
#define CAMERA_FRICTION_NUM  870
#define CAMERA_FRICTION_DEN  1024

#define CAMERA_ZOOM_STEP_FP  82          /* ~0.08 zoom per key press */

/* --------------------------------------------------------------------------
 * Type
 * -------------------------------------------------------------------------- */

typedef struct {
    int32_t  pos_x_fp;     /* world x << 8  (CAMERA_POS_SCALE)    */
    int32_t  pos_y_fp;     /* world y << 8                        */
    int32_t  zoom_fp;      /* zoom × CAMERA_ZOOM_SCALE            */
    int32_t  vel_x_fp;     /* velocity, same scale as pos          */
    int32_t  vel_y_fp;

    int      screen_w;
    int      screen_h;

    bool     dirty;        /* view_transform must be rebuilt       */
} camera_t;

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

camera_t *camera_create(int screen_w, int screen_h);
void      camera_destroy(camera_t *cam);

/* --------------------------------------------------------------------------
 * Navigation
 * -------------------------------------------------------------------------- */

/* dx/dy in world pixels (integer). */
void camera_pan(camera_t *cam, int dx, int dy);
void camera_center_on(camera_t *cam, int world_x, int world_y);

/* new_zoom_fp = desired zoom in fixed-point (e.g. CAMERA_ZOOM_SCALE = 1.0) */
void camera_zoom_at(camera_t *cam, int new_zoom_fp, int pivot_sx, int pivot_sy);
void camera_reset(camera_t *cam);
void camera_fit(camera_t *cam, const gui_rect_t *rects, uint32_t count);

/* Decay inertia — call once per frame. */
void camera_update(camera_t *cam);

/* --------------------------------------------------------------------------
 * Coordinate conversions (integer, no floats, no SSE)
 * -------------------------------------------------------------------------- */

/*
 * World → Screen:
 *   screen_x = (world_x - pos_x) * zoom_fp / CAMERA_ZOOM_SCALE
 *   where pos_x = pos_x_fp / CAMERA_POS_SCALE
 */
static inline int camera_world_to_screen_x(const camera_t *c, int wx) {
    int64_t diff = (int64_t)(wx * CAMERA_POS_SCALE) - (int64_t)c->pos_x_fp;
    return (int)((diff * c->zoom_fp) / ((int64_t)CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE));
}
static inline int camera_world_to_screen_y(const camera_t *c, int wy) {
    int64_t diff = (int64_t)(wy * CAMERA_POS_SCALE) - (int64_t)c->pos_y_fp;
    return (int)((diff * c->zoom_fp) / ((int64_t)CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE));
}

/* Screen → World */
static inline int camera_screen_to_world_x(const camera_t *c, int sx) {
    int64_t v = ((int64_t)sx * CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE) / c->zoom_fp;
    return (int)((v + c->pos_x_fp) / CAMERA_POS_SCALE);
}
static inline int camera_screen_to_world_y(const camera_t *c, int sy) {
    int64_t v = ((int64_t)sy * CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE) / c->zoom_fp;
    return (int)((v + c->pos_y_fp) / CAMERA_POS_SCALE);
}

/* World rect → screen rect */
static inline gui_rect_t camera_world_rect_to_screen(const camera_t *c, gui_rect_t wr) {
    int sx  = camera_world_to_screen_x(c, wr.x);
    int sy  = camera_world_to_screen_y(c, wr.y);
    int ex  = camera_world_to_screen_x(c, wr.x + wr.width);
    int ey  = camera_world_to_screen_y(c, wr.y + wr.height);
    return rect_make(sx, sy, ex - sx, ey - sy);
}

/* Viewport frustum in world space */
static inline gui_rect_t camera_viewport_in_world(const camera_t *c) {
    int x = camera_screen_to_world_x(c, 0);
    int y = camera_screen_to_world_y(c, 0);
    int x2 = camera_screen_to_world_x(c, c->screen_w);
    int y2 = camera_screen_to_world_y(c, c->screen_h);
    return rect_make(x, y, x2 - x, y2 - y);
}

/* Scaled node size helper: node_dim × zoom_fp / CAMERA_ZOOM_SCALE */
static inline int camera_scale(const camera_t *c, int world_dim) {
    return (int)((int64_t)world_dim * c->zoom_fp / CAMERA_ZOOM_SCALE);
}

#endif /* GUI_CAMERA_H */
