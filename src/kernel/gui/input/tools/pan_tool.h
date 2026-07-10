/*
 * gui/input/tools/pan_tool.h  +  pan_tool.c
 *
 * PanTool — move a câmera ao arrastar com RMB ou Space+LMB.
 * Também responde aos atalhos de teclado H (home) e F (fit).
 */
#ifndef GUI_PAN_TOOL_H
#define GUI_PAN_TOOL_H

#include "tool.h"

tool_t *pan_tool_create(camera_t *camera, node_t *scene_root);

#endif
