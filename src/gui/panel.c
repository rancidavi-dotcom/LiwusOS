#include "compositor.h"
#include "editor.h"
#include "explorer.h"
#include "gui.h"
#include "kheap.h"
#include "launcher.h"
#include "settings.h"
#include "string.h"
#include "terminal.h"
#include "timer.h"
#include "video.h"

static wl_surface_t *panel_surface = NULL;
static wl_buffer_t panel_buffer;
static wl_surface_t *taskbar_targets[8];

#define PANEL_BG 0xEE10151C
#define PANEL_BORDER 0xFF212D3B
#define PANEL_TEXT 0xFFF3F6FA
#define PANEL_MUTED 0xFF7E8B9A
#define PANEL_ACTIVE 0xFF254A96
#define PANEL_BUTTON 0xFF1B2634
#define PANEL_BUTTON_HOT 0xFF233245

static void panel_draw_chip(int x, int y, int w, const char *text, uint32_t color) {
  draw_rect(x, y, w, 20, color);
  draw_rect(x, y, w, 1, PANEL_BORDER);
  draw_rect(x, y + 19, w, 1, PANEL_BORDER);
  draw_rect(x, y, 1, 20, PANEL_BORDER);
  draw_rect(x + w - 1, y, 1, 20, PANEL_BORDER);
  draw_string(x + 10, y + 4, text, PANEL_TEXT);
}

static void panel_set_widget(int index, int x, int y, int w, int h,
                             const char *text,
                             void (*callback)(void *, void *),
                             uint32_t color) {
  wl_widget_t *wdg;

  if (!panel_surface || index < 0 || index >= 16) {
    return;
  }

  wdg = &panel_surface->widgets[index];
  memset(wdg, 0, sizeof(*wdg));
  wdg->x = x;
  wdg->y = y;
  wdg->w = w;
  wdg->h = h;
  strncpy(wdg->text, text, sizeof(wdg->text) - 1);
  wdg->callback = callback;
  wdg->color = color;
  panel_surface->widget_count = index + 1;
}

void draw_panel_clock(int x, int y) {
  uint32_t total_s = timer_ticks / 100;
  uint32_t mins = (total_s / 60) % 60;
  uint32_t hours = (total_s / 3600) % 24;

  char time_str[16];
  char tmp[4];

  if (hours < 10)
    strcpy(time_str, "0");
  else
    strcpy(time_str, "");
  int_to_str(hours, tmp);
  strcat(time_str, tmp);
  strcat(time_str, ":");
  if (mins < 10)
    strcat(time_str, "0");
  int_to_str(mins, tmp);
  strcat(time_str, tmp);

  draw_string(x, y, time_str, PANEL_TEXT);
}

void on_apps_click(void *widget, void *surface) {
  (void)widget;
  (void)surface;
  toggle_launcher();
}

static void on_terminal_click(void *widget, void *surface) {
  (void)widget;
  (void)surface;
  open_terminal();
}

static void on_explorer_click(void *widget, void *surface) {
  (void)widget;
  (void)surface;
  open_explorer();
}

static void on_editor_click(void *widget, void *surface) {
  (void)widget;
  (void)surface;
  open_editor();
}

static void on_settings_click(void *widget, void *surface) {
  (void)widget;
  (void)surface;
  open_settings();
}

static void on_task_click(void *widget, void *surface) {
  (void)surface;
  if (!panel_surface || !widget) {
    return;
  }

  for (int i = 0; i < 8; i++) {
    if (&panel_surface->widgets[8 + i] == (wl_widget_t *)widget &&
        taskbar_targets[i]) {
      wl_toggle_taskbar_surface(taskbar_targets[i]);
      return;
    }
  }
}

static void panel_rebuild_widgets(void) {
  wl_surface_t *surfaces[8];
  int count;
  int x = 344;

  if (!panel_surface) {
    return;
  }

  memset(taskbar_targets, 0, sizeof(taskbar_targets));
  panel_surface->widget_count = 0;

  panel_set_widget(0, 8, 4, 64, 22, "Apps", on_apps_click, 0xFF274B9B);
  panel_set_widget(1, 80, 4, 72, 22, "Term", on_terminal_click, PANEL_BUTTON);
  panel_set_widget(2, 158, 4, 72, 22, "Files", on_explorer_click, PANEL_BUTTON);
  panel_set_widget(3, 236, 4, 72, 22, "Edit", on_editor_click, PANEL_BUTTON);
  panel_set_widget(4, 314, 4, 80, 22, "Config", on_settings_click,
                   PANEL_BUTTON);

  count = wl_list_taskbar_surfaces(surfaces, 8);
  for (int i = 0; i < count && i < 8; i++) {
    char label[20];
    const char *title = surfaces[i]->title[0] ? surfaces[i]->title : "Janela";
    int j = 0;

    while (title[j] && j < 15) {
      label[j] = title[j];
      j++;
    }
    label[j] = '\0';
    taskbar_targets[i] = surfaces[i];

    panel_set_widget(8 + i, x, 4, 110, 22, label, on_task_click,
                     surfaces[i]->visible ? PANEL_ACTIVE : PANEL_BUTTON_HOT);
    x += 116;
    if (x > (int)panel_buffer.width - 150) {
      break;
    }
  }
}

void panel_redraw() {
  if (!panel_surface)
    return;

  panel_rebuild_widgets();
  video_set_target(panel_buffer.pixels, panel_buffer.width,
                   panel_buffer.height);

  draw_rect(0, 0, panel_buffer.width, 30, PANEL_BG);
  draw_rect(0, 29, panel_buffer.width, 1, PANEL_BORDER);

  draw_string(560, 8, "Taskbar", PANEL_MUTED);
  draw_panel_clock(panel_buffer.width - 50, 8);

  video_reset_target();
}

void init_panel() {
  extern uint32_t screen_width;
  panel_buffer.width = screen_width;
  panel_buffer.height = 30;
  panel_buffer.pixels = (uint32_t *)kmalloc(screen_width * 30 * 4);
  panel_buffer.shm = true;

  panel_surface = wl_create_surface(screen_width, 30, WL_SURFACE_PANEL);
  panel_surface->x = 0;
  panel_surface->y = 0;

  wl_attach_buffer(panel_surface, &panel_buffer);

  panel_redraw();
}
