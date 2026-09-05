/*
 * gui/apps/gui_text_editor.c
 * Simple text editor for SDFS files.
 */
#include "gui_text_editor.h"
#include "../core/app_registry.h"
#include "../scene/node.h"
#include "../widgets/window_node.h"
#include "../widgets/button.h"
#include "../widgets/label.h"
#include "../widgets/panel.h"
#include "../widgets/text_input.h"
#include "../layout/layout_engine.h"
#include "../window/window_manager.h"
#include "fs/sdfs.h"
#include "string.h"
#include "kheap.h"
#include "../core/event_bus.h"

#define EDITOR_MAX_FILE_SIZE (64 * 1024)

extern uint32_t timer_ticks;

typedef struct {
    char *filepath;
    char *content;
    uint32_t content_len;
    uint32_t cursor_pos;
    node_t *text_input;
    node_t *status_label;
    bool modified;
} editor_state_t;

static editor_state_t *g_editor = NULL;

static void editor_update_status(void) {
    if (!g_editor || !g_editor->status_label) return;
    char status[128] = "";
    const char *fname = g_editor->filepath ? g_editor->filepath : "(novo)";
    char num[16];
    strcat(status, fname);
    strcat(status, " | ");
    itoa((int)g_editor->content_len, num, 10);
    strcat(status, num);
    strcat(status, " bytes");
    if (g_editor->modified) strcat(status, " (modificado)");
    label_set_text(g_editor->status_label, status);
}

static void editor_load_file(const char *path) {
    if (!g_editor) return;
    
    if (g_editor->content) {
        kfree(g_editor->content);
        g_editor->content = NULL;
    }
    g_editor->content_len = 0;
    g_editor->cursor_pos = 0;
    g_editor->modified = false;

    if (g_editor->filepath) {
        kfree(g_editor->filepath);
        g_editor->filepath = NULL;
    }
    g_editor->filepath = (char *)kmalloc(strlen(path) + 1);
    strcpy(g_editor->filepath, path);

    uint32_t size = 0;
    void *data = sdfs_read_file(path, &size);
    if (data && size > 0 && size < EDITOR_MAX_FILE_SIZE) {
        g_editor->content = (char *)kmalloc(size + 1);
        memcpy(g_editor->content, data, size);
        g_editor->content[size] = '\0';
        g_editor->content_len = size;
        g_editor->cursor_pos = size;
        kfree(data);
    } else {
        g_editor->content = (char *)kmalloc(1);
        g_editor->content[0] = '\0';
        if (data) kfree(data);
    }
    
    if (g_editor->text_input) {
        text_input_set_text(g_editor->text_input, g_editor->content);
    }
    editor_update_status();
}

static void editor_save_file(node_t *btn, void *userdata) {
    (void)btn;
    (void)userdata;
    if (!g_editor || !g_editor->filepath || !g_editor->content) return;
    
    uint32_t len = strlen(g_editor->content);
    if (sdfs_write_file(g_editor->filepath, (uint8_t *)g_editor->content, len) == 0) {
        g_editor->content_len = len;
        g_editor->modified = false;
        editor_update_status();
    }
}

static void editor_save_as_click(node_t *btn, void *userdata) {
    (void)btn;
    (void)userdata;
    if (!g_editor) return;
    
    char path[160] = "/novo_arquivo_";
    char num[16];
    itoa((int)timer_ticks, num, 10);
    strcat(path, num);
    strcat(path, ".txt");
    
    if (sdfs_create_file(path) == 0 && sdfs_write_file(path, (uint8_t *)"", 0) == 0) {
        if (g_editor->filepath) kfree(g_editor->filepath);
        g_editor->filepath = (char *)kmalloc(strlen(path) + 1);
        strcpy(g_editor->filepath, path);
        editor_save_file(NULL, NULL);
    }
}

static void editor_close_click(node_t *btn, void *userdata) {
    (void)btn;
    (void)userdata;
    if (!g_editor || !g_editor->modified) {
        if (g_editor) {
            if (g_editor->content) kfree(g_editor->content);
            if (g_editor->filepath) kfree(g_editor->filepath);
            kfree(g_editor);
            g_editor = NULL;
        }
        if (btn) {
            node_t *win = btn;
            while (win && win->type != NODE_WINDOW) win = win->parent;
            if (win) {
                extern gui_event_bus_t *g_event_bus;
                gui_event_t ev;
                memset(&ev, 0, sizeof(ev));
                ev.type = GUI_EVENT_WIN_CLOSE;
                ev.generic.a = (uint64_t)win;
                event_bus_post(g_event_bus, &ev);
            }
        }
    } else {
        if (g_editor->status_label) {
            label_set_text(g_editor->status_label, "Arquivo modificado - salve antes de fechar");
        }
    }
}

static void editor_text_changed(node_t *input, void *userdata) {
    (void)userdata;
    if (!g_editor) return;
    const char *text = text_input_get_text(input);
    if (g_editor->content) kfree(g_editor->content);
    g_editor->content = (char *)kmalloc(strlen(text) + 1);
    strcpy(g_editor->content, text);
    g_editor->modified = true;
    editor_update_status();
}

static bool editor_window_key_char(node_t *win, char c, void *userctx) {
    (void)win;
    (void)userctx;
    if (g_editor && g_editor->text_input) {
        text_input_focus(g_editor->text_input);
        text_input_type_char(g_editor->text_input, (uint32_t)(uint8_t)c);
        return true;
    }
    return false;
}

static void editor_start_with_file(const char *path) {
    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return;

    if (g_editor) {
        if (g_editor->content) kfree(g_editor->content);
        if (g_editor->filepath) kfree(g_editor->filepath);
        kfree(g_editor);
    }
    g_editor = (editor_state_t *)kmalloc(sizeof(editor_state_t));
    memset(g_editor, 0, sizeof(editor_state_t));

    node_t *win = window_node_create("text_editor", 150, 100, 700, 500, "Editor de Texto");
    if (!win) { kfree(g_editor); g_editor = NULL; return; }
    window_node_set_key_handler(win, NULL, editor_window_key_char, NULL);
    win->layout_type = LAYOUT_VBOX;
    win->padding[0] = 30;
    win->padding[1] = 10;
    win->padding[2] = 10;
    win->padding[3] = 10;

    node_t *toolbar = panel_create("editor_toolbar", 0, 0, 680, 36, 0xFF0A2E1A);
    toolbar->layout_type = LAYOUT_HBOX;
    toolbar->padding[0] = 6; toolbar->padding[1] = 8;
    toolbar->padding[2] = 6; toolbar->padding[3] = 8;
    panel_set_border(toolbar, 0xFF008800, 1);

    node_t *save_btn = button_create("editor_save", 0, 0, 60, 28, "Salvar");
    node_t *saveas_btn = button_create("editor_saveas", 0, 0, 70, 28, "Salvar Como");
    node_t *close_btn = button_create("editor_close", 0, 0, 60, 28, "Fechar");
    button_set_on_click(save_btn, editor_save_file, NULL);
    button_set_on_click(saveas_btn, editor_save_as_click, NULL);
    button_set_on_click(close_btn, editor_close_click, NULL);
    node_add_child(toolbar, save_btn);
    node_add_child(toolbar, saveas_btn);
    node_add_child(toolbar, close_btn);

    g_editor->text_input = text_input_create("editor_text", 0, 0, 680, 380, "");
    g_editor->text_input->flex_weight = 1;
    g_editor->text_input->layout_align = ALIGN_STRETCH;
    text_input_set_on_change(g_editor->text_input, editor_text_changed, NULL);

    node_t *footer = panel_create("editor_footer", 0, 0, 680, 26, 0xFF0A1510);
    footer->layout_type = LAYOUT_HBOX;
    footer->padding[0] = 5; footer->padding[1] = 8;
    footer->padding[2] = 5; footer->padding[3] = 8;
    panel_set_border(footer, 0xFF008800, 1);
    g_editor->status_label = label_create("editor_status", 0, 0, "Pronto", 0xFF00CC33);
    g_editor->status_label->flex_weight = 1;
    node_add_child(footer, g_editor->status_label);

    node_add_child(win, toolbar);
    node_add_child(win, g_editor->text_input);
    node_add_child(win, footer);
    node_add_child(g_scene->root, win);
    
    // Auto-focus the text input
    text_input_focus(g_editor->text_input);

    editor_load_file(path);
    window_manager_bring_to_front(win);
}

void app_text_editor_init(void) {
    app_registry_add("Editor de Texto", "text_icon", NULL);
}

void text_editor_open_file(const char *path) {
    editor_start_with_file(path);
}