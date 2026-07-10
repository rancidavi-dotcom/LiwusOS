/*
 * gui/input/tools/select_tool.h
 *
 * Ferramenta padrão (seta).
 * - Clique esquerdo (LMB) em um node o seleciona.
 * - Clique no fundo desmarca tudo.
 * - Arrastar um node selecionado transfere o controle para a MoveTool.
 */
#ifndef GUI_SELECT_TOOL_H
#define GUI_SELECT_TOOL_H

#include "tool.h"

tool_t *select_tool_create(camera_t *camera, node_t *scene_root);

/* Retorna o node atualmente selecionado (ou NULL) */
node_t *select_tool_get_selection(tool_t *t);

#endif /* GUI_SELECT_TOOL_H */
