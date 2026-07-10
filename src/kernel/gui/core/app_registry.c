/*
 * gui/core/app_registry.c
 */
#include "app_registry.h"
#include "kheap.h"
#include "string.h"
#include "../scene/node.h"
#include "../widgets/window_node.h"
#include "../widgets/button.h"
#include "../layout/layout_engine.h"
#include "../window/focus_manager.h"

#define MAX_APPS 32

static app_descriptor_t s_apps[MAX_APPS];
static uint32_t s_app_count = 0;
static node_t *s_launcher_win = NULL;

void app_registry_init(void) {
    s_app_count = 0;
    s_launcher_win = NULL;
}

void app_registry_add(const char *name, const char *icon, void (*start)(void)) {
    if (s_app_count >= MAX_APPS) return;
    s_apps[s_app_count].name = name;
    s_apps[s_app_count].icon = icon;
    s_apps[s_app_count].start = start;
    s_app_count++;
}

uint32_t app_registry_get_count(void) {
    return s_app_count;
}

const app_descriptor_t *app_registry_get(uint32_t index) {
    if (index >= s_app_count) return NULL;
    return &s_apps[index];
}

static void launcher_btn_click(node_t *btn, void *userdata) {
    (void)btn; // unused
    uint32_t index = (uint32_t)(uint64_t)userdata;
    if (index < s_app_count && s_apps[index].start) {
        s_apps[index].start();
    }
    
    // Close the launcher
    if (s_launcher_win) {
        extern gui_event_bus_t *g_event_bus;
        if (g_event_bus) {
            gui_event_t close_ev;
            memset(&close_ev, 0, sizeof(close_ev));
            close_ev.type = GUI_EVENT_WIN_CLOSE;
            close_ev.generic.a = (uint64_t)s_launcher_win;
            event_bus_post(g_event_bus, &close_ev);
        }
        s_launcher_win = NULL;
    }
}

static uint32_t s_selected_app_index = 0;
static node_vtable_t s_launcher_vtable;
static bool s_launcher_vtable_inited = false;
static bool (*s_orig_launcher_on_event)(node_t *, const gui_event_t *);

static void update_launcher_selection(void) {
    if (!s_launcher_win) return;
    for (uint32_t i = 0; i < s_app_count; i++) {
        // Buttons are added after the VBOX layout, they are the children.
        // Wait, title bar might not be a child in window_node, but let's just check type
        if (i < s_launcher_win->child_count) {
            node_t *btn = s_launcher_win->children[i];
            if (btn->type == NODE_BUTTON) {
                button_set_highlight(btn, i == s_selected_app_index);
            }
        }
    }
}

static bool launcher_on_event(node_t *self, const gui_event_t *e) {
    if (e->type == GUI_EVENT_KEY_DOWN) {
        uint8_t sc = e->key.scancode;
        if (sc == 0x11 || sc == 0x48 || sc == 0x1E) { // W, Up, A
            if (s_selected_app_index > 0) s_selected_app_index--;
            update_launcher_selection();
            return true;
        } else if (sc == 0x1F || sc == 0x50 || sc == 0x1F) { // S, Down, S
            if (s_selected_app_index + 1 < s_app_count) s_selected_app_index++;
            update_launcher_selection();
            return true;
        } else if (sc == 0x1C) { // Enter
            if (s_selected_app_index < s_app_count && s_apps[s_selected_app_index].start) {
                s_apps[s_selected_app_index].start();
            }
            // Close the launcher
            if (s_launcher_win) {
                extern gui_event_bus_t *g_event_bus;
                if (g_event_bus) {
                    gui_event_t close_ev;
                    memset(&close_ev, 0, sizeof(close_ev));
                    close_ev.type = GUI_EVENT_WIN_CLOSE;
                    close_ev.generic.a = (uint64_t)s_launcher_win;
                    event_bus_post(g_event_bus, &close_ev);
                }
                s_launcher_win = NULL;
            }
            return true;
        }
    }
    
    if (s_orig_launcher_on_event) {
        return s_orig_launcher_on_event(self, e);
    }
    return false;
}

void app_registry_show_launcher(void) {
    if (s_launcher_win) return; // already open

    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return;
    
    // Create launcher window centered
    s_launcher_win = window_node_create("launcher", 300, 150, 400, 300, "Applications");
    if (!s_launcher_win) return;
    
    s_launcher_win->layout_type = LAYOUT_VBOX;
    s_launcher_win->padding[0] = 30; // title
    s_launcher_win->padding[1] = 10;
    s_launcher_win->padding[2] = 10;
    s_launcher_win->padding[3] = 10;
    
    for (uint32_t i = 0; i < s_app_count; i++) {
        node_t *btn = button_create("app_btn", 0, 0, 380, 40, s_apps[i].name);
        button_set_on_click(btn, launcher_btn_click, (void*)(uint64_t)i);
        
        node_add_child(s_launcher_win, btn);
        
        btn->layout_align = ALIGN_STRETCH;
        btn->margin[2] = 5;
    }
    
    node_add_child(g_scene->root, s_launcher_win);
    layout_engine_compute(s_launcher_win);
    
    extern focus_manager_t *g_focus_manager;
    if (g_focus_manager) {
        focus_manager_set_focus(g_focus_manager, s_launcher_win);
    }
    
    extern void window_manager_bring_to_front(node_t *node);
    window_manager_bring_to_front(s_launcher_win);
    
    if (!s_launcher_vtable_inited && s_launcher_win->vtable) {
        s_launcher_vtable = *s_launcher_win->vtable;
        s_orig_launcher_on_event = s_launcher_vtable.on_event;
        s_launcher_vtable.on_event = launcher_on_event;
        s_launcher_vtable_inited = true;
    }
    if (s_launcher_vtable_inited) {
        s_launcher_win->vtable = &s_launcher_vtable;
    }
    
    s_selected_app_index = 0;
    update_launcher_selection();
}
