/*
 * gui/widgets/button.h
 */
#ifndef GUI_BUTTON_H
#define GUI_BUTTON_H

#include "../scene/node.h"
#include "../core/event_bus.h"

/* Opcional: callback ao clicar */
typedef void (*button_click_cb_t)(node_t *button, void *userdata);

node_t *button_create(const char *name, int x, int y, int w, int h, const char *text);

void button_set_text(node_t *button, const char *text);
void button_set_on_click(node_t *button, button_click_cb_t cb, void *userdata);
void button_set_highlight(node_t *button, bool highlighted);

#endif /* GUI_BUTTON_H */
