/*
 * gui/input/tools/move_tool.h
 *
 * MoveTool — Intercepta o arraste do mouse para mover o node selecionado
 * (caso ele seja movível, ex: NODE_WINDOW).
 * Requer uma referência à SelectTool para saber qual node mover.
 */
#ifndef GUI_MOVE_TOOL_H
#define GUI_MOVE_TOOL_H

#include "tool.h"
#include "select_tool.h"

tool_t *move_tool_create(camera_t *camera, node_t *scene_root, tool_t *select_tool);

#endif /* GUI_MOVE_TOOL_H */
