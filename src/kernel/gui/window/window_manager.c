/*
 * gui/window/window_manager.c
 */
#include "window_manager.h"
#include "kheap.h"
#include "string.h"
#include "focus_manager.h"

struct window_manager {
    gui_event_bus_t *bus;
    node_t          *scene_root;
    uint32_t         sub_id;
};

static void wm_bus_handler(const gui_event_t *event, void *userdata) {
    (void)userdata;
    if (event->type == GUI_EVENT_WIN_FOCUS) {
        node_t *focused = (node_t *)event->generic.a;
        if (focused) {
            window_manager_bring_to_front(focused);
        }
    } else if (event->type == GUI_EVENT_WIN_CLOSE) {
        node_t *closing = (node_t *)event->generic.a;
        if (closing) {
            // Unfocus if focused
            extern focus_manager_t *g_focus_manager;
            if (g_focus_manager && focus_manager_get_focus(g_focus_manager) == closing) {
                focus_manager_set_focus(g_focus_manager, NULL);
            }
            
            // Mark parent dirty before removing
            if (closing->parent) {
                node_mark_dirty(closing->parent, NODE_DIRTY_PAINT);
                node_remove_child(closing->parent, closing);
            }
            
            // Actually destroy the node
            node_destroy(closing);
        }
    }
}

window_manager_t *window_manager_create(gui_event_bus_t *bus, node_t *root) {
    window_manager_t *wm = (window_manager_t *)kmalloc(sizeof(window_manager_t));
    if (!wm) return NULL;
    memset(wm, 0, sizeof(window_manager_t));

    wm->bus = bus;
    wm->scene_root = root;
    wm->sub_id = event_bus_subscribe(bus, GUI_EVENT_NONE, wm_bus_handler, wm);

    return wm;
}

void window_manager_destroy(window_manager_t *wm) {
    if (!wm) return;
    if (wm->bus && wm->sub_id) {
        event_bus_unsubscribe(wm->bus, wm->sub_id);
    }
    kfree(wm);
}

void window_manager_bring_to_front(node_t *node) {
    if (!node || !node->parent) return;
    
    node_t *parent = node->parent;
    uint32_t idx = 0;
    bool found = false;
    
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == node) {
            idx = i;
            found = true;
            break;
        }
    }
    
    if (found && idx < parent->child_count - 1) {
        /* Shift remaining children down */
        for (uint32_t i = idx; i < parent->child_count - 1; i++) {
            parent->children[i] = parent->children[i + 1];
        }
        /* Place node at the end (top-most in z-order) */
        parent->children[parent->child_count - 1] = node;
        
        node_mark_dirty(parent, NODE_DIRTY_PAINT); /* trigger repaint */
    }
}
