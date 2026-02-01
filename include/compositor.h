#ifndef COMPOSITOR_H
#define COMPOSITOR_H

#include <stdbool.h>
#include <stdint.h>

/* --- LiwusWayland Protocol Definitions --- */

typedef struct wl_buffer {
  uint32_t width;
  uint32_t height;
  uint32_t *pixels; /* ARGB8888 */
  bool shm; /* Se verdadeiro, pixels aponta para memória compartilhada (heap) */
} wl_buffer_t;

typedef enum {
  WL_SURFACE_TOPLEVEL,
  WL_SURFACE_BACKGROUND,
  WL_SURFACE_PANEL,
  WL_SURFACE_CURSOR
} wl_surface_type_t;

typedef struct wl_surface {
  uint32_t id;
  int32_t x, y;
  uint32_t width, height;
  wl_surface_type_t type;

  wl_buffer_t *current_buffer;

  struct wl_surface *next; /* Linked list Z-Order (Front to Back) */
  struct wl_surface *prev;

  bool dirty; /* Precisa de redraw? */

  /* macOS Window Style Properties */
  char title[256];
  bool is_focused;
  bool is_minimized;
  bool is_maximized;

  /* Legacy Widget Compatibility */
  bool visible;
  uint32_t color;
  char *text;
#define focused is_focused /* Alias for legacy code */
#define minimized is_minimized
#define maximized is_maximized

  /* Theme & Visuals */
  bool traffic_light_hover;
  int hovered_button;       // -1 none, 0 close, 1 min, 2 max
  uint32_t titlebar_height; // 40 or 52

  /* Interactive Elements (Widgets) */
  struct wl_widget {
    int x, y, w, h;
    char text[64];
    void (*callback)(void *widget, void *surface);
    uint32_t color;
    bool hovered;
  } widgets[16];
  int widget_count;
} wl_surface_t;

typedef struct wl_widget wl_widget_t;

typedef struct wl_compositor {
  wl_surface_t *surfaces_head; /* Top-most surface */
  wl_surface_t *surfaces_tail; /* Bottom-most surface */

  wl_surface_t *focused_surface;
  wl_surface_t *mouse_focus;

  uint32_t screen_width;
  uint32_t screen_height;
} wl_compositor_t;

/* --- Compositor API (Kernel Side) --- */

void compositor_init();
void compositor_repaint(); /* Chama o LGX para desenhar */

/* Gerenciamento de Superfícies */
wl_surface_t *wl_create_surface(uint32_t w, uint32_t h, wl_surface_type_t type);
void wl_destroy_surface(wl_surface_t *surface);
void wl_attach_buffer(wl_surface_t *surface, wl_buffer_t *buffer);
void wl_commit(wl_surface_t *surface);

/* Input */
void wl_handle_mouse(int mx, int my, bool left, bool right);
void wl_handle_key(char key);

#endif
