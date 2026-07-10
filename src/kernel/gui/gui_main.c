/*
 * gui/gui_main.c  —  Bootstrap do subsistema gráfico
 *
 * Ordem de inicialização (respeita dependências):
 *   1. scene_graph_init()
 *   2. event_bus_create()
 *   3. input_manager_create()
 *   4. camera_create()
 *   5. fb_renderer_create()
 *   6. Montagem da Scene (canvas root + terminal node)
 *   7. compositor_create()
 */
#include "gui_main.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "core/event_bus.h"
#include "scene/node.h"
#include "scene/camera.h"
#include "core/theme_engine.h"
#include "core/animation_engine.h"
#include "render/renderer.h"
#include "render/fb_renderer.h"
#include "render/compositor.h"
#include "layout/layout_engine.h"
#include "input/input_manager.h"
#include "input/tools/tool_manager.h"
#include "input/tools/pan_tool.h"
#include "input/tools/select_tool.h"
#include "input/tools/move_tool.h"

#include "widgets/window_node.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/panel.h"
#include "window/focus_manager.h"
#include "window/window_manager.h"
#include "core/app_registry.h"

/* VGA globals (de vga.c) */
extern uint32_t vga_fb_width;
extern uint32_t vga_fb_height;

/* --------------------------------------------------------------------------
 * Instâncias (owned por este TU)
 * -------------------------------------------------------------------------- */

gui_event_bus_t *g_event_bus = NULL;
input_manager_t *g_input_manager = NULL;
camera_t        *g_camera = NULL;
focus_manager_t *g_focus_manager = NULL;
static gui_renderer_t  *s_renderer = NULL;
static compositor_t    *s_comp     = NULL;
static tool_manager_t  *s_tools    = NULL;
static window_manager_t *s_wm      = NULL;

/* --------------------------------------------------------------------------
 * Init
 * -------------------------------------------------------------------------- */

void demo_app_start(void) {
    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return;

    node_t *win = window_node_create("demo_win", 100, 100, 300, 200, "LiwusOS Demo");
    if (win) {
        win->layout_type = LAYOUT_VBOX;
        win->padding[0] = 30;
        win->padding[1] = 10;
        win->padding[2] = 10;
        win->padding[3] = 10;

        node_t *lbl = label_create("lbl", 0, 0, "Hello, Infinite Canvas!", 0xFFFFFFFF);
        lbl->margin[2] = 10;
        lbl->layout_align = ALIGN_CENTER;

        node_t *btn = button_create("btn", 0, 0, 120, 36, "Click Me");
        btn->margin[2] = 10;
        btn->layout_align = ALIGN_CENTER;

        node_t *panel = panel_create("pnl", 0, 0, 260, 40, 0x88000000);
        panel_set_border(panel, 0xFF475569, 1);
        panel->flex_weight = 1;
        panel->layout_align = ALIGN_STRETCH;
        
        node_add_child(win, lbl);
        node_add_child(win, btn);
        node_add_child(win, panel);
        node_add_child(g_scene->root, win);
        
        layout_engine_compute(win);

        win->width = 0;
        win->height = 0;
        animation_start(win, ANIM_PROP_WIDTH, NULL, 0, 300, 30);
        animation_start(win, ANIM_PROP_HEIGHT, NULL, 0, 200, 30);
    }
}

void settings_app_start(void) {
    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return;

    node_t *win = window_node_create("settings_win", 150, 150, 400, 300, "System Settings");
    if (win) {
        win->layout_type = LAYOUT_VBOX;
        win->padding[0] = 30;
        win->padding[1] = 10;
        win->padding[2] = 10;
        win->padding[3] = 10;

        node_t *lbl = label_create("lbl_set", 0, 0, "Settings Panel", 0xFFFFFFFF);
        lbl->margin[2] = 10;
        lbl->layout_align = ALIGN_CENTER;

        node_t *lbl2 = label_create("lbl_set2", 0, 0, "Theme: Dark Mode", 0xFFAAAAAA);
        lbl2->margin[2] = 20;
        lbl2->layout_align = ALIGN_CENTER;

        node_add_child(win, lbl);
        node_add_child(win, lbl2);
        node_add_child(g_scene->root, win);
        layout_engine_compute(win);
    }
}

static char terminal_buffer[256] = "root@liwusos# ";
static int  terminal_cursor     = 14;
static node_t *s_terminal_lbl  = NULL;

static bool term_key_down(node_t *self, uint8_t sc, void *ctx) {
    (void)self; (void)ctx;
    extern void label_set_text(node_t *label, const char *text);

    if (sc == 0x0E) { /* Backspace */
        if (terminal_cursor > 14) {
            terminal_buffer[--terminal_cursor] = '\0';
            if (s_terminal_lbl) label_set_text(s_terminal_lbl, terminal_buffer);
        }
        return true;
    }
    if (sc == 0x1C) { /* Enter */
        if (terminal_cursor + 16 < 256) {
            terminal_buffer[terminal_cursor++] = '\n';
            const char *p = "root@liwusos# ";
            while (*p && terminal_cursor < 254)
                terminal_buffer[terminal_cursor++] = *p++;
            terminal_buffer[terminal_cursor] = '\0';
        } else {
            /* clear */
            const char *p = "root@liwusos# ";
            terminal_cursor = 0;
            while (*p) terminal_buffer[terminal_cursor++] = *p++;
            terminal_buffer[terminal_cursor] = '\0';
        }
        if (s_terminal_lbl) label_set_text(s_terminal_lbl, terminal_buffer);
        return true;
    }
    /* Consume all other keys (WASD, arrows…) so canvas doesn't move */
    return true;
}

static bool term_key_char(node_t *self, char c, void *ctx) {
    (void)self; (void)ctx;
    extern void label_set_text(node_t *label, const char *text);
    if (c >= 32 && c <= 126 && terminal_cursor < 255) {
        terminal_buffer[terminal_cursor++] = c;
        terminal_buffer[terminal_cursor]   = '\0';
        if (s_terminal_lbl) label_set_text(s_terminal_lbl, terminal_buffer);
    }
    return true;
}

void terminal_app_start(void) {
    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return;

    node_t *win = window_node_create("terminal_win", 200, 200, 500, 350, "Terminal (GUI)");
    if (win) {
        win->layout_type = LAYOUT_VBOX;
        win->padding[0] = 30;
        win->padding[1] = 5;
        win->padding[2] = 5;
        win->padding[3] = 5;

        node_t *panel = panel_create("term_bg", 0, 0, 490, 310, 0xFF000000);
        panel->flex_weight  = 1;
        panel->layout_align = ALIGN_STRETCH;

        s_terminal_lbl = label_create("term_txt", 0, 0, terminal_buffer, 0xFF00FF00);
        s_terminal_lbl->local_x = 5;
        s_terminal_lbl->local_y = 5;
        node_add_child(panel, s_terminal_lbl);

        node_add_child(win, panel);
        node_add_child(g_scene->root, win);
        layout_engine_compute(win);

        /* Safe callback-based key handling — no vtable subclassing */
        window_node_set_key_handler(win, term_key_down, term_key_char, NULL);

        extern focus_manager_t *g_focus_manager;
        if (g_focus_manager) focus_manager_set_focus(g_focus_manager, win);
        extern void window_manager_bring_to_front(node_t *node);
        window_manager_bring_to_front(win);
    }
}




void gui_init(void) {
    /* 1. Scene graph */
    scene_graph_init();
    
    /* 1.1. App Registry */
    app_registry_init();

    /* 1.5. Theme Engine */
    theme_engine_init();

    /* 1.6. Animation Engine */
    animation_engine_init();

    /* 2. Event bus */
    g_event_bus = event_bus_create();

    /* 3. Input manager */
    g_input_manager = input_manager_create(g_event_bus);

    /* 4. Camera */
    int sw = (int)vga_fb_width;
    int sh = (int)vga_fb_height;
    g_camera = camera_create(sw, sh);

    /* 5. Framebuffer renderer */
    s_renderer = fb_renderer_create();

    /* 6. Montar a Scene */
    node_t *root = node_create(NODE_CANVAS, "canvas");
    if (!root) return;
    g_scene->root = root;

    /* Register Apps */
    app_registry_add("Demo Window", NULL, demo_app_start);
    app_registry_add("System Settings", NULL, settings_app_start);
    app_registry_add("Terminal", NULL, terminal_app_start);


    /* 7. Managers (must subscribe BEFORE tools to intercept focus events) */
    g_focus_manager = focus_manager_create(g_event_bus, root);
    s_wm    = window_manager_create(g_event_bus, root);

    /* 8. Tools */
    s_tools = tool_manager_create(g_event_bus, g_camera, root);
    if (s_tools) {
        tool_t *select = select_tool_create(g_camera, root);
        tool_t *move   = move_tool_create(g_camera, root, select);
        tool_t *pan    = pan_tool_create(g_camera, root);

        /* A ordem importa: primeiro tentamos Move, depois Select, depois Pan */
        tool_manager_add_tool(s_tools, move);
        tool_manager_add_tool(s_tools, select);
        tool_manager_add_tool(s_tools, pan);
    }

    /* 9. Compositor */
    s_comp = compositor_create(s_renderer, g_camera, g_event_bus, g_input_manager, root);
}

/* --------------------------------------------------------------------------
 * Task do compositor (loop infinito, chamado como kernel task)
 * -------------------------------------------------------------------------- */

void gui_compositor_task(void) {
    if (!s_comp) return;
    while (1) {
        compositor_frame(s_comp);
    }
}
