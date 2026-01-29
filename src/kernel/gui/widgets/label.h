/*
 * gui/widgets/label.h
 *
 * Label — Renderiza texto simples (1 linha) usando a fonte global.
 */
#ifndef GUI_LABEL_H
#define GUI_LABEL_H

#include "../scene/node.h"

node_t *label_create(const char *name, int x, int y, const char *text, uint32_t color);

void label_set_text(node_t *label, const char *text);
void label_set_color(node_t *label, uint32_t color);

#endif /* GUI_LABEL_H */
