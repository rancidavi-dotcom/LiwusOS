/*
 * gui/widgets/window_node.h
 */
#ifndef GUI_WINDOW_NODE_H
#define GUI_WINDOW_NODE_H

#include "../scene/node.h"

node_t *window_node_create(const char *name, int x, int y, int w, int h, const char *title);

void window_node_set_title(node_t *win, const char *title);
void window_node_set_pid(node_t *win, int pid);

/* Returns the current window title (or NULL). Used by the taskbar. */
const char *window_node_get_title(node_t *win);

/* Install per-instance key handlers — replaces vtable subclassing */
void window_node_set_key_handler(node_t *win,
    bool (*key_down_cb)(node_t *, uint8_t, void *),
    bool (*key_char_cb)(node_t *, char, void *),
    void *userctx);

#endif /* GUI_WINDOW_NODE_H */
