/*
 * Persistent SDFS file explorer.
 *
 * The explorer deliberately talks to SDFS directly: it therefore shows the
 * files stored on liwus_disk.img, not the transient contents of the initrd.
 */
#include "gui_explorer.h"
#include "../core/app_registry.h"
#include "../scene/node.h"
#include "../widgets/window_node.h"
#include "../widgets/button.h"
#include "../widgets/label.h"
#include "../widgets/panel.h"
#include "../layout/layout_engine.h"
#include "../window/window_manager.h"
#include "fs/sdfs.h"
#include "string.h"
#include "gui_text_editor.h"

#define EXPLORER_MAX_ENTRIES 40
#define EXPLORER_PAGE_SIZE   8

typedef struct {
    char name[64];
    uint32_t size;
    int is_dir;
} explorer_entry_t;

static node_t *s_win = NULL;
static node_t *s_path_label = NULL;
static node_t *s_status_label = NULL;
static node_t *s_usage_label = NULL;
static node_t *s_list_panel = NULL;
static node_t *s_entry_buttons[EXPLORER_PAGE_SIZE];
static uint32_t s_entry_button_count = 0;
static explorer_entry_t s_entries[EXPLORER_MAX_ENTRIES];
static uint32_t s_entry_count = 0;
static uint32_t s_page = 0;
static int32_t s_selected = -1;
static char s_path[160] = "/";

static void explorer_rebuild(void);

static void set_status(const char *text) {
    if (s_status_label) label_set_text(s_status_label, text ? text : "");
}

static void update_usage(void) {
    char text[64] = "SDFS  ";
    char number[16];
    uint32_t total = 0;
    uint32_t used = 0;
    if (!s_usage_label) return;
    sdfs_get_usage(&total, &used);
    itoa((int)(used * SDFS_BLOCK_SIZE / 1024), number, 10);
    strcat(text, number);
    strcat(text, " KB usados / ");
    itoa((int)(total * SDFS_BLOCK_SIZE / 1024), number, 10);
    strcat(text, number);
    strcat(text, " KB");
    label_set_text(s_usage_label, text);
}

static void join_path(const char *name, char *out, uint32_t cap) {
    uint32_t n;
    if (strcmp(s_path, "/") == 0) {
        out[0] = '/';
        out[1] = '\0';
    } else {
        strncpy(out, s_path, cap - 1);
        out[cap - 1] = '\0';
    }
    n = strlen(out);
    if (n && out[n - 1] != '/' && n + 1 < cap) {
        out[n++] = '/';
        out[n] = '\0';
    }
    if (n < cap - 1) {
        strncpy(out + n, name, cap - n - 1);
        out[cap - 1] = '\0';
    }
}

static void go_parent(void) {
    uint32_t len;
    if (strcmp(s_path, "/") == 0) return;
    len = strlen(s_path);
    while (len > 1 && s_path[len - 1] != '/') len--;
    if (len <= 1) {
        strcpy(s_path, "/");
    } else {
        s_path[len - 1] = '\0';
    }
}

static void refresh_entries(void) {
    s_entry_count = 0;
    s_selected = -1;
    if (!sdfs_is_mounted()) {
        set_status("Disco persistente indisponivel");
        return;
    }
    for (int i = 0; i < EXPLORER_MAX_ENTRIES; i++) {
        explorer_entry_t *entry = &s_entries[s_entry_count];
        if (sdfs_list_dir_entry(s_path, i, entry->name, &entry->is_dir,
                                &entry->size) != 0)
            break;
        s_entry_count++;
    }
    if (s_page * EXPLORER_PAGE_SIZE >= s_entry_count) s_page = 0;
}

static void select_entry(node_t *btn, void *userdata) {
    char message[112] = "Selecionado: ";
    uint32_t index = (uint32_t)(uint64_t)userdata;
    (void)btn;
    if (index >= s_entry_count) return;
    s_selected = (int32_t)index;
    strcat(message, s_entries[index].name);
    if (s_entries[index].is_dir) strcat(message, " (pasta)");
    set_status(message);
    for (uint32_t i = 0; i < s_entry_button_count; i++) {
        uint32_t displayed = s_page * EXPLORER_PAGE_SIZE + i;
        button_set_highlight(s_entry_buttons[i], displayed == index);
    }
}

static void open_selected(node_t *btn, void *userdata) {
    (void)btn;
    (void)userdata;
    if (s_selected < 0 || (uint32_t)s_selected >= s_entry_count) {
        set_status("Selecione um item primeiro");
        return;
    }
    explorer_entry_t *entry = &s_entries[s_selected];
    if (entry->is_dir) {
        char full[sizeof(s_path)];
        join_path(entry->name, full, sizeof(full));
        strncpy(s_path, full, sizeof(s_path) - 1);
        s_path[sizeof(s_path) - 1] = '\0';
        s_page = 0;
        refresh_entries();
        explorer_rebuild();
        return;
    }
    char full[sizeof(s_path)];
    join_path(entry->name, full, sizeof(full));
    text_editor_open_file(full);
}

static void double_click_entry(node_t *btn, void *userdata) {
    uint32_t index = (uint32_t)(uint64_t)userdata;
    (void)btn;
    if (index >= s_entry_count) return;
    explorer_entry_t *entry = &s_entries[index];
    if (entry->is_dir) {
        char full[sizeof(s_path)];
        join_path(entry->name, full, sizeof(full));
        strncpy(s_path, full, sizeof(s_path) - 1);
        s_path[sizeof(s_path) - 1] = '\0';
        s_page = 0;
        refresh_entries();
        explorer_rebuild();
        return;
    }
    char full[sizeof(s_path)];
    join_path(entry->name, full, sizeof(full));
    text_editor_open_file(full);
}

static void up_click(node_t *btn, void *userdata) {
    (void)btn;
    (void)userdata;
    go_parent();
    s_page = 0;
    refresh_entries();
    explorer_rebuild();
}

static void refresh_click(node_t *btn, void *userdata) {
    (void)btn;
    (void)userdata;
    refresh_entries();
    explorer_rebuild();
    set_status("Lista atualizada");
}

static int create_unique_file(const char *base, const char *extension) {
    char name[48];
    char full[sizeof(s_path)];
    char number[12];
    uint32_t suffix = 1;
    strcpy(name, base);
    strcat(name, extension);
    while (1) {
        int exists = 0;
        for (uint32_t i = 0; i < s_entry_count; i++) {
            if (strcmp(s_entries[i].name, name) == 0) {
                exists = 1;
                break;
            }
        }
        if (!exists) break;
        strcpy(name, base);
        strcat(name, " ");
        itoa((int)++suffix, number, 10);
        strcat(name, number);
        strcat(name, extension);
    }
    join_path(name, full, sizeof(full));
    if (sdfs_create_file(full) != 0) return -1;
    return sdfs_write_file(full, (uint8_t *)"", 0) == 0 ? 0 : -1;
}

static void new_folder_click(node_t *btn, void *userdata) {
    char name[32] = "Nova pasta";
    char full[sizeof(s_path)];
    char number[8];
    uint32_t suffix = 1;
    (void)btn;
    (void)userdata;
    while (1) {
        int exists = 0;
        for (uint32_t i = 0; i < s_entry_count; i++) {
            if (strcmp(s_entries[i].name, name) == 0) {
                exists = 1;
                break;
            }
        }
        if (!exists) break;
        strcpy(name, "Nova pasta ");
        itoa((int)++suffix, number, 10);
        strcat(name, number);
    }
    join_path(name, full, sizeof(full));
    if (sdfs_create_dir(full) != 0) {
        set_status("Nao foi possivel criar a pasta");
        return;
    }
    refresh_entries();
    explorer_rebuild();
    set_status("Pasta criada");
}

static void new_file_click(node_t *btn, void *userdata) {
    const char *base = "Novo arquivo";
    const char *extension = ".txt";
    int kind = (int)(intptr_t)userdata;
    (void)btn;
    if (kind == 1) { base = "Novo script"; extension = ".lua"; }
    if (kind == 2) { base = "Configuracao"; extension = ".cfg"; }
    if (create_unique_file(base, extension) != 0) {
        set_status("Nao foi possivel criar o arquivo");
        return;
    }
    refresh_entries();
    explorer_rebuild();
    set_status("Arquivo criado no disco persistente");
}

static void delete_click(node_t *btn, void *userdata) {
    char full[sizeof(s_path)];
    (void)btn;
    (void)userdata;
    if (s_selected < 0 || (uint32_t)s_selected >= s_entry_count) {
        set_status("Selecione um item para excluir");
        return;
    }
    join_path(s_entries[s_selected].name, full, sizeof(full));
    if (sdfs_delete(full) != 0) {
        set_status("Exclusao falhou (pasta pode nao estar vazia)");
        return;
    }
    refresh_entries();
    explorer_rebuild();
    set_status("Item excluido");
}

static void page_click(node_t *btn, void *userdata) {
    int direction = (int)(intptr_t)userdata;
    uint32_t pages = (s_entry_count + EXPLORER_PAGE_SIZE - 1) / EXPLORER_PAGE_SIZE;
    (void)btn;
    if (!pages) return;
    if (direction > 0 && s_page + 1 < pages) s_page++;
    if (direction < 0 && s_page > 0) s_page--;
    explorer_rebuild();
}

static void explorer_rebuild(void) {
    char path_text[192] = "Disco: ";
    uint32_t begin;
    uint32_t end;
    if (!s_win || !s_list_panel) return;
    for (uint32_t i = 0; i < s_entry_button_count; i++) {
        node_remove_child(s_list_panel, s_entry_buttons[i]);
        node_destroy(s_entry_buttons[i]);
    }
    s_entry_button_count = 0;
    strcat(path_text, s_path);
    if (s_path_label) label_set_text(s_path_label, path_text);

    begin = s_page * EXPLORER_PAGE_SIZE;
    end = begin + EXPLORER_PAGE_SIZE;
    if (end > s_entry_count) end = s_entry_count;
    for (uint32_t i = begin; i < end; i++) {
        char text[92] = "[FILE] ";
        if (s_entries[i].is_dir) strcpy(text, "[DIR]  ");
        strcat(text, s_entries[i].name);
        node_t *entry = button_create("explorer_entry", 0, 0, 500, 30, text);
        entry->margin[2] = 4;
        entry->layout_align = ALIGN_STRETCH;
        button_set_on_click(entry, select_entry, (void *)(uint64_t)i);
        button_set_on_double_click(entry, double_click_entry, (void *)(uint64_t)i);
        node_add_child(s_list_panel, entry);
        s_entry_buttons[s_entry_button_count++] = entry;
    }
    if (s_entry_count == 0) set_status("Pasta vazia");
    update_usage();
    layout_engine_compute(s_list_panel);
    layout_engine_compute(s_win);
}

static void explorer_start(void) {
    extern scene_graph_t *g_scene;
    if (s_win || !g_scene || !g_scene->root) return;
    s_win = window_node_create("file_explorer", 120, 70, 620, 570,
                               "File Explorer");
    if (!s_win) return;
    s_win->layout_type = LAYOUT_VBOX;
    s_win->padding[0] = 30;
    s_win->padding[1] = 16;
    s_win->padding[2] = 14;
    s_win->padding[3] = 16;

    node_t *header = panel_create("explorer_header", 0, 0, 588, 42, 0xFF0A2E1A);
    header->layout_type = LAYOUT_HBOX;
    header->padding[0] = 8; header->padding[1] = 12;
    header->padding[2] = 8; header->padding[3] = 12;
    panel_set_border(header, 0xFF00AA00, 1);
    node_t *title = label_create("explorer_title", 0, 0, "FILES", 0xFF00FF41);
    title->flex_weight = 1;
    s_usage_label = label_create("explorer_usage", 0, 0, "SDFS", 0xFF00CC33);
    node_add_child(header, title);
    node_add_child(header, s_usage_label);

    node_t *location = panel_create("explorer_location", 0, 0, 588, 30, 0xFF0A1510);
    location->layout_type = LAYOUT_HBOX;
    location->padding[0] = 6; location->padding[1] = 10;
    location->padding[2] = 4; location->padding[3] = 10;
    panel_set_border(location, 0xFF008800, 1);
    s_path_label = label_create("explorer_path", 0, 0, "Location: /", 0xFF00FF41);
    node_add_child(location, s_path_label);

    node_t *toolbar = panel_create("explorer_toolbar", 0, 0, 588, 38, 0x330A2E1A);
    toolbar->layout_type = LAYOUT_HBOX;
    toolbar->padding[0] = 4; toolbar->padding[1] = 8;
    toolbar->padding[2] = 4; toolbar->padding[3] = 8;
    panel_set_border(toolbar, 0xFF008800, 1);
    node_t *up = button_create("explorer_up", 0, 0, 48, 30, "Up");
    node_t *open = button_create("explorer_open", 0, 0, 58, 30, "Open");
    node_t *folder = button_create("explorer_folder", 0, 0, 84, 30, "+ Folder");
    node_t *text = button_create("explorer_txt", 0, 0, 60, 30, "+ TXT");
    node_t *lua = button_create("explorer_lua", 0, 0, 60, 30, "+ LUA");
    node_t *cfg = button_create("explorer_cfg", 0, 0, 60, 30, "+ CFG");
    node_t *remove = button_create("explorer_delete", 0, 0, 62, 30, "Delete");
    node_t *refresh = button_create("explorer_refresh", 0, 0, 66, 30, "Refresh");
    button_set_on_click(up, up_click, NULL);
    button_set_on_click(open, open_selected, NULL);
    button_set_on_click(folder, new_folder_click, NULL);
    button_set_on_click(text, new_file_click, (void *)(intptr_t)0);
    button_set_on_click(lua, new_file_click, (void *)(intptr_t)1);
    button_set_on_click(cfg, new_file_click, (void *)(intptr_t)2);
    button_set_on_click(remove, delete_click, NULL);
    button_set_on_click(refresh, refresh_click, NULL);
    node_t *previous = button_create("explorer_prev", 0, 0, 78, 28, "Previous");
    node_t *next = button_create("explorer_next", 0, 0, 58, 28, "Next");
    button_set_on_click(previous, page_click, (void *)(intptr_t)-1);
    button_set_on_click(next, page_click, (void *)(intptr_t)1);
    node_add_child(toolbar, up); node_add_child(toolbar, open);
    node_add_child(toolbar, folder); node_add_child(toolbar, text);
    node_add_child(toolbar, lua); node_add_child(toolbar, cfg);
    node_add_child(toolbar, remove); node_add_child(toolbar, refresh);

    s_list_panel = panel_create("explorer_list", 0, 0, 588, 330, 0xFF050A10);
    s_list_panel->layout_type = LAYOUT_VBOX;
    s_list_panel->flex_weight = 1;
    s_list_panel->padding[0] = 8; s_list_panel->padding[1] = 8;
    s_list_panel->padding[2] = 8; s_list_panel->padding[3] = 8;
    panel_set_border(s_list_panel, 0xFF008800, 1);

    node_t *footer = panel_create("explorer_footer", 0, 0, 588, 34, 0xFF0A1510);
    footer->layout_type = LAYOUT_HBOX;
    footer->padding[0] = 5; footer->padding[1] = 8;
    footer->padding[2] = 5; footer->padding[3] = 8;
    panel_set_border(footer, 0xFF008800, 1);
    s_status_label = label_create("explorer_status", 0, 0, "Ready", 0xFF00CC33);
    s_status_label->flex_weight = 1;
    node_add_child(footer, s_status_label);
    node_add_child(footer, previous);
    node_add_child(footer, next);
    node_add_child(s_win, header);
    node_add_child(s_win, location);
    node_add_child(s_win, toolbar);
    node_add_child(s_win, s_list_panel);
    node_add_child(s_win, footer);
    node_add_child(g_scene->root, s_win);
    refresh_entries();
    explorer_rebuild();
    window_manager_bring_to_front(s_win);
}

void app_explorer_init(void) {
    app_registry_add("File Explorer", "folder_icon", explorer_start);
}
