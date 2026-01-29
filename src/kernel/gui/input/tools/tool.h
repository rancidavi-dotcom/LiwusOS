/*
 * gui/input/tools/tool.h  —  Interface base para todas as ferramentas
 *
 * Uma Tool é o único consumidor de eventos de entrada que pode agir
 * sobre o Canvas/Camera. Nenhum widget lida com input diretamente:
 * o InputManager posta eventos → EventBus → Tool ativa.
 *
 * Ferramentas disponíveis (e sua ativação padrão):
 *   PanTool      — arrastar com MMB ou Space+LMB
 *   SelectTool   — LMB
 *   MoveTool     — ativada quando SelectTool tem seleção e arrasta
 *   ZoomTool     — scroll do mouse ou Ctrl+/−
 *   ResizeTool   — borda de janela + LMB
 *   InspectTool  — I (abre debug overlay sobre o node)
 *
 * Padrão:
 *   - Só uma Tool está ativa por vez (ToolManager gerencia)
 *   - on_event retorna true para consumir o evento (stop propagation)
 *   - on_activate / on_deactivate fazem setup/teardown
 */
#ifndef GUI_TOOL_H
#define GUI_TOOL_H

#include <stdbool.h>
#include "../../core/event_bus.h"
#include "../../scene/node.h"
#include "../../scene/camera.h"

typedef struct tool tool_t;

typedef struct {
    /* Nome para debug/UI */
    const char *name;

    /* Chamado quando a tool se torna ativa */
    void (*on_activate)(tool_t *self);

    /* Chamado quando outra tool assume */
    void (*on_deactivate)(tool_t *self);

    /* Processa um evento do bus.
     * Retorna true para consumir (stop propagation). */
    bool (*on_event)(tool_t *self, const gui_event_t *event);

    /* Destrutor opcional */
    void (*destroy)(tool_t *self);
} tool_vtable_t;

struct tool {
    const tool_vtable_t *vtable;
    camera_t            *camera;   /* referência compartilhada */
    node_t              *scene_root;
    void                *userdata;
    bool                 active;
};

/* Wrappers inline para o vtable */
static inline void tool_activate(tool_t *t) {
    t->active = true;
    if (t->vtable->on_activate) t->vtable->on_activate(t);
}
static inline void tool_deactivate(tool_t *t) {
    t->active = false;
    if (t->vtable->on_deactivate) t->vtable->on_deactivate(t);
}
static inline bool tool_event(tool_t *t, const gui_event_t *e) {
    if (!t->active || !t->vtable->on_event) return false;
    return t->vtable->on_event(t, e);
}

#endif /* GUI_TOOL_H */
