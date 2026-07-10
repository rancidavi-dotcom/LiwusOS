/*
 * gui/input/tools/select_tool.c
 */
#include "select_tool.h"
#include "../../render/compositor.h"
#include "kheap.h"
#include "string.h"

typedef struct {
    node_t *selected_node;
    node_t *hovered_node;
    bool    mouse_down;
    int     down_mx;
    int     down_my;
} select_state_t;

/* ---- event handler ---- */

static bool select_event(tool_t *self, const gui_event_t *e) {
    select_state_t *s = (select_state_t *)self->userdata;

    if (e->type == GUI_EVENT_MOUSE_MOVE) {
        node_t *hit = node_hit_test(self->scene_root, e->mouse.x, e->mouse.y);
        
        extern compositor_t *g_compositor;
        
        if (hit != s->hovered_node) {
            /* Dispatch leave to old */
            if (s->hovered_node && s->hovered_node->vtable && s->hovered_node->vtable->on_event) {
                gui_event_t leave = *e;
                leave.type = GUI_EVENT_MOUSE_LEAVE;
                s->hovered_node->vtable->on_event(s->hovered_node, &leave);
            }
            s->hovered_node = hit;
        }

        /* Dispatch move to current */
        if (s->hovered_node && s->hovered_node->vtable && s->hovered_node->vtable->on_event) {
            s->hovered_node->vtable->on_event(s->hovered_node, e);
        }
        
        /* Change cursor if hovered node is interactive button etc. */
        if (g_compositor) {
            if (hit && hit->type == NODE_BUTTON) {
                compositor_set_cursor(g_compositor, CURSOR_HAND);
            } else {
                compositor_set_cursor(g_compositor, CURSOR_ARROW);
            }
        }
        
        return false;
    }

    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        s->mouse_down = true;
        s->down_mx = e->mouse.x;
        s->down_my = e->mouse.y;

        /* Hit-test (procura qual node foi clicado, top-down z-order) */
        if (self->scene_root) {
            node_t *hit = node_hit_test(self->scene_root, e->mouse.x, e->mouse.y);
            
            /* Se clicou no fundo (CANVAS), desmarca */
            if (hit && hit->type == NODE_CANVAS) hit = NULL;

            if (s->selected_node != hit) {
                s->selected_node = hit;
            }
            
            if (hit && hit->vtable && hit->vtable->on_event) {
                return hit->vtable->on_event(hit, e);
            }
        }
        return false; /* Let other tools see the down event */
    }

    if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        s->mouse_down = false;
        
        if (self->scene_root) {
            node_t *hit = node_hit_test(self->scene_root, e->mouse.x, e->mouse.y);
            if (hit && hit->type == NODE_CANVAS) hit = NULL;
            
            if (hit && hit->vtable && hit->vtable->on_event) {
                return hit->vtable->on_event(hit, e);
            }
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

    t->vtable      = &select_vtable;
    t->camera      = camera;
    t->scene_root  = scene_root;
    t->userdata    = s;
    t->active      = false;
    return t;
}

node_t *select_tool_get_selection(tool_t *t) {
    if (!t || t->vtable != &select_vtable) return NULL;
    select_state_t *s = (select_state_t *)t->userdata;
    return s->selected_node;
}
