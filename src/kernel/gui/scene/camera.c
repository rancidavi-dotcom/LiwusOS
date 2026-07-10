/*
 * gui/scene/camera.c  —  Fixed-point integer camera (no floats, no SSE)
 */
#include "camera.h"
#include "kheap.h"
#include "string.h"

static inline int32_t iclampcam(int32_t v, int32_t lo, int32_t hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

camera_t *camera_create(int screen_w, int screen_h) {
    camera_t *cam = (camera_t *)kmalloc(sizeof(camera_t));
    if (!cam) return NULL;
    memset(cam, 0, sizeof(camera_t));
    cam->zoom_fp  = CAMERA_ZOOM_DEF_FP;
    cam->screen_w = screen_w;
    cam->screen_h = screen_h;
    cam->dirty    = true;
    return cam;
}

void camera_destroy(camera_t *cam) {
    if (cam) kfree(cam);
}

void camera_pan(camera_t *cam, int dx, int dy) {
    cam->pos_x_fp += dx * CAMERA_POS_SCALE;
    cam->pos_y_fp += dy * CAMERA_POS_SCALE;
    cam->vel_x_fp  = dx * CAMERA_POS_SCALE;
    cam->vel_y_fp  = dy * CAMERA_POS_SCALE;
    cam->dirty     = true;
}

void camera_center_on(camera_t *cam, int wx, int wy) {
    /* pos_x = wx - screen_w/2 / zoom */
    cam->pos_x_fp = (int32_t)((int64_t)wx * CAMERA_POS_SCALE
                    - ((int64_t)cam->screen_w * CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE)
                      / (2 * cam->zoom_fp));
    cam->pos_y_fp = (int32_t)((int64_t)wy * CAMERA_POS_SCALE
                    - ((int64_t)cam->screen_h * CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE)
                      / (2 * cam->zoom_fp));
    cam->dirty = true;
}

void camera_zoom_at(camera_t *cam, int new_zoom_fp, int pivot_sx, int pivot_sy) {
    new_zoom_fp = iclampcam(new_zoom_fp, CAMERA_ZOOM_MIN_FP, CAMERA_ZOOM_MAX_FP);
    if (new_zoom_fp == cam->zoom_fp) return;

    /* World point under pivot (screen coords) */
    int wpx = camera_screen_to_world_x(cam, pivot_sx);
    int wpy = camera_screen_to_world_y(cam, pivot_sy);

    cam->zoom_fp = new_zoom_fp;
    cam->dirty   = true;

    /* Reposition so the world point maps back to the same screen pixel */
    cam->pos_x_fp = (int32_t)((int64_t)wpx * CAMERA_POS_SCALE
                    - ((int64_t)pivot_sx * CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE)
                      / cam->zoom_fp);
    cam->pos_y_fp = (int32_t)((int64_t)wpy * CAMERA_POS_SCALE
                    - ((int64_t)pivot_sy * CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE)
                      / cam->zoom_fp);
}

void camera_reset(camera_t *cam) {
    cam->pos_x_fp = 0;
    cam->pos_y_fp = 0;
    cam->zoom_fp  = CAMERA_ZOOM_DEF_FP;
    cam->vel_x_fp = 0;
    cam->vel_y_fp = 0;
    cam->dirty    = true;
}

void camera_fit(camera_t *cam, const gui_rect_t *rects, uint32_t count) {
    if (!rects || count == 0) return;

    int min_x =  0x7FFFFFFF, min_y =  0x7FFFFFFF;
    int max_x = -0x7FFFFFFF, max_y = -0x7FFFFFFF;

    for (uint32_t i = 0; i < count; i++) {
        if (rects[i].x < min_x) min_x = rects[i].x;
        if (rects[i].y < min_y) min_y = rects[i].y;
        int rx = rects[i].x + rects[i].width;
        int ry = rects[i].y + rects[i].height;
        if (rx > max_x) max_x = rx;
        if (ry > max_y) max_y = ry;
    }

    int bounds_w = max_x - min_x + 80;
    int bounds_h = max_y - min_y + 80;
    if (bounds_w < 1) bounds_w = 1;
    if (bounds_h < 1) bounds_h = 1;

    int32_t zoom_x = (int32_t)((int64_t)cam->screen_w * CAMERA_ZOOM_SCALE / bounds_w);
    int32_t zoom_y = (int32_t)((int64_t)cam->screen_h * CAMERA_ZOOM_SCALE / bounds_h);
    int32_t z = zoom_x < zoom_y ? zoom_x : zoom_y;

    cam->zoom_fp = iclampcam(z, CAMERA_ZOOM_MIN_FP, CAMERA_ZOOM_MAX_FP);
    camera_center_on(cam, (min_x + max_x) / 2, (min_y + max_y) / 2);
}

void camera_update(camera_t *cam) {
    int thresh = CAMERA_POS_SCALE / 4;   /* < 0.25 world px */
    if (cam->vel_x_fp > thresh || cam->vel_x_fp < -thresh ||
        cam->vel_y_fp > thresh || cam->vel_y_fp < -thresh) {
        cam->vel_x_fp = (int32_t)((int64_t)cam->vel_x_fp
                                  * CAMERA_FRICTION_NUM / CAMERA_FRICTION_DEN);
        cam->vel_y_fp = (int32_t)((int64_t)cam->vel_y_fp
                                  * CAMERA_FRICTION_NUM / CAMERA_FRICTION_DEN);
        cam->pos_x_fp += cam->vel_x_fp;
        cam->pos_y_fp += cam->vel_y_fp;
        cam->dirty = true;
    } else {
        cam->vel_x_fp = cam->vel_y_fp = 0;
    }
}
