/*
 * gui/core/taskbar.h — Barra de tarefas estilo desktop (painel inferior)
 */
#ifndef GUI_TASKBAR_H
#define GUI_TASKBAR_H

#include "../scene/node.h"

/* Cria a barra de tarefas fixa na parte inferior da tela. */
node_t *taskbar_create(int screen_w, int screen_h);

/* Atualiza a barra (apps abertos + relógio) e a mantém sempre no topo.
 * Chamada a cada quadro pelo compositor. */
void taskbar_refresh(void);

#endif /* GUI_TASKBAR_H */
