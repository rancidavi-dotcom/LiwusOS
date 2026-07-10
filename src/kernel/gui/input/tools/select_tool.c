/*
 * gui/input/tools/select_tool.c
 *
 * BUGFIX: Never cache node pointers across frames. A node can be destroyed
 * at any time by the window manager (WIN_CLOSE).  Instead of storing
 * hovered_node / selected_node we re-hit-test every event.  This is safe
 * because node_hit_test is read-only and fast on our small scene graphs.
 */
#include "select_tool.h"
#include "../../render/compositor.h"
#include "kheap.h"
#include "string.h"

typedef struct {
    bool    mouse_down;
    int     down_mx;
    int     down_my;
} select_state_t;

/* Return the topmost hit node, but never the canvas itself. */
static node_t *safe_hit(tool_t *self, int sx, int sy) {
    node_t *hit = node_hit_test(self->scene_root, sx, sy);
    if (hit && hit->type == NODE_CANVAS) hit = NULL;
    return hit;
}

static bool select_event(tool_t *self, const gui_event_t *e) {
    select_state_t *s = (select_state_t *)self->userdata;

    if (e->type == GUI_EVENT_MOUSE_MOVE) {
        node_t *hit = safe_hit(self, e->mouse.x, e->mouse.y);

        extern compositor_t *g_compositor;
        if (g_compositor) {
            compositor_set_cursor(g_compositor,
                (hit && hit->type == NODE_BUTTON) ? CURSOR_HAND : CURSOR_ARROW);
        }

        if (hit && hit->vtable && hit->vtable->on_event) {
            hit->vtable->on_event(hit, e);
        }
        return false;
    }

    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        s->mouse_down = true;
        s->down_mx    = e->mouse.x;
        s->down_my    = e->mouse.y;

        node_t *hit = safe_hit(self, e->mouse.x, e->mouse.y);
        if (hit && hit->vtable && hit->vtable->on_event) {
            return hit->vtable->on_event(hit, e);
        }
        return false;
    }

    if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        s->mouse_down = false;

        node_t *hit = safe_hit(self, e->mouse.x, e->mouse.y);
        if (hit && hit->vtable && hit->vtable->on_event) {
            return hit->vtable->on_event(hit, e);
        }
        return false;
    }

    return false;
}

static void select_destroy(tool_t *self) {
    if (self->userdata) { kfree(self->userdata); self->userdata = NULL; }
    kfree(self);
}

static const tool_vtable_t select_vtable = {
    .name          = "SelectTool",
    .on_activate   = NULL,
    .on_deactivate = NULL,
    .on_event      = select_event,
    .destroy       = select_destroy,
};

tool_t *select_tool_create(camera_t *camera, node_t *scene_root) {
    tool_t *t = (tool_t *)kmalloc(sizeof(tool_t));
    if (!t) return NULL;
    memset(t, 0, sizeof(tool_t));

    select_state_t *s = (select_state_t *)kmalloc(sizeof(select_state_t));
    if (!s) { kfree(t); return NULL; }
    memset(s, 0, sizeof(select_state_t));

    t->userdata   = s;
    t->camera     = camera;
    t->scene_root = scene_root;
    t->vtable     = &select_vtable;
    t->active     = false;

    return t;
}



