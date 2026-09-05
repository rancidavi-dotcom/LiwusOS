/*
 * gui/window/focus_manager.c
 */
#include "focus_manager.h"
#include "kheap.h"
#include "string.h"

struct focus_manager {
    gui_event_bus_t *bus;
    node_t          *scene_root;
    node_t          *focused_node;
    uint32_t         sub_id;
};

static void focus_bus_handler(const gui_event_t *event, void *userdata) {
    focus_manager_t *fm = (focus_manager_t *)userdata;

    if (event->type == GUI_EVENT_MOUSE_DOWN && event->mouse.button == 1) {
        node_t *hit = node_hit_test(fm->scene_root, event->mouse.x, event->mouse.y);
        if (hit && hit->type == NODE_CANVAS) hit = NULL;
        
        /* Walk up to find a window to focus */
        node_t *win = hit;
        while (win && win->type != NODE_WINDOW) {
            win = win->parent;
        }
        
        focus_manager_set_focus(fm, win ? win : NULL);
        /* Do NOT consume — let tools/widgets also process the click */
        return;
    }

    /* The TAB key ALWAYS toggles the app launcher — before any window sees
     * the key. This ensures Tab reliably opens the launcher so the user can
     * open as many apps as they want. */
    if (event->type == GUI_EVENT_KEY_DOWN &&
        event->key.scancode == 0x0F /* Tab */) {
        extern void app_registry_toggle_launcher(void);
        app_registry_toggle_launcher();
        event_stop_propagation(fm->bus);
        return;
    }

    /* Key events: forward to focused window first.
     * Only call stop_propagation if the focused window consumed the key. */
    if (event->type == GUI_EVENT_KEY_DOWN ||
        event->type == GUI_EVENT_KEY_UP  ||
        event->type == GUI_EVENT_KEY_CHAR) {

        if (fm->focused_node &&
            fm->focused_node->vtable &&
            fm->focused_node->vtable->on_event) {

            bool consumed = fm->focused_node->vtable->on_event(fm->focused_node, event);
            if (consumed) {
                /* Stop this key from reaching the canvas/pan-tool */
                event_stop_propagation(fm->bus);
                return;
            }
        }
    }
}

focus_manager_t *focus_manager_create(gui_event_bus_t *bus, node_t *root) {
    focus_manager_t *fm = (focus_manager_t *)kmalloc(sizeof(focus_manager_t));
    if (!fm) return NULL;
    memset(fm, 0, sizeof(focus_manager_t));

    fm->bus = bus;
    fm->scene_root = root;
    fm->focused_node = NULL;

    /* Subscribe to ALL events so we intercept keys before normal tree dispatch */
    fm->sub_id = event_bus_subscribe(bus, GUI_EVENT_NONE, focus_bus_handler, fm);

    return fm;
}

void focus_manager_destroy(focus_manager_t *fm) {
    if (!fm) return;
    if (fm->bus && fm->sub_id) {
        event_bus_unsubscribe(fm->bus, fm->sub_id);
    }
    kfree(fm);
}

node_t *focus_manager_get_focus(focus_manager_t *fm) {
    if (!fm) return NULL;
    return fm->focused_node;
}

void focus_manager_set_focus(focus_manager_t *fm, node_t *node) {
    if (!fm) return;
    if (fm->focused_node == node) return;

    node_t *old = fm->focused_node;
    fm->focused_node = node;

    gui_event_t ev;
    memset(&ev, 0, sizeof(ev));

    if (old) {
        ev.type = GUI_EVENT_WIN_BLUR;
        ev.generic.a = (uint64_t)old;
        if (old->vtable && old->vtable->on_event) old->vtable->on_event(old, &ev);
        node_mark_dirty(old, NODE_DIRTY_PAINT);
    }

    if (node) {
        ev.type = GUI_EVENT_WIN_FOCUS;
        ev.generic.a = (uint64_t)node;
        if (node->vtable && node->vtable->on_event) node->vtable->on_event(node, &ev);
        node_mark_dirty(node, NODE_DIRTY_PAINT);
    }
}

void focus_manager_focus_next(focus_manager_t *fm) {
    if (!fm || !fm->scene_root) return;
    /* TODO: Implement focus traversal by walking the tree looking for interactive nodes */
}
