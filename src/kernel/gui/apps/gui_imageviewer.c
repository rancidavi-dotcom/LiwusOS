/*
 * src/kernel/gui/apps/gui_imageviewer.c
 *
 * Kernel-side image viewer. Lists decoded image files present on the root
 * of the SDFS and opens them in a GUI window using the compositor's ARGB
 * image node. Decoding is done in ring 0 with the vendored stb_image
 * library (src/drivers/image.c), supporting PNG/JPEG/BMP/GIF/TGA/PSD/PNM.
 */

#include "gui_imageviewer.h"
#include "../core/app_registry.h"
#include "../scene/node.h"
#include "../widgets/window_node.h"
#include "../widgets/button.h"
#include "../widgets/label.h"
#include "../widgets/image_node.h"
#include "../layout/layout_engine.h"
#include "../window/window_manager.h"
#include "drivers/image.h"
#include "fs/sdfs.h"
#include "kernel/task.h"
#include "kheap.h"
#include "string.h"

#define VIEWER_MAX_FILES 128
#define VIEWER_MAX_DIRS  256
#define VIEWER_TITLE_H   30
#define VIEWER_PATH_MAX  192

extern uint32_t vga_fb_width, vga_fb_height;

static node_t *s_win = NULL;
static uint32_t s_last_file_count = 0;
static node_t *s_status_lbl = NULL;
static node_t *s_files[VIEWER_MAX_FILES];
static uint32_t s_file_count = 0;
/* Full path of each detected image; used for display and for opening. */
static char     s_file_names[VIEWER_MAX_FILES][VIEWER_PATH_MAX];

static int image_ext(const char *name) {
    if (!name) return 0;
    int len = (int)strlen(name);
    if (len >= 5) {
        const char *p = name + len - 4;
        if (p[0] == '.') {
            if ((p[1] == 'p'||p[1]=='P')&&(p[2]=='n'||p[2]=='N')&&(p[3]=='g'||p[3]=='G')) return 1;
            if ((p[1] == 'j'||p[1]=='J')&&(p[2]=='p'||p[2]=='P')&&(p[3]=='g'||p[3]=='G')) return 1;
            if ((p[1] == 'b'||p[1]=='B')&&(p[2]=='m'||p[2]=='M')&&(p[3]=='p'||p[3]=='P')) return 1;
            if ((p[1] == 'g'||p[1]=='G')&&(p[2]=='i'||p[2]=='I')&&(p[3]=='f'||p[3]=='F')) return 1;
            if ((p[1] == 't'||p[1]=='T')&&(p[2]=='g'||p[2]=='G')&&(p[3]=='a'||p[3]=='A')) return 1;
            if ((p[1] == 'p'||p[1]=='P')&&(p[2]=='s'||p[2]=='S')&&(p[3]=='d'||p[3]=='D')) return 1;
        }
    }
    if (len >= 6) {
        const char *p = name + len - 5;
        if (p[0] == '.' &&
            (p[1]=='j'||p[1]=='J')&&(p[2]=='p'||p[2]=='P')&&
            (p[3]=='e'||p[3]=='E')&&(p[4]=='g'||p[4]=='G')) return 1; /* .jpeg */
    }
    return 0;
}

static void image_click(node_t *btn, void *userdata) {
    (void)btn;
    uint32_t idx = (uint32_t)(uint64_t)userdata;
    if (idx >= s_file_count) return;

    char path[VIEWER_PATH_MAX];
    strncpy(path, s_file_names[idx], VIEWER_PATH_MAX - 1);
    path[VIEWER_PATH_MAX - 1] = '\0';

    if (s_status_lbl)
        label_set_text(s_status_lbl, "Decodificando...");

    uint32_t fsize = 0;
    void *fdata = sdfs_read_file(path, &fsize);
    if (!fdata || fsize == 0) {
        if (fdata) kfree(fdata);
        if (s_status_lbl) label_set_text(s_status_lbl, "Erro ao ler arquivo");
        return;
    }

    uint32_t *pixels = NULL;
    int w = 0, h = 0;
    int rc = image_decode((const uint8_t *)fdata, fsize, &pixels, &w, &h);
    kfree(fdata);
    if (rc != 0 || !pixels) {
        if (s_status_lbl) label_set_text(s_status_lbl, "Decodificacao falhou");
        return;
    }

    /* Fit the window on screen. */
    int dw = w, dh = h;
    int max_w = (int)vga_fb_width - 24;
    int max_h = (int)vga_fb_height - 24;
    if (dw > max_w || dh > max_h) {
        float sx = dw > max_w ? (float)max_w / (float)dw : 1.0f;
        float sy = dh > max_h ? (float)max_h / (float)dh : 1.0f;
        float s = sx < sy ? sx : sy;
        dw = (int)(dw * s);
        dh = (int)(dh * s);
    }
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;

    int x = 40, y = 40;
    if (x + dw > (int)vga_fb_width) x = (int)vga_fb_width - dw - 12;
    if (y + dh > (int)vga_fb_height) y = (int)vga_fb_height - dh - 12;
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    extern scene_graph_t *g_scene;
    node_t *img = image_node_create("image", dw, dh, pixels);
    image_free(pixels);
    if (!img) {
        if (s_status_lbl) label_set_text(s_status_lbl, "Erro ao criar janela");
        return;
    }

    node_t *win = window_node_create("win_image", x, y, dw, dh + VIEWER_TITLE_H,
                                     s_file_names[idx]);
    if (!win) {
        if (s_status_lbl) label_set_text(s_status_lbl, "Erro ao criar janela");
        return;
    }
    node_add_child(win, img);
    if (g_scene && g_scene->root)
        node_add_child(g_scene->root, win);
    window_manager_bring_to_front(win);
    layout_engine_compute(win);

    if (s_status_lbl) {
        char txt[96];
        char nb[16];
        strcpy(txt, "Mostrando: ");
        strncat(txt, s_file_names[idx], sizeof(txt) - strlen(txt) - 1);
        strncat(txt, " (", sizeof(txt) - strlen(txt) - 1);
        itoa(dw, nb, 10);      strncat(txt, nb, sizeof(txt) - strlen(txt) - 1);
        strncat(txt, "x", sizeof(txt) - strlen(txt) - 1);
        itoa(dh, nb, 10);      strncat(txt, nb, sizeof(txt) - strlen(txt) - 1);
        strncat(txt, ")", sizeof(txt) - strlen(txt) - 1);
        label_set_text(s_status_lbl, txt);
    }
    layout_engine_compute(s_win);
}

static void scan_images(void) {
    static char queue[VIEWER_MAX_DIRS][VIEWER_PATH_MAX];
    int head = 0, tail = 0;
    uint32_t idx = 0;
    char dir[VIEWER_PATH_MAX];

    if (sdfs_is_mounted()) {
        strncpy(queue[tail++], "/", VIEWER_PATH_MAX - 1);
        queue[tail - 1][VIEWER_PATH_MAX - 1] = '\0';
        if (tail >= VIEWER_MAX_DIRS) tail = 0;
    }

    while (head != tail && s_file_count < VIEWER_MAX_FILES) {
        strncpy(dir, queue[head++], VIEWER_PATH_MAX - 1);
        dir[VIEWER_PATH_MAX - 1] = '\0';
        if (head >= VIEWER_MAX_DIRS) head = 0;

        int rooty = (dir[0] == '/' && dir[1] == '\0');

        idx = 0;
        while (s_file_count < VIEWER_MAX_FILES) {
            char name[64];
            int is_dir;
            uint32_t size;
            if (sdfs_list_dir_entry(dir, (int)idx++, name, &is_dir, &size) != 0)
                break;

            size_t nlen = strlen(name);
            size_t dlen = strlen(dir);
            if (!rooty && dlen + 1 + nlen >= VIEWER_PATH_MAX - 1)
                continue;

            char child[VIEWER_PATH_MAX];
            if (rooty) {
                strcpy(child, "/");
                strncat(child, name, VIEWER_PATH_MAX - 2);
            } else {
                strcpy(child, dir);
                strncat(child, "/", VIEWER_PATH_MAX - strlen(child) - 1);
                strncat(child, name, VIEWER_PATH_MAX - strlen(child) - 1);
            }
            child[VIEWER_PATH_MAX - 1] = '\0';

            if (is_dir) {
                if ((tail + 1) % VIEWER_MAX_DIRS != head) {
                    strncpy(queue[tail++], child, VIEWER_PATH_MAX - 1);
                    queue[tail - 1][VIEWER_PATH_MAX - 1] = '\0';
                    if (tail >= VIEWER_MAX_DIRS) tail = 0;
                }
            } else if (image_ext(name)) {
                strncpy(s_file_names[s_file_count], child, VIEWER_PATH_MAX - 1);
                s_file_names[s_file_count][VIEWER_PATH_MAX - 1] = '\0';
                s_file_count++;
            }
        }
    }
}

static void rebuild_list(void) {
    node_t *rest[NODE_MAX_CHILDREN];
    uint32_t rest_count;
    uint32_t i;

    if (!s_win) return;

    for (i = 0; i < s_file_count; i++) {
        if (s_files[i]) {
            node_remove_child(s_win, s_files[i]);
            node_destroy(s_files[i]);
        }
    }
    s_file_count = 0;

    rest_count = s_win->child_count;
    for (i = 0; i < rest_count; i++)
        rest[i] = s_win->children[i];
    while (s_win->child_count > 0)
        node_remove_child(s_win, s_win->children[0]);

    for (i = 0; i < rest_count; i++) {
        if (rest[i] && rest[i]->type == NODE_LABEL)
            node_add_child(s_win, rest[i]);
    }

    scan_images();

    for (i = 0; i < s_file_count; i++) {
        node_t *b = button_create("imgfile", 0, 0, 580, 30, s_file_names[i]);
        if (!b) break;
        b->margin[2] = 6;
        b->layout_align = ALIGN_CENTER;
        button_set_on_click(b, image_click, (void *)(uint64_t)i);
        node_add_child(s_win, b);
        s_files[i] = b;
    }

    for (i = 0; i < rest_count; i++) {
        if (rest[i] && rest[i]->type != NODE_LABEL)
            node_add_child(s_win, rest[i]);
    }
    s_last_file_count = s_file_count;
}

static void image_refresh_task(void) {
    for (;;) {
        if (s_win && s_file_count != s_last_file_count) {
            s_last_file_count = s_file_count;
            rebuild_list();
            layout_engine_compute(s_win);
        }
        for (int k = 0; k < 8; k++) {
            switch_task();
        }
    }
}

static void image_viewer_start(void) {
    if (s_win) return;

    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return;

    s_win = window_node_create("win_viewer", 120, 60, 620, 480,
                               "Image Viewer");
    if (!s_win) return;
    s_win->layout_type = LAYOUT_VBOX;
    s_win->padding[0] = VIEWER_TITLE_H;
    s_win->padding[1] = 14;
    s_win->padding[2] = 14;
    s_win->padding[3] = 14;

    node_t *title = label_create("img_title", 0, 0,
        "Imagens (PNG / JPG / BMP / GIF / TGA / PSD)", 0xFF00FF41);
    title->margin[2] = 12;
    node_add_child(s_win, title);

    s_status_lbl = label_create("img_status", 0, 0,
                                "Selecione uma imagem", 0xFF00CC33);
    s_status_lbl->margin[2] = 8;
    node_add_child(s_win, s_status_lbl);

    rebuild_list();

    node_add_child(g_scene->root, s_win);
    window_manager_bring_to_front(s_win);
    layout_engine_compute(s_win);
}

void app_imageviewer_init(void) {
    create_task_named(image_refresh_task, "img_refresh");
    app_registry_add("Image Viewer", NULL, image_viewer_start);
}
