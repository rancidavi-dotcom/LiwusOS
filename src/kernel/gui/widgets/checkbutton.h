/*
 * gui/widgets/checkbutton.h
 *
 * Chicago95 checkbox and radio button widgets.
 */
#ifndef GUI_CHECKBUTTON_H
#define GUI_CHECKBUTTON_H

#include "../scene/node.h"
#include "../core/event_bus.h"

/* Callback when the control is toggled (new state in `checked`). */
typedef void (*checkbutton_toggle_cb_t)(node_t *control, void *userdata, bool checked);

/* Create a checkbox or radio control. Width is computed from the label. */
node_t *checkbox_create(const char *name, int x, int y, const char *text, bool checked);
node_t *radio_create(const char *name, int x, int y, const char *text, bool checked);

bool checkbutton_get_checked(node_t *control);
void checkbutton_set_checked(node_t *control, bool checked);
void checkbutton_set_on_toggle(node_t *control, checkbutton_toggle_cb_t cb, void *userdata);

#endif /* GUI_CHECKBUTTON_H */