/*
 * gui/core/taskbar.c — Barra de tarefas estilo desktop (painel inferior).
 *
 * Fica fixa na parte inferior da tela e mostra: botão "Aplicativos"
 * (alterna o launcher), um botão por janela de app aberta (clique traz a
 * janela para a frente) e um relógio à direita.
 *
 * A barra é mantida sempre por cima: a cada frame o compositor chama
 * taskbar_refresh(), que recoloca o nó da barra no fim dos filhos do root
 * (topo em z) e reconstrói os botões de apps abertos quando o conjunto muda.
 */
#include "taskbar.h"
#include "../scene/node.h"
#include "../core/event_bus.h"
#include "../core/theme_engine.h"
#include "../widgets/button.h"
#include "../widgets/label.h"
#include "../widgets/panel.h"
#include "../widgets/window_node.h"
#include "../core/app_registry.h"
#include "../window/focus_manager.h"
#include "../window/window_manager.h"
#include "rtc.h"
#include "kheap.h"
#include "string.h"

#define TASKBAR_H 46

static node_t *s_taskbar      = NULL;
static node_t *s_task_start   = NULL;
static node_t *s_task_clock   = NULL;

/* botões de apps abertos (filhos da barra) + janelas que representam */
#define MAX_TASK_BTNS 24
static node_t *s_task_btns[MAX_TASK_BTNS];
static uint32_t s_task_count = 0;

extern scene_graph_t *g_scene;

/* itoa global */
extern char *itoa(int value, char *str, int base);

/* --------------------------------------------------------------------------
 * Ações
 * -------------------------------------------------------------------------- */

static void start_btn_click(node_t *btn, void *ud) {
    (void)btn; (void)ud;
    app_registry_toggle_launcher();
}

static void task_btn_click(node_t *btn, void *ud) {
    (void)btn;
    node_t *win = (node_t *)ud;
    if (!win) return;

    extern focus_manager_t *g_focus_manager;
    if (g_focus_manager) focus_manager_set_focus(g_focus_manager, win);
    window_manager_bring_to_front(win);
}

/* --------------------------------------------------------------------------
 * Recolocar a barra sempre no fim dos filhos do root (topo em z)
 * -------------------------------------------------------------------------- */

static void taskbar_raise(void) {
    if (!s_taskbar || !s_taskbar->parent) return;
    node_t *parent = s_taskbar->parent;
    uint32_t idx = 0, found = 0;
    for (uint32_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == s_taskbar) { idx = i; found = 1; break; }
    }
    if (found && idx < parent->child_count - 1) {
        for (uint32_t i = idx; i < parent->child_count - 1; i++)
            parent->children[i] = parent->children[i + 1];
        parent->children[parent->child_count - 1] = s_taskbar;
    }
}

/* --------------------------------------------------------------------------
 * Relógio (RTC)
 * -------------------------------------------------------------------------- */

static void taskbar_update_clock(void) {
    if (!s_task_clock) return;
    rtc_time_t t = rtc_read_time();

    char hh[4], mm[4], ss[4], buf[16];
    itoa(t.hour / 10, hh, 10);   hh[1] = '0' + (t.hour % 10);   hh[2] = 0;
    itoa(t.minute / 10, mm, 10); mm[1] = '0' + (t.minute % 10); mm[2] = 0;
    itoa(t.second / 10, ss, 10); ss[1] = '0' + (t.second % 10); ss[2] = 0;
    buf[0] = hh[0]; buf[1] = hh[1]; buf[2] = ':';
    buf[3] = mm[0]; buf[4] = mm[1]; buf[5] = ':';
    buf[6] = ss[0]; buf[7] = ss[1]; buf[8] = 0;
    label_set_text(s_task_clock, buf);
}

/* --------------------------------------------------------------------------
 * Rotina de atualização
 * -------------------------------------------------------------------------- */

void taskbar_refresh(void) {
    if (!s_taskbar || !g_scene || !g_scene->root) return;

    taskbar_update_clock();

    /* 1. Coletar janelas de apps abertas do root (ignora o launcher) */
    node_t *open_windows[MAX_TASK_BTNS];
    const char *open_titles[MAX_TASK_BTNS];
    uint32_t open_count = 0;
    for (uint32_t i = 0; i < g_scene->root->child_count && open_count < MAX_TASK_BTNS; i++) {
        node_t *n = g_scene->root->children[i];
        if (n->type != NODE_WINDOW) continue;
        if (strcmp(n->name, "launcher") == 0) continue;
        if (n == s_taskbar) continue;
        open_windows[open_count] = n;
        open_titles[open_count] = window_node_get_title(n);
        open_count++;
    }

    /* 2. Ver se mudou desde a última vez */
    bool changed = (open_count != s_task_count);
    if (!changed) {
        for (uint32_t i = 0; i < open_count; i++) {
            if (s_task_btns[i] == NULL || s_task_btns[i]->userdata != open_windows[i]) {
                changed = true;
                break;
            }
        }
    }

    /* 3. Reconstruir botões de apps se necessário */
    if (changed) {
        while (s_task_count > 0) {
            uint32_t i = s_task_count - 1;
            node_t *b = s_task_btns[i];
            if (b && b->parent == s_taskbar) {
                node_remove_child(s_taskbar, b);
                node_destroy(b);
            }
            s_task_btns[i] = NULL;
            s_task_count--;
        }

        for (uint32_t i = 0; i < open_count; i++) {
            const char *title = open_titles[i];
            char *label = (char *)kmalloc(33);
            if (!label) break;
            if (title && title[0]) { strncpy(label, title, 32); label[32] = 0; }
            else                   { strncpy(label, open_windows[i]->name, 32); label[32] = 0; }

            int bw = 160;
            if (strlen(label) * 8 + 16 < 120) bw = strlen(label) * 8 + 16;

            node_t *b = button_create("task_btn", 0, 0, bw, TASKBAR_H - 12, label);
            kfree(label);
            if (!b) break;
            b->local_x = 132 + (int)i * (bw + 8);
            b->local_y = 6;
            button_set_on_click(b, task_btn_click, open_windows[i]);
            if (!node_add_child(s_taskbar, b)) { node_destroy(b); break; }
            s_task_btns[s_task_count] = b;
            s_task_count++;
        }
    }

    /* 4. Sempre manter no topo */
    taskbar_raise();
}

/* --------------------------------------------------------------------------
 * Criação
 * -------------------------------------------------------------------------- */

node_t *taskbar_create(int screen_w, int screen_h) {
    if (s_taskbar) return s_taskbar;
    if (!g_scene || !g_scene->root) return NULL;

    s_taskbar = panel_create("taskbar", 0, screen_h - TASKBAR_H, screen_w, TASKBAR_H,
                             theme_engine_get_color(THEME_COLOR_WINDOW_TITLEBAR));
    if (!s_taskbar) return NULL;
    s_taskbar->interactive = true;
    s_taskbar->opacity = 1.0f;

    /* Botão Aplicativos (inicia o launcher) */
    s_task_start = button_create("task_start", 6, 6, 118, TASKBAR_H - 12, "Aplicativos");
    if (s_task_start) {
        button_set_on_click(s_task_start, start_btn_click, NULL);
        node_add_child(s_taskbar, s_task_start);
    }

    /* Relógio à direita */
    s_task_clock = label_create("task_clock", screen_w - 90, 6, "--:--:--",
                                theme_engine_get_color(THEME_COLOR_BUTTON_TEXT));
    if (s_task_clock) {
        node_set_position(s_task_clock, screen_w - 90, 6);
        node_set_size(s_task_clock, 84, TASKBAR_H - 12);
        node_add_child(s_taskbar, s_task_clock);
    }

    node_add_child(g_scene->root, s_taskbar);
    taskbar_refresh();
    return s_taskbar;
}
