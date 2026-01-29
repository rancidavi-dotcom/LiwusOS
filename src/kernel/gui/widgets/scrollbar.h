/*
 * gui/widgets/scrollbar.h
 *
 * Chicago95 vertical scrollbar widget.
 * Fixed-point: value 0..1000 (SB_SCALE).
 */
#ifndef GUI_SCROLLBAR_H
#define GUI_SCROLLBAR_H

#include "../scene/node.h"
#include "../core/event_bus.h"

#define SB_SCALE 1000

typedef void (*scrollbar_change_cb_t)(node_t *sb, void *userdata, int value);

node_t *scrollbar_create(const char *name, int x, int y, int w, int h);
void scrollbar_set_value(node_t *sb, int value);          // 0 .. SB_SCALE
int scrollbar_get_value(node_t *sb);
void scrollbar_set_range(node_t *sb, int min, int max);   // thumb size ratio 0..SB_SCALE
void scrollbar_set_on_change(node_t *sb, scrollbar_change_cb_t cb, void *userdata);

#endif /* GUI_SCROLLBAR_H */