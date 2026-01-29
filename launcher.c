#include "launcher.h"
#include "video.h"
#include "string.h"

static widget_t* launcher_win;
static widget_t** system_apps;
static int system_app_count;

void on_app_item_click(widget_t* self) {
    for (int i = 0; i < system_app_count; i++) {
        if (strcmp(system_apps[i]->text, self->text) == 0) {
            system_apps[i]->minimized = false;
            system_apps[i]->visible = true;
            system_apps[i]->focused = true;
            launcher_win->visible = false;
            return;
        }
    }
}

widget_t* init_launcher(widget_t* all_apps[], int count) {
    system_apps = all_apps;
    system_app_count = count;

    launcher_win = create_window("Menu de Aplicativos", 100, 100, 300, 450);
    launcher_win->visible = false;

    for (int i = 0; i < count; i++) {
        if (all_apps[i]->type == TYPE_WINDOW) {
            add_widget(launcher_win, create_button(all_apps[i]->text, 10, 10 + (i * 40), 280, 35, on_app_item_click));
        }
    }

    return launcher_win;
}

void toggle_launcher() {
    launcher_win->visible = !launcher_win->visible;
    if (launcher_win->visible) launcher_win->focused = true;
}
