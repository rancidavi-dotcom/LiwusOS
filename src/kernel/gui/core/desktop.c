/*
 * gui/core/desktop.c — Ícones de aplicativos sobre o fundo do desktop.
 *
 * Cria um botão com o nome de cada app registrado, posicionados em uma
 * grade no canto superior esquerdo do desktop (estilo KDE). Clicar abre o
 * aplicativo. Ficam abaixo da barra de tarefas e das janelas.
 */
#include "desktop.h"
#include "../scene/node.h"
#include "../core/app_registry.h"
#include "../widgets/button.h"
#include "string.h"

#define ICON_W 130
#define ICON_H 52
#define ICON_GAP_X 12
#define ICON_GAP_Y 12
#define ICON_MARGIN 18
#define ICONS_PER_ROW 4

extern scene_graph_t *g_scene;

static void desktop_icon_click(node_t *btn, void *ud) {
    (void)btn;
    uint32_t idx = (uint32_t)(uint64_t)ud;
    const app_descriptor_t *app = app_registry_get(idx);
    if (app && app->start) app->start();
}

void desktop_create(int screen_w, int screen_h) {
    if (!g_scene || !g_scene->root) return;
    (void)screen_h;

    uint32_t count = app_registry_get_count();
    if (count == 0) return;

    for (uint32_t i = 0; i < count; i++) {
        const app_descriptor_t *app = app_registry_get(i);
        if (!app || !app->name) continue;

        int col = (int)(i % ICONS_PER_ROW);
        int row = (int)(i / ICONS_PER_ROW);
        int x = ICON_MARGIN + col * (ICON_W + ICON_GAP_X);
        int y = ICON_MARGIN + row * (ICON_H + ICON_GAP_Y);

        node_t *b = button_create("desktop_icon", x, y, ICON_W, ICON_H, app->name);
        if (!b) continue;
        button_set_on_click(b, desktop_icon_click, (void *)(uint64_t)i);
        node_add_child(g_scene->root, b);
        (void)screen_w;
    }
}
