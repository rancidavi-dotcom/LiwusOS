/*
 * gui/core/desktop.h — Ícones de aplicativos sobre o fundo do desktop
 */
#ifndef GUI_DESKTOP_H
#define GUI_DESKTOP_H

#include "../scene/node.h"

/* Cria os ícones de apps sobre o desktop (acima do papel de parede, abaixo
 * da barra de tarefas e das janelas). Clique abre o aplicativo. */
void desktop_create(int screen_w, int screen_h);

#endif /* GUI_DESKTOP_H */
