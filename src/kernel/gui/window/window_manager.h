/*
 * gui/window/window_manager.h
 *
 * Manages window lifecycle, z-order, and workspace layers.
 */
#ifndef GUI_WINDOW_MANAGER_H
#define GUI_WINDOW_MANAGER_H

#include "../scene/node.h"
#include "../core/event_bus.h"

typedef struct window_manager window_manager_t;

window_manager_t *window_manager_create(gui_event_bus_t *bus, node_t *root);
void window_manager_destroy(window_manager_t *wm);

/* Brings a node to the front of its siblings */
void window_manager_bring_to_front(node_t *node);

#endif
