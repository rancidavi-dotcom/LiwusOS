/*
 * gui/input/tools/move_tool.c
 */
#include "move_tool.h"
#include "kheap.h"
#include "string.h"

typedef struct {
    node_t *dragged_node;
    bool    dragging;
    int     start_mx;
    int     start_my;
    int     start_node_x;
    int     start_node_y;
} move_state_t;

/* ---- event handler ---- */

static bool move_event(tool_t *self, const gui_event_t *e) {
    move_state_t *s = (move_state_t *)self->userdata;
    camera_t     *cam = self->camera;

    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        node_t *hit = node_hit_test(self->scene_root, e->mouse.x, e->mouse.y);
        /* Walk up to find a window or terminal */
        node_t *win = hit;
        while (win && win->type != NODE_WINDOW && win->type != NODE_TERMINAL) {
            win = win->parent;
        }

        if (win) {
            int title_h_screen = camera_scale(cam, 24);
            int title_bottom = win->screen_bounds.y + title_h_screen;
            
            /* Check if within title bar height */
            if (e->mouse.y >= win->screen_bounds.y && e->mouse.y <= title_bottom) {
                /* Check if it's the close button area (roughly 8 to 20 px from left) */
                int close_left = win->screen_bounds.x + camera_scale(cam, 8);
                int close_right = close_left + camera_scale(cam, 12);
                if (e->mouse.x >= close_left && e->mouse.x <= close_right) {
                    /* Clicked close button, do not start drag */
                    return false;
                }
                
                s->dragging     = true;
                s->dragged_node = win;
                s->start_mx     = e->mouse.x;
                s->start_my     = e->mouse.y;
                s->start_node_x = win->local_x;
                s->start_node_y = win->local_y;
                return true; /* Consome o evento */
            }
        }
    }

    if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        if (s->dragging) {
            s->dragging = false;
            s->dragged_node = NULL;
            return true;
        }
    }

    if (e->type == GUI_EVENT_MOUSE_MOVE) {
        if (s->dragging && s->dragged_node) {
            node_t *sel = s->dragged_node;
            int ddx = e->mouse.x - s->start_mx;
            int ddy = e->mouse.y - s->start_my;

            /* World delta = screen delta / zoom */
            int world_dx = (int)((int64_t)ddx * CAMERA_ZOOM_SCALE / cam->zoom_fp);
            int world_dy = (int)((int64_t)ddy * CAMERA_ZOOM_SCALE / cam->zoom_fp);

            node_set_position(sel, s->start_node_x + world_dx, s->start_node_y + world_dy);
            return true;
        }
    }

    return false;
}

static void move_destroy(tool_t *self) {
    if (self->userdata) { kfree(self->userdata); self->userdata = NULL; }
    kfree(self);
}

static const tool_vtable_t move_vtable = {
    .name          = "MoveTool",
    .on_activate   = NULL,
    .on_deactivate = NULL,
    .on_event      = move_event,
    .destroy       = move_destroy,
};

tool_t *move_tool_create(camera_t *camera, node_t *scene_root, tool_t *select_tool) {
    tool_t *t = (tool_t *)kmalloc(sizeof(tool_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(tool_t));

    move_state_t *s = (move_state_t *)kmalloc(sizeof(move_state_t));
    if (!s) { kfree(t); return NULL; }
    memset(s, 0, sizeof(move_state_t));

    t->vtable      = &move_vtable;
    t->camera      = camera;
    t->scene_root  = scene_root;
    t->userdata    = s;
    t->active      = false;
    return t;
}
