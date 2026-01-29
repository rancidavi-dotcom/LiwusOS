/*
 * gui/core/desktop.c — Ícones de aplicativos sobre o fundo do desktop.
 *
 * Estilo Windows 95: cada app vira um ícone pixelado 16x16 (do pacote
 * Chicago95) com rótulo branco + sombra preta logo abaixo. Clicar abre o
 * aplicativo. Ficam abaixo da barra de tarefas e das janelas.
 */
#include "desktop.h"
#include "../scene/node.h"
#include "../core/app_registry.h"
#include "../widgets/image_node.h"
#include "../widgets/label.h"
#include "../render/compositor.h"
#include "../assets/pixel_icons.h"
#include "kheap.h"
#include "string.h"

#define ICON_W 100
#define ICON_H 44
#define ICON_GAP_X 12
#define ICON_GAP_Y 16
#define ICON_MARGIN 18
#define ICONS_PER_ROW 4

extern scene_graph_t *g_scene;

typedef struct {
    uint32_t index;
} desktop_icon_data_t;

static const pixel_icon_t *find_pixel_icon(const char *name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < sizeof(pixel_icons) / sizeof(pixel_icons[0]); i++) {
        if (!strcmp(pixel_icons[i].name, name)) return &pixel_icons[i];
    }
    return NULL;
}

static const pixel_icon_t *find_icon_for_app(const app_descriptor_t *app) {
    static const char *const map[][2] = {
        {"settings_icon",     "settings"},
        {"Settings",          "settings"},
        {"audio_icon",        "multimedia"},
        {"Multimedia",        "multimedia"},
        {"folder_icon",       "explorer"},
        {"File Explorer",     "explorer"},
        {"text_icon",         "editor"},
        {"Editor de Texto",   "editor"},
        {"Terminal",          "terminal"},
        {"Liwus Desktop Engine", "lde"},
        {"Image Viewer",      "imageviewer"},
        {"Browser",           "browser"},
    };
    for (uint32_t j = 0; j < sizeof(map) / sizeof(map[0]); j++) {
        if ((app->icon && !strcmp(app->icon, map[j][0])) ||
            (app->name && !strcmp(app->name, map[j][0])))
            return find_pixel_icon(map[j][1]);
    }
    return find_pixel_icon("demo");
}

static void desktop_icon_draw(node_t *self, struct gui_renderer *r) {
    (void)r;
    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int sx = camera_world_to_screen_x(cam, pt.x);
    int sy = camera_world_to_screen_y(cam, pt.y);
    self->screen_bounds = rect_make(sx, sy, self->width, self->height);
}

static bool desktop_icon_event(node_t *self, const gui_event_t *e) {
    if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        desktop_icon_data_t *d = self->userdata;
        if (d) {
            const app_descriptor_t *app = app_registry_get(d->index);
            if (app && app->start) app->start();
        }
        return true;
    }
    return false;
}

static const node_vtable_t desktop_icon_vtable = {
    .draw     = desktop_icon_draw,
    .on_event = desktop_icon_event,
    .layout   = NULL,
    .destroy  = NULL,
};

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

        node_t *icon = node_create(NODE_GROUP, "desktop_icon");
        if (!icon) continue;

        icon->local_x = x;
        icon->local_y = y;
        icon->width = ICON_W;
        icon->height = ICON_H;
        icon->interactive = true;
        icon->vtable = &desktop_icon_vtable;

        desktop_icon_data_t *d = kmalloc(sizeof(desktop_icon_data_t));
        if (!d) { node_destroy(icon); continue; }
        d->index = i;
        icon->userdata = d;

        node_add_child(g_scene->root, icon);

        const pixel_icon_t *pic = find_icon_for_app(app);
        if (pic) {
            int ix = (ICON_W - PIXEL_ICON_W) / 2;
            int iy = 2;
            node_t *img = image_node_create("icon_img", PIXEL_ICON_W, PIXEL_ICON_H,
                                            (uint32_t *)pic->data);
            img->local_x = ix;
            img->local_y = iy;
            img->interactive = false;
            node_add_child(icon, img);
        }

        int label_w = (int)strlen(app->name) * 8;
        int lx = (ICON_W - label_w) / 2;
        int ly = PIXEL_ICON_W + 6;

        node_t *shadow = label_create("icon_shadow", lx + 1, ly + 1, app->name, 0xFF000000);
        shadow->interactive = false;
        node_add_child(icon, shadow);

        node_t *label = label_create("icon_label", lx, ly, app->name, 0xFFFFFFFF);
        label->interactive = false;
        node_add_child(icon, label);

        (void)screen_w;
    }
}