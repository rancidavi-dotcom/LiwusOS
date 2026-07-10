/*
 * gui/widgets/panel.h
 *
 * Um Panel é um container visual genérico (NODE_PANEL).
 * Ele desenha um fundo sólido ou semi-transparente e bordas, e serve
 * para agrupar outros nós.
 */
#ifndef GUI_PANEL_H
#define GUI_PANEL_H

#include "../scene/node.h"

node_t *panel_create(const char *name, int x, int y, int w, int h, uint32_t bg_color);

void panel_set_bg_color(node_t *panel, uint32_t color);
void panel_set_border(node_t *panel, uint32_t color, int thickness);

#endif /* GUI_PANEL_H */
