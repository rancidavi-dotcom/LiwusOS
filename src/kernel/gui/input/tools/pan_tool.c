/*
 * gui/input/tools/pan_tool.c
 */
#include "pan_tool.h"
#include "kheap.h"
#include "string.h"

/* Scancode constants */
#define SC_PLUS     0x0D   /* = / + */
#define SC_MINUS    0x0C   /* - / _ */
#define SC_H        0x23   /* H — home */
#define SC_F        0x21   /* F — fit all */

typedef struct {
    bool  dragging;
    int   drag_start_mx;
    int   drag_start_my;
    int32_t drag_start_px;
    int32_t drag_start_py;
} pan_state_t;

/* ---- event handler ---- */

static bool pan_event(tool_t *self, const gui_event_t *e) {
    pan_state_t *s = (pan_state_t *)self->userdata;
    camera_t    *cam = self->camera;

    switch (e->type) {

    /* --- Zoom with keyboard --- */
    case GUI_EVENT_KEY_DOWN: {
        uint8_t sc = e->key.scancode;
        if (sc == SC_PLUS) {
            camera_zoom_at(cam, cam->zoom_fp + CAMERA_ZOOM_STEP_FP,
                           cam->screen_w / 2, cam->screen_h / 2);
            return true;
        }
        if (sc == SC_MINUS) {
            camera_zoom_at(cam, cam->zoom_fp - CAMERA_ZOOM_STEP_FP,
                           cam->screen_w / 2, cam->screen_h / 2);
            return true;
        }
        if (sc == SC_H) {
            camera_reset(cam);
            return true;
        }
        if (sc == SC_F && self->scene_root) {
            /* Collect all child rects and fit */
            gui_rect_t rects[64];
            uint32_t count = 0;
            for (uint32_t i = 0; i < self->scene_root->child_count && count < 64; i++) {
                node_t *n = self->scene_root->children[i];
                rects[count++] = rect_make(n->local_x, n->local_y, n->width, n->height);
            }
            camera_fit(cam, rects, count);
            return true;
        }
        
        /* Panning via keyboard (WASD and Arrows) */
        int pan_step = 20;
        if (sc == 0x11 || sc == 0x48) { camera_pan(cam, 0, -pan_step); return true; } // W or Up
        if (sc == 0x1F || sc == 0x50) { camera_pan(cam, 0, pan_step); return true; } // S or Down
        if (sc == 0x1E || sc == 0x4B) { camera_pan(cam, -pan_step, 0); return true; } // A or Left
        if (sc == 0x20 || sc == 0x4D) { camera_pan(cam, pan_step, 0); return true; } // D or Right
        
        break;
    }

    /* --- Pan: right mouse button drag --- */
    case GUI_EVENT_MOUSE_DOWN:
        if (e->mouse.button == 2) {  /* RMB */
            s->dragging       = true;
            s->drag_start_mx  = e->mouse.x;
            s->drag_start_my  = e->mouse.y;
            s->drag_start_px  = cam->pos_x_fp;
            s->drag_start_py  = cam->pos_y_fp;
            return true;
        }
        break;

    case GUI_EVENT_MOUSE_UP:
        if (e->mouse.button == 2 && s->dragging) {
            s->dragging = false;
            return true;
        }
        break;

    case GUI_EVENT_MOUSE_MOVE:
        if (s->dragging) {
            int ddx = e->mouse.x - s->drag_start_mx;
            int ddy = e->mouse.y - s->drag_start_my;
            /* World delta = screen delta / zoom */
            int world_dx = (int)(-(int64_t)ddx * CAMERA_ZOOM_SCALE / cam->zoom_fp);
            int world_dy = (int)(-(int64_t)ddy * CAMERA_ZOOM_SCALE / cam->zoom_fp);
            cam->pos_x_fp = s->drag_start_px + world_dx * CAMERA_POS_SCALE;
            cam->pos_y_fp = s->drag_start_py + world_dy * CAMERA_POS_SCALE;
            cam->dirty    = true;
            return true;
        }
        break;

    /* --- Scroll → zoom at cursor --- */
    case GUI_EVENT_MOUSE_SCROLL: {
        int step = e->mouse.dy > 0 ? CAMERA_ZOOM_STEP_FP : -CAMERA_ZOOM_STEP_FP;
        camera_zoom_at(cam, cam->zoom_fp + step, e->mouse.x, e->mouse.y);
        return true;
    }

    default: break;
    }
    return false;
}

static void pan_destroy(tool_t *self) {
    if (self->userdata) { kfree(self->userdata); self->userdata = NULL; }
    kfree(self);
}

static const tool_vtable_t pan_vtable = {
    .name          = "PanTool",
    .on_activate   = NULL,
    .on_deactivate = NULL,
    .on_event      = pan_event,
    .destroy       = pan_destroy,
};

tool_t *pan_tool_create(camera_t *camera, node_t *scene_root) {
    tool_t *t = (tool_t *)kmalloc(sizeof(tool_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(tool_t));

    pan_state_t *s = (pan_state_t *)kmalloc(sizeof(pan_state_t));
    if (!s) { kfree(t); return NULL; }
    memset(s, 0, sizeof(pan_state_t));

    t->vtable      = &pan_vtable;
    t->camera      = camera;
    t->scene_root  = scene_root;
    t->userdata    = s;
    t->active      = true;   /* PanTool always active (background handler) */
    return t;
}
