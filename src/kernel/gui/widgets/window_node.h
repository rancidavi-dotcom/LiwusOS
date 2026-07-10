/*
 * gui/widgets/window_node.h
 */
#ifndef GUI_WINDOW_NODE_H
#define GUI_WINDOW_NODE_H

#include "../scene/node.h"

node_t *window_node_create(const char *name, int x, int y, int w, int h, const char *title);

void window_node_set_title(node_t *win, const char *title);
void window_node_set_pid(node_t *win, int pid);

#endif /* GUI_WINDOW_NODE_H */
