/*
 * gui/apps/gui_explorer.c
 *
 * Windows 95 / Chicago95 style File Explorer.
 * Features:
 *  - Left tree pane (folders with +/- expanders)
 *  - Right list/grid view with checkboxes
 *  - Toolbar: Back, Forward, Up, View modes, New Folder/File
 *  - Status bar with file count/size
 *  - Full SDFS integration (persistent disk)
 */
#include "gui_explorer.h"
#include "../core/app_registry.h"
#include "../scene/node.h"
#include "../widgets/window_node.h"
#include "../widgets/button.h"
#include "../widgets/checkbutton.h"
#include "../widgets/label.h"
#include "../widgets/panel.h"
#include "../widgets/image_node.h"
#include "../layout/layout_engine.h"
#include "../window/window_manager.h"
#include "../assets/pixel_controls.h"
#include "../assets/pixel_icons.h"
#include "../assets/asset_manager.h"
#include "../core/theme_engine.h"
#include "fs/sdfs.h"
#include "string.h"
#include "gui_text_editor.h"

#define EXP_MAX_ENTRIES   64
#define EXP_MAX_TREE_NODES 32
#define EXP_ICON_SIZE     32
#define EXP_LIST_ROW_H    28

typedef enum { VIEW_ICONS = 0, VIEW_LIST = 1, VIEW_DETAILS = 2 } view_mode_t;

typedef struct {
    char name[64];
    uint32_t size;
    int is_dir;
    bool selected;
} exp_entry_t;

typedef struct {
    char path[160];
    int depth;
    bool expanded;
    node_t *btn;      // tree button
    node_t *expander; // expander image node
} exp_tree_node_t;

static node_t *s_win = NULL;
static node_t *s_tree_panel = NULL;
static node_t *s_view_panel = NULL;
static node_t *s_path_label = NULL;
static node_t *s_status_label = NULL;
static node_t *s_usage_label = NULL;

static exp_entry_t s_entries[EXP_MAX_ENTRIES];
static uint32_t s_entry_count = 0;
static int32_t s_selected = -1;

static exp_tree_node_t s_tree[EXP_MAX_TREE_NODES];
static uint32_t s_tree_count = 0;
static char s_path[160] = "/";
static view_mode_t s_view_mode = VIEW_ICONS;

static void exp_rebuild_tree(void);
static void exp_rebuild_view(void);
static void exp_set_status(const char *text);
static void exp_update_usage(void);
static void exp_join_path(const char *name, char *out, uint32_t cap);
static void exp_go_parent(void);
static void exp_refresh_entries(void);

/* ----- Path helpers ----- */
static void exp_join_path(const char *name, char *out, uint32_t cap) {
    if (strcmp(s_path, "/") == 0) {
        out[0] = '/'; out[1] = '\0';
    } else {
        strncpy(out, s_path, cap - 1);
        out[cap - 1] = '\0';
    }
    uint32_t n = strlen(out);
    if (n && out[n - 1] != '/' && n + 1 < cap) {
        out[n++] = '/'; out[n] = '\0';
    }
    if (n < cap - 1) strncpy(out + n, name, cap - n - 1);
}

static void exp_go_parent(void) {
    if (strcmp(s_path, "/") == 0) return;
    uint32_t len = strlen(s_path);
    while (len > 1 && s_path[len - 1] != '/') len--;
    s_path[len <= 1 ? 1 : len] = '\0';
}

/* ----- Directory scanning ----- */
static void exp_refresh_entries(void) {
    s_entry_count = 0;
    s_selected = -1;
    if (!sdfs_is_mounted()) { exp_set_status("Disco indisponivel"); return; }
    for (int i = 0; i < EXP_MAX_ENTRIES; i++) {
        exp_entry_t *e = &s_entries[s_entry_count];
        if (sdfs_list_dir_entry(s_path, i, e->name, &e->is_dir, &e->size) != 0) break;
        e->selected = false;
        s_entry_count++;
    }
}

/* ----- Status / usage ----- */
static void exp_set_status(const char *text) {
    if (s_status_label) label_set_text(s_status_label, text ? text : "");
}
static void exp_update_usage(void) {
    if (!s_usage_label) return;
    char txt[64] = "SDFS  ";
    char num[16];
    uint32_t total = 0, used = 0;
    sdfs_get_usage(&total, &used);
    itoa((int)(used * SDFS_BLOCK_SIZE / 1024), num, 10);
    strcat(txt, num); strcat(txt, " KB / ");
    itoa((int)(total * SDFS_BLOCK_SIZE / 1024), num, 10);
    strcat(txt, num); strcat(txt, " KB");
    label_set_text(s_usage_label, txt);
}

/* ----- Tree expander toggle ----- */
static void exp_tree_toggle(node_t *btn, void *ud) {
    uint32_t idx = (uint32_t)(uint64_t)ud;
    if (idx >= s_tree_count) return;
    s_tree[idx].expanded = !s_tree[idx].expanded;
    exp_rebuild_tree();
    exp_rebuild_view();
}

/* ----- Tree node click (navigate) ----- */
static void exp_tree_click(node_t *btn, void *ud) {
    uint32_t idx = (uint32_t)(uint64_t)ud;
    if (idx >= s_tree_count) return;
    strncpy(s_path, s_tree[idx].path, sizeof(s_path) - 1);
    exp_refresh_entries();
    exp_rebuild_view();
}

/* ----- View entry click (button) ----- */
static void exp_entry_btn_click(node_t *btn, void *ud) {
    uint32_t idx = (uint32_t)(uint64_t)ud;
    if (idx >= s_entry_count) return;
    s_selected = (int32_t)idx;
    s_entries[idx].selected = !s_entries[idx].selected;
    node_mark_dirty(btn, NODE_DIRTY_PAINT);
    char msg[96] = "Selecionado: ";
    strcat(msg, s_entries[idx].name);
    if (s_entries[idx].is_dir) strcat(msg, " (pasta)");
    exp_set_status(msg);
}

/* ----- View entry click (checkbox toggle) ----- */
static void exp_entry_toggle(node_t *btn, void *ud, bool checked) {
    (void)checked;
    uint32_t idx = (uint32_t)(uint64_t)ud;
    if (idx >= s_entry_count) return;
    s_selected = (int32_t)idx;
    s_entries[idx].selected = !s_entries[idx].selected;
    node_mark_dirty(btn, NODE_DIRTY_PAINT);
    char msg[96] = "Selecionado: ";
    strcat(msg, s_entries[idx].name);
    if (s_entries[idx].is_dir) strcat(msg, " (pasta)");
    exp_set_status(msg);
}

/* ----- View entry double-click (open) ----- */
static void exp_entry_dblclick(node_t *btn, void *ud) {
    uint32_t idx = (uint32_t)(uint64_t)ud;
    if (idx >= s_entry_count) return;
    exp_entry_t *e = &s_entries[idx];
    if (e->is_dir) {
        char full[sizeof(s_path)];
        exp_join_path(e->name, full, sizeof(full));
        strncpy(s_path, full, sizeof(s_path) - 1);
        exp_refresh_entries();
        exp_rebuild_tree();
        exp_rebuild_view();
        return;
    }
    char full[sizeof(s_path)];
    exp_join_path(e->name, full, sizeof(full));
    text_editor_open_file(full);
}

/* ----- Toolbar actions ----- */
static void exp_up_click(node_t *b, void *ud) { (void)b; (void)ud; exp_go_parent(); exp_refresh_entries(); exp_rebuild_tree(); exp_rebuild_view(); }
static void exp_refresh_click(node_t *b, void *ud) { (void)b; (void)ud; exp_refresh_entries(); exp_rebuild_view(); exp_set_status("Atualizado"); }

static void exp_new_folder_click(node_t *b, void *ud) {
    (void)b; (void)ud;
    char name[32] = "Nova pasta"; char full[sizeof(s_path)];
    char num[8]; uint32_t suf = 1;
    while (1) {
        int exists = 0;
        for (uint32_t i = 0; i < s_entry_count; i++) if (strcmp(s_entries[i].name, name) == 0) { exists = 1; break; }
        if (!exists) break;
        strcpy(name, "Nova pasta "); itoa((int)++suf, num, 10); strcat(name, num);
    }
    exp_join_path(name, full, sizeof(full));
    if (sdfs_create_dir(full) == 0) { exp_refresh_entries(); exp_rebuild_view(); exp_set_status("Pasta criada"); }
    else exp_set_status("Falha ao criar pasta");
}

static void exp_new_file_click(node_t *b, void *ud) {
    (void)b; const char *base = "Novo arquivo"; const char *ext = ".txt";
    int kind = (int)(intptr_t)ud;
    if (kind == 1) { base = "Novo script"; ext = ".lua"; }
    if (kind == 2) { base = "Configuracao"; ext = ".cfg"; }
    char name[48]; char full[sizeof(s_path)]; char num[12]; uint32_t suf = 1;
    strcpy(name, base); strcat(name, ext);
    while (1) {
        int exists = 0;
        for (uint32_t i = 0; i < s_entry_count; i++) if (strcmp(s_entries[i].name, name) == 0) { exists = 1; break; }
        if (!exists) break;
        strcpy(name, base); strcat(name, " "); itoa((int)++suf, num, 10); strcat(name, num); strcat(name, ext);
    }
    exp_join_path(name, full, sizeof(full));
    if (sdfs_create_file(full) == 0 && sdfs_write_file(full, (uint8_t *)"", 0) == 0) {
        exp_refresh_entries(); exp_rebuild_view(); exp_set_status("Arquivo criado");
    } else exp_set_status("Falha ao criar arquivo");
}

static void exp_delete_click(node_t *b, void *ud) {
    (void)b; (void)ud;
    if (s_selected < 0 || (uint32_t)s_selected >= s_entry_count) { exp_set_status("Selecione um item"); return; }
    char full[sizeof(s_path)];
    exp_join_path(s_entries[s_selected].name, full, sizeof(full));
    if (sdfs_delete(full) == 0) { exp_refresh_entries(); exp_rebuild_view(); exp_set_status("Excluido"); }
    else exp_set_status("Falha (pasta nao vazia?)");
}

static void exp_view_mode_click(node_t *b, void *ud) {
    s_view_mode = (view_mode_t)(intptr_t)ud;
    exp_rebuild_view();
}

/* ----- Tree rebuild ----- */
static void exp_rebuild_tree(void) {
    if (!s_tree_panel) return;
    for (uint32_t i = 0; i < s_tree_count; i++) {
        if (s_tree[i].btn && s_tree[i].btn->parent) { node_remove_child(s_tree_panel, s_tree[i].btn); node_destroy(s_tree[i].btn); }
        if (s_tree[i].expander && s_tree[i].expander->parent) { node_remove_child(s_tree_panel, s_tree[i].expander); node_destroy(s_tree[i].expander); }
    }
    s_tree_count = 0;

    /* Build path components */
    char parts[16][64]; uint32_t part_count = 0;
    char tmp[160]; strncpy(tmp, s_path, sizeof(tmp)-1);
    char *p = tmp;
    while (*p && part_count < 16) {
        char *start = p;
        while (*p && *p != '/') p++;
        uint32_t len = p - start;
        if (len > 0) {
            strncpy(parts[part_count++], start, len < 63 ? len : 63);
            parts[part_count-1][len < 63 ? len : 63] = '\0';
        }
        if (*p == '/') p++;
    }

    /* Root entry */
    if (s_tree_count < EXP_MAX_TREE_NODES) {
        exp_tree_node_t *t = &s_tree[s_tree_count++];
        strcpy(t->path, "/"); t->depth = 0; t->expanded = true;
        const glyph_t *font = asset_manager_get_font(NULL);
        node_t *row = panel_create("tree_root", 0, 0, 180, 24, 0x00000000);
        row->layout_type = LAYOUT_HBOX; row->padding[0] = 2; row->padding[3] = 8;
        node_t *expander = image_node_create("tree_exp_root", 16, 16,
            (uint32_t *)pixel_controls[CONTROL_EXPANDER_MINUS].data);
        expander->interactive = false; node_add_child(row, expander); t->expander = expander;
        node_t *icon = image_node_create("tree_ico_root", 16, 16,
            (uint32_t *)pixel_icons[0].data); /* folder icon */
        icon->interactive = false; node_add_child(row, icon);
        node_t *lbl = label_create("tree_lbl_root", 0, 0, "Disco Local (C:)", 0xFFE6E8EB);
        lbl->flex_weight = 1; node_add_child(row, lbl);
        node_t *btn = button_create("tree_btn_root", 0, 0, 160, 22, "");
        btn->interactive = true; btn->userdata = t; button_set_on_click(btn, exp_tree_click, (void *)(uint64_t)(s_tree_count-1));
        node_add_child(row, btn); t->btn = btn;
        node_add_child(s_tree_panel, row);
    }

    /* Path components */
    char accum[160] = "/";
    for (uint32_t i = 0; i < part_count && s_tree_count < EXP_MAX_TREE_NODES; i++) {
        if (strcmp(accum, "/") != 0) { strcat(accum, "/"); }
        strcat(accum, parts[i]);

        exp_tree_node_t *t = &s_tree[s_tree_count++];
        strncpy(t->path, accum, sizeof(t->path)-1);
        t->depth = i + 1;
        t->expanded = true;

        node_t *row = panel_create("tree_row", 0, 0, 180, 24, 0x00000000);
        row->layout_type = LAYOUT_HBOX; row->padding[0] = 2; row->padding[3] = 8 + t->depth * 12;

        node_t *expander = image_node_create("tree_exp", 16, 16,
            (uint32_t *)pixel_controls[t->expanded ? CONTROL_EXPANDER_MINUS : CONTROL_EXPANDER_PLUS].data);
        expander->interactive = false; node_add_child(row, expander); t->expander = expander;
        node_t *btn_exp = button_create("tree_exp_btn", 0, 0, 16, 16, "");
        btn_exp->userdata = (void *)(uint64_t)(s_tree_count-1); button_set_on_click(btn_exp, exp_tree_toggle, btn_exp->userdata);
        node_add_child(row, btn_exp);

        node_t *icon = image_node_create("tree_ico", 16, 16,
            (uint32_t *)pixel_icons[0].data);
        icon->interactive = false; node_add_child(row, icon);
        node_t *lbl = label_create("tree_lbl", 0, 0, parts[i], 0xFFE6E8EB);
        lbl->flex_weight = 1; node_add_child(row, lbl);
        node_t *btn = button_create("tree_btn", 0, 0, 160, 22, "");
        btn->userdata = (void *)(uint64_t)(s_tree_count-1); button_set_on_click(btn, exp_tree_click, btn->userdata);
        node_add_child(row, btn); t->btn = btn;

        node_add_child(s_tree_panel, row);
    }
    layout_engine_compute(s_tree_panel);
}

/* ----- View rebuild (icons / list / details) ----- */
static void exp_rebuild_view(void) {
    if (!s_view_panel) return;
    for (uint32_t i = 0; i < s_view_panel->child_count; i++) {
        node_t *c = s_view_panel->children[i];
        if (c) node_destroy(c);
    }
    s_view_panel->child_count = 0;

    char path_txt[192] = "Local: "; strcat(path_txt, s_path);
    if (s_path_label) label_set_text(s_path_label, path_txt);

    const glyph_t *font = asset_manager_get_font(NULL);
    const pixel_control_t *folder_px = &pixel_controls[CONTROL_FOLDER];
    const pixel_control_t *file_px = &pixel_controls[CONTROL_FILE];

    if (s_view_mode == VIEW_ICONS) {
        s_view_panel->layout_type = LAYOUT_HBOX; /* grid via wrap not supported -> use VBOX of rows */
        s_view_panel->layout_type = LAYOUT_VBOX;
        uint32_t per_row = 4;
        uint32_t row_idx = 0;
        node_t *current_row = NULL;
        for (uint32_t i = 0; i < s_entry_count; i++) {
            if (i % per_row == 0) {
                current_row = panel_create("icon_row", 0, 0, 0, 80, 0x00000000);
                current_row->layout_type = LAYOUT_HBOX;
                current_row->flex_weight = 1;
                node_add_child(s_view_panel, current_row);
            }
            exp_entry_t *e = &s_entries[i];
            node_t *cell = panel_create("icon_cell", 0, 0, 120, 80, e->selected ? 0xFF316AC5 : 0x00000000);
            cell->layout_type = LAYOUT_VBOX; cell->layout_align = ALIGN_CENTER; cell->padding[0] = 8; cell->padding[2] = 4;

            node_t *ico = image_node_create("icon_ico", EXP_ICON_SIZE, EXP_ICON_SIZE,
                (uint32_t *)(e->is_dir ? folder_px->data : file_px->data));
            ico->interactive = false; node_add_child(cell, ico);

            node_t *cb = checkbox_create("icon_cb", 0, 0, "", e->selected);
            cb->width = EXP_ICON_SIZE + 4; checkbutton_set_on_toggle(cb, exp_entry_toggle, (void *)(uint64_t)i);
            node_add_child(cell, cb);

            char short_name[32]; strncpy(short_name, e->name, 31); short_name[31]=0;
            node_t *lbl = label_create("icon_lbl", 0, 0, short_name, 0xFFE6E8EB);
            lbl->width = 112; lbl->layout_align = ALIGN_CENTER; node_add_child(cell, lbl);

            node_t *btn = button_create("icon_btn", 0, 0, 120, 80, "");
            btn->userdata = (void *)(uint64_t)i;
            button_set_on_click(btn, exp_entry_btn_click, btn->userdata);
            button_set_on_double_click(btn, exp_entry_dblclick, btn->userdata);
            node_add_child(cell, btn);

            node_add_child(current_row, cell);
        }
    } else if (s_view_mode == VIEW_LIST || s_view_mode == VIEW_DETAILS) {
        s_view_panel->layout_type = LAYOUT_VBOX;
        for (uint32_t i = 0; i < s_entry_count; i++) {
            exp_entry_t *e = &s_entries[i];
            node_t *row = panel_create("list_row", 0, 0, 500, EXP_LIST_ROW_H, e->selected ? 0xFF316AC5 : 0x00000000);
            row->layout_type = LAYOUT_HBOX; row->padding[0] = 4; row->padding[1] = 8; row->padding[2] = 4; row->padding[3] = 8;

            node_t *ico = image_node_create("list_ico", 16, 16,
                (uint32_t *)(e->is_dir ? folder_px->data : file_px->data));
            ico->interactive = false; node_add_child(row, ico);

            node_t *cb = checkbox_create("list_cb", 0, 0, "", e->selected);
            cb->width = 20; checkbutton_set_on_toggle(cb, exp_entry_toggle, (void *)(uint64_t)i);
            node_add_child(row, cb);

            node_t *lbl = label_create("list_lbl", 0, 0, e->name, 0xFFE6E8EB);
            lbl->flex_weight = 1; node_add_child(row, lbl);

            if (s_view_mode == VIEW_DETAILS) {
                char sz[24]; itoa((int)(e->size / 1024), sz, 10); strcat(sz, e->is_dir ? "" : " KB");
                node_t *sl = label_create("list_sz", 0, 0, sz, 0xFF99A1AF); sl->width = 70; sl->layout_align = ALIGN_END; node_add_child(row, sl);
                char mod[32] = "HOJE 12:00"; /* placeholder */
                node_t *ml = label_create("list_mod", 0, 0, mod, 0xFF99A1AF); ml->width = 100; ml->layout_align = ALIGN_END; node_add_child(row, ml);
            }

            node_t *btn = button_create("list_btn", 0, 0, 500, EXP_LIST_ROW_H, "");
            btn->userdata = (void *)(uint64_t)i;
            button_set_on_click(btn, exp_entry_btn_click, btn->userdata);
            button_set_on_double_click(btn, exp_entry_dblclick, btn->userdata);
            node_add_child(row, btn);

            node_add_child(s_view_panel, row);
        }
    }
    exp_update_usage();
    layout_engine_compute(s_view_panel);
}

/* ----- Window creation ----- */
static void explorer_start(void) {
    extern scene_graph_t *g_scene;
    if (s_win || !g_scene || !g_scene->root) return;

    s_win = window_node_create("file_explorer", 100, 50, 800, 600, "File Explorer");
    if (!s_win) return;
    s_win->layout_type = LAYOUT_VBOX;
    s_win->padding[0] = 30; s_win->padding[1] = 0; s_win->padding[2] = 0; s_win->padding[3] = 0;

    /* Toolbar */
    node_t *toolbar = panel_create("exp_toolbar", 0, 0, 770, 42, 0xFF2D333D);
    toolbar->layout_type = LAYOUT_HBOX; toolbar->padding[0] = 4; toolbar->padding[1] = 8;
    toolbar->padding[2] = 4; toolbar->padding[3] = 8; panel_set_border(toolbar, 0xFF4A5260, 1);

    node_t *back = button_create("tb_back", 0, 0, 60, 30, "Voltar");
    node_t *fwd = button_create("tb_fwd", 0, 0, 60, 30, "Avancar");
    node_t *up = button_create("tb_up", 0, 0, 60, 30, "Subir");
    button_set_on_click(up, exp_up_click, NULL);

    node_t *sp1 = panel_create("tb_sp1", 0, 0, 12, 30, 0x00000000); sp1->layout_type = LAYOUT_HBOX;
    node_t *view_ico = button_create("tb_view_ico", 0, 0, 70, 30, "Icones");
    node_t *view_lst = button_create("tb_view_lst", 0, 0, 70, 30, "Lista");
    node_t *view_det = button_create("tb_view_det", 0, 0, 80, 30, "Detalhes");
    button_set_on_click(view_ico, exp_view_mode_click, (void *)VIEW_ICONS);
    button_set_on_click(view_lst, exp_view_mode_click, (void *)VIEW_LIST);
    button_set_on_click(view_det, exp_view_mode_click, (void *)VIEW_DETAILS);

    node_t *sp2 = panel_create("tb_sp2", 0, 0, 20, 30, 0x00000000);
    node_t *nfolder = button_create("tb_nfolder", 0, 0, 90, 30, "+ Pasta");
    node_t *nfile = button_create("tb_nfile", 0, 0, 80, 30, "+ TXT");
    node_t *nlua = button_create("tb_nlua", 0, 0, 70, 30, "+ LUA");
    node_t *ncfg = button_create("tb_ncfg", 0, 0, 70, 30, "+ CFG");
    button_set_on_click(nfolder, exp_new_folder_click, NULL);
    button_set_on_click(nfile, exp_new_file_click, (void *)0);
    button_set_on_click(nlua, exp_new_file_click, (void *)1);
    button_set_on_click(ncfg, exp_new_file_click, (void *)2);

    node_t *sp3 = panel_create("tb_sp3", 0, 0, 20, 30, 0x00000000);
    node_t *del = button_create("tb_del", 0, 0, 60, 30, "Excluir");
    node_t *ref = button_create("tb_ref", 0, 0, 60, 30, "Atualizar");
    button_set_on_click(del, exp_delete_click, NULL);
    button_set_on_click(ref, exp_refresh_click, NULL);

    node_t *spacer = panel_create("tb_spacer", 0, 0, 1, 30, 0x00000000); spacer->flex_weight = 1;

    node_add_child(toolbar, back); node_add_child(toolbar, fwd); node_add_child(toolbar, up);
    node_add_child(toolbar, sp1);
    node_add_child(toolbar, view_ico); node_add_child(toolbar, view_lst); node_add_child(toolbar, view_det);
    node_add_child(toolbar, sp2);
    node_add_child(toolbar, nfolder); node_add_child(toolbar, nfile); node_add_child(toolbar, nlua); node_add_child(toolbar, ncfg);
    node_add_child(toolbar, sp3);
    node_add_child(toolbar, del); node_add_child(toolbar, ref);
    node_add_child(toolbar, spacer);

    /* Main splitter area: tree | view */
    node_t *split = panel_create("exp_split", 0, 0, 770, 460, 0x00000000);
    split->layout_type = LAYOUT_HBOX;

    /* Tree panel (left) */
    s_tree_panel = panel_create("exp_tree", 0, 0, 200, 460, 0xFF1B1E24);
    s_tree_panel->layout_type = LAYOUT_VBOX; s_tree_panel->flex_weight = 0;
    s_tree_panel->padding[0] = 4; s_tree_panel->padding[1] = 4; s_tree_panel->padding[2] = 4; s_tree_panel->padding[3] = 4;
    panel_set_border(s_tree_panel, 0xFF4A5260, 1);

    /* View panel (right) */
    s_view_panel = panel_create("exp_view", 0, 0, 570, 460, 0xFF15181E);
    s_view_panel->layout_type = LAYOUT_VBOX; s_view_panel->flex_weight = 1;
    s_view_panel->padding[0] = 8; s_view_panel->padding[1] = 8; s_view_panel->padding[2] = 8; s_view_panel->padding[3] = 8;
    panel_set_border(s_view_panel, 0xFF4A5260, 1);

    node_add_child(split, s_tree_panel);
    node_add_child(split, s_view_panel);

    /* Location bar */
    node_t *loc = panel_create("exp_loc", 0, 0, 770, 30, 0xFF252A33);
    loc->layout_type = LAYOUT_HBOX; loc->padding[0] = 6; loc->padding[1] = 10; loc->padding[2] = 4; loc->padding[3] = 10;
    panel_set_border(loc, 0xFF4A5260, 1);
    s_path_label = label_create("exp_path", 0, 0, "Local: /", 0xFFE6E8EB);
    s_path_label->flex_weight = 1; node_add_child(loc, s_path_label);

    /* Status bar */
    node_t *status = panel_create("exp_status", 0, 0, 770, 28, 0xFF252A33);
    status->layout_type = LAYOUT_HBOX; status->padding[0] = 4; status->padding[1] = 8; status->padding[2] = 4; status->padding[3] = 8;
    panel_set_border(status, 0xFF4A5260, 1);
    s_status_label = label_create("exp_status_lbl", 0, 0, "Pronto", 0xFF99A1AF);
    s_status_label->flex_weight = 1;
    s_usage_label = label_create("exp_usage", 0, 0, "SDFS", 0xFF99A1AF);
    node_add_child(status, s_status_label);
    node_add_child(status, s_usage_label);

    /* Assemble */
    node_add_child(s_win, toolbar);
    node_add_child(s_win, split);
    node_add_child(s_win, loc);
    node_add_child(s_win, status);
    node_add_child(g_scene->root, s_win);

    exp_refresh_entries();
    exp_rebuild_tree();
    exp_rebuild_view();
    window_manager_bring_to_front(s_win);
}

void app_explorer_init(void) {
    app_registry_add("File Explorer", "folder_icon", explorer_start);
}