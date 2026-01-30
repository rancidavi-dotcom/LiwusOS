#ifndef GUI_H
#define GUI_H

#include "compositor.h"
#include "string.h"
#include <stdint.h>

#define TYPE_WINDOW WL_SURFACE_TOPLEVEL

void draw_button_visual(int x, int y, int w, int h, const char *text,
                        uint32_t color);

char get_last_key();
void open_terminal();

static inline bool is_inside(int mx, int my, int x, int y, int w, int h) {
  return (mx >= x && mx < x + w && my >= y && my < y + h);
}

/* Bridge for legacy widget system to new Wayland surface system */

typedef wl_surface_t widget_t;

static inline widget_t *create_window(const char *title, int x, int y, int w,
                                      int h) {
  widget_t *s = wl_create_surface(w, h, WL_SURFACE_TOPLEVEL);
  s->x = x;
  s->y = y;
  strncpy(s->title, title, 255);
  s->visible = true; // Default visible
  return s;
}

typedef struct {
  int x, y, w, h;
  char text[64];
  void (*callback)(void *widget, void *surface);
  uint32_t color;
} gui_temp_widget_t;

static inline void *create_label(const char *text, int x, int y,
                                 uint32_t color) {
  (void)text;
  (void)x;
  (void)y;
  (void)color;
  return (void *)0;
}

static inline void add_widget(widget_t *win, void *widget) {
  extern void *kmalloc(uint32_t size);
  extern void kfree(void *ptr);
  if (!win || !widget || win->widget_count >= 16)
    return;
  gui_temp_widget_t *tw = (gui_temp_widget_t *)widget;
  win->widgets[win->widget_count].x = tw->x;
  win->widgets[win->widget_count].y = tw->y;
  win->widgets[win->widget_count].w = tw->w;
  win->widgets[win->widget_count].h = tw->h;
  strncpy(win->widgets[win->widget_count].text, tw->text, 63);
  win->widgets[win->widget_count].callback = tw->callback;
  win->widgets[win->widget_count].color = tw->color;
  win->widget_count++;
  kfree(widget);
}

static inline void *create_button(const char *text, int x, int y, int w, int h,
                                  void *callback) {
  extern void *kmalloc(uint32_t size);
  gui_temp_widget_t *tw =
      (gui_temp_widget_t *)kmalloc(sizeof(gui_temp_widget_t));
  tw->x = x;
  tw->y = y;
  tw->w = w;
  tw->h = h;
  strncpy(tw->text, text, 63);
  tw->callback = (void (*)(void *, void *))callback;
  tw->color = 0xFF555555;
  return (void *)tw;
}

#endif
