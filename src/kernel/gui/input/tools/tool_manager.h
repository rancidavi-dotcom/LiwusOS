/*
 * gui/input/tools/tool_manager.h
 *
 * O ToolManager é um assinante do EventBus. Ele recebe todos os eventos
 * de entrada (mouse, teclado) e os repassa para a Tool atualmente ativa.
 * Se a Tool não consumir o evento (retornar false), o ToolManager pode
 * repassá-lo para a PanTool (que atua como fallback global).
 */
#ifndef GUI_TOOL_MANAGER_H
#define GUI_TOOL_MANAGER_H

#include "tool.h"

typedef struct tool_manager tool_manager_t;

/* Inicializa o ToolManager e o inscreve no EventBus */
tool_manager_t *tool_manager_create(gui_event_bus_t *bus, camera_t *cam, node_t *scene_root);
void            tool_manager_destroy(tool_manager_t *tm);

/* Registra uma tool na lista (ordem de prioridade, primeiro a ser chamado) */
void tool_manager_add_tool(tool_manager_t *tm, tool_t *tool);

#endif /* GUI_TOOL_MANAGER_H */
