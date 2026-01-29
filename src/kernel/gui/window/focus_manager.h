/*
 * gui/window/focus_manager.h
 *
 * Manages which node currently has keyboard focus.
 * Routes keyboard events from the EventBus to the focused node.
 */
#ifndef GUI_FOCUS_MANAGER_H
#define GUI_FOCUS_MANAGER_H

#include <stdbool.h>
#include "../scene/node.h"
#include "../core/event_bus.h"

typedef struct focus_manager focus_manager_t;

/* Create and initialize the focus manager. Subscribes to keyboard events on the bus. */
focus_manager_t *focus_manager_create(gui_event_bus_t *bus, node_t *root);

/* Destroy the focus manager */
void focus_manager_destroy(focus_manager_t *fm);

/* Get the currently focused node */
node_t *focus_manager_get_focus(focus_manager_t *fm);

/* Set the focus to a specific node (or NULL to clear focus) */
void focus_manager_set_focus(focus_manager_t *fm, node_t *node);

/* Move focus to the next interactive node (Tab behavior) */
void focus_manager_focus_next(focus_manager_t *fm);

#endif /* GUI_FOCUS_MANAGER_H */
