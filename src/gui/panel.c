#include "compositor.h"
#include "gui.h"
#include "kheap.h"
#include "launcher.h"
#include "string.h"
#include "timer.h" // Para relógio
#include "video.h"

static wl_surface_t *panel_surface = NULL;
static wl_buffer_t panel_buffer;

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

  draw_string(x, y, time_str, 0xFFFFFF);
}

void on_apps_click(void *widget, void *surface) {
  (void)widget;
  (void)surface;
  toggle_launcher();
}

void panel_redraw() {
  if (!panel_surface)
    return;

  video_set_target(panel_buffer.pixels, panel_buffer.width,
                   panel_buffer.height);

  // Fundo Gradiente ou Sólido
  draw_rect(0, 0, panel_buffer.width, 30, 0x1E1E1E);
  draw_rect(0, 29, panel_buffer.width, 1, 0x555555); // Border bottom

  // Clock moved to the left
  draw_panel_clock(10, 8);

  // Lista de Janelas (Simulada por enquanto) - Moved slightly
  draw_string(100, 8, "[ Terminal ]", 0xAAAAAA);
  draw_string(200, 8, "[ Guia ]", 0xAAAAAA);

  video_reset_target();
  wl_commit(panel_surface);
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

  // Botão "Apps" na Direita, Branco sobre Preto
  void *btn =
      create_button("Apps", screen_width - 85, 4, 80, 22, on_apps_click);
  wl_widget_t *w_btn = (wl_widget_t *)btn;
  w_btn->color = 0xFF000000; // Black Background
  // Note: draw_string in compositor.c already uses White (0xFFFFFF)
  add_widget(panel_surface, btn);

  panel_redraw();
}
