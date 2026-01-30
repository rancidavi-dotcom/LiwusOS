#include "launcher.h"
#include "book.h"
#include "browser.h"
#include "kheap.h"
#include "string.h"
#include "terminal.h"
#include "video.h"

static widget_t *launcher_win = NULL;
static widget_t **system_apps;
static int system_app_count;

void on_app_item_click(void *widget, void *surface) {
  (void)surface;
  wl_widget_t *btn = (wl_widget_t *)widget;

  if (strstr(btn->text, "Terminal")) {
    open_terminal();
  } else if (strstr(btn->text, "Book")) {
    open_book();
  } else if (strstr(btn->text, "Browser") || strstr(btn->text, "Web")) {
    open_browser();
  }

  launcher_win->visible = false;
}

widget_t *init_launcher(widget_t *all_apps[], int count) {
  system_apps = all_apps;
  system_app_count = count;

  // Design Premium: Janela elegante no centro
  launcher_win = create_window("Gerenciador de Apps", 200, 100, 400, 500);
  launcher_win->visible = false;
  launcher_win->color = 0xEE1A1A1A; // Dark translucency feel

  // Adiciona botões para os apps conhecidos
  add_widget(launcher_win,
             create_button("> Terminal", 10, 10, 380, 35, on_app_item_click));
  add_widget(launcher_win, create_button("> Book Reader", 10, 50, 380, 35,
                                         on_app_item_click));
  add_widget(launcher_win,
             create_button("> Settings", 10, 90, 380, 35, on_app_item_click));
  add_widget(launcher_win, create_button("> Web Browser", 10, 130, 380, 35,
                                         on_app_item_click));

  return launcher_win;
}

void toggle_launcher() {
  if (!launcher_win) {
    // Auto-init se não existir
    init_launcher(NULL, 0);
  }
  launcher_win->visible = !launcher_win->visible;
  if (launcher_win->visible) {
    launcher_win->is_focused = true;
    // Centraliza se necessário
  }
}
