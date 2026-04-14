#include "compositor.h"
#include "kheap.h"
#include "lgx.h"
#include "mouse.h"
#include "string.h"
#include "video.h"

static wl_compositor_t compositor;
static bool is_resizing = false;
static int resize_start_w = 0;
static int resize_start_h = 0;
static int resize_start_mx = 0;
static int resize_start_my = 0;

#define SHELL_BG 0xFF0B1016
#define SHELL_BG_ALT 0xFF121A24
#define WINDOW_BG 0xFF111821
#define WINDOW_BORDER 0xFF263241
#define WINDOW_TITLE_ACTIVE 0xFF18212C
#define WINDOW_TITLE_INACTIVE 0xFF121922
#define WINDOW_TEXT 0xFFF4F7FB
#define WINDOW_MUTED 0xFF93A0B2
#define WIDGET_BG 0xFF315DCC
#define WIDGET_BG_HOVER 0xFF4475ED
#define TITLEBAR_HEIGHT 40
#define RESIZE_HANDLE 16
#define WORKAREA_TOP 30
#define WORKAREA_BOTTOM 50

static void compositor_draw_modern_button(int x, int y, int w, int h,
                                          const char *text, uint32_t color) {
  draw_rect(x, y, w, h, color);
  draw_rect(x, y, w, 1, WINDOW_BORDER);
  draw_rect(x, y + h - 1, w, 1, WINDOW_BORDER);
  draw_rect(x, y, 1, h, WINDOW_BORDER);
  draw_rect(x + w - 1, y, 1, h, WINDOW_BORDER);
  draw_rect(x, y + h - 1, w, 1, 0x50000000);
  draw_string(x + 12, y + 9, text, 0xFFFFFFFF);
}

static void compositor_draw_modern_window_frame(int wx, int wy, int ww, int wh,
                                                int title_h, bool active) {
  uint32_t title_color = active ? WINDOW_TITLE_ACTIVE : WINDOW_TITLE_INACTIVE;

  draw_rect(wx, wy, ww, wh + title_h, WINDOW_BG);
  draw_rect(wx, wy, ww, title_h, title_color);
  draw_rect(wx, wy, ww, 1, WINDOW_BORDER);
  draw_rect(wx, wy, 1, wh + title_h, WINDOW_BORDER);
  draw_rect(wx + ww - 1, wy, 1, wh + title_h, WINDOW_BORDER);
  draw_rect(wx, wy + wh + title_h - 1, ww, 1, WINDOW_BORDER);
  draw_rect(wx, wy + title_h - 1, ww, 1, WINDOW_BORDER);
  draw_rect(wx, wy + title_h, ww, wh, WINDOW_BG);
  draw_rect(wx + ww - RESIZE_HANDLE, wy + title_h + wh - 1, RESIZE_HANDLE, 1,
            WINDOW_MUTED);
  draw_rect(wx + ww - 1, wy + title_h + wh - RESIZE_HANDLE, 1, RESIZE_HANDLE,
            WINDOW_MUTED);
}

static int compositor_workarea_height(void) {
  int h = (int)compositor.screen_height - WORKAREA_TOP - WORKAREA_BOTTOM;
  return h > 120 ? h : (int)compositor.screen_height;
}

static void compositor_focus_surface(wl_surface_t *surface) {
  wl_surface_t *it = compositor.surfaces_head;
  while (it) {
    it->is_focused = false;
    it = it->next;
  }
  compositor.focused_surface = surface;
  if (surface) {
    surface->is_focused = true;
  }
}

static void compositor_unlink_surface(wl_surface_t *surface) {
  if (!surface) {
    return;
  }

  if (surface->prev) {
    surface->prev->next = surface->next;
  }
  if (surface->next) {
    surface->next->prev = surface->prev;
  }
  if (surface == compositor.surfaces_head) {
    compositor.surfaces_head = surface->next;
  }
  if (surface == compositor.surfaces_tail) {
    compositor.surfaces_tail = surface->prev;
  }
}

static void compositor_raise_surface(wl_surface_t *surface) {
  if (!surface || surface == compositor.surfaces_head) {
    compositor_focus_surface(surface);
    return;
  }

  compositor_unlink_surface(surface);
  surface->prev = NULL;
  surface->next = compositor.surfaces_head;
  if (compositor.surfaces_head) {
    compositor.surfaces_head->prev = surface;
  } else {
    compositor.surfaces_tail = surface;
  }
  compositor.surfaces_head = surface;
  compositor_focus_surface(surface);
}

static void compositor_toggle_maximize(wl_surface_t *surface) {
  if (!surface || surface->type != WL_SURFACE_TOPLEVEL) {
    return;
  }

  if (!surface->is_maximized) {
    surface->restore_x = surface->x;
    surface->restore_y = surface->y;
    surface->restore_width = surface->width;
    surface->restore_height = surface->height;
    surface->x = 0;
    surface->y = WORKAREA_TOP;
    surface->width = compositor.screen_width;
    surface->height = compositor_workarea_height() - TITLEBAR_HEIGHT;
    surface->is_maximized = true;
  } else {
    surface->x = surface->restore_x;
    surface->y = surface->restore_y;
    surface->width = surface->restore_width ? surface->restore_width : 640;
    surface->height = surface->restore_height ? surface->restore_height : 400;
    surface->is_maximized = false;
  }
}

static bool compositor_point_in_resize_handle(wl_surface_t *surface, int mx,
                                              int my) {
  int title_h;
  int handle_x;
  int handle_y;

  if (!surface || surface->type != WL_SURFACE_TOPLEVEL) {
    return false;
  }

  title_h = TITLEBAR_HEIGHT;
  handle_x = surface->x + (int)surface->width - RESIZE_HANDLE;
  handle_y = surface->y + title_h + (int)surface->height - RESIZE_HANDLE;

  return mx >= handle_x && my >= handle_y &&
         mx < surface->x + (int)surface->width &&
         my < surface->y + title_h + (int)surface->height;
}

/* --- LGX Integration --- */
extern lg_device_t global_lg_device;
extern lg_queue_t global_lg_queue;
extern lg_command_pool_t global_lg_pool;
extern lg_swapchain_t global_sw;
static lg_command_buffer_t comp_cmd = NULL;

void compositor_init() {
  extern uint32_t screen_width;
  extern uint32_t screen_height;
  compositor.screen_width = screen_width;
  compositor.screen_height = screen_height;
  compositor.surfaces_head = NULL;

  /* Inicializa recursos LGX para o compositor se necessário */
  if (global_lg_device && !comp_cmd) {
    lg_command_buffer_allocate_info_t cb_info = {global_lg_pool, 1};
    lg_allocate_command_buffers(global_lg_device, &cb_info, &comp_cmd);
  }
}

void compositor_repaint() {
  video_reset_target();
  clear_screen(SHELL_BG);
  draw_rect(0, 0, compositor.screen_width, 96, SHELL_BG_ALT);
  draw_rect(0, compositor.screen_height - 120, compositor.screen_width, 120,
            0xFF0F151D);

  if (!global_lg_device || !comp_cmd)
    return;

  lg_command_buffer_begin_info_t begin = {true};
  lg_begin_command_buffer(comp_cmd, &begin);
  lg_image_t sw_img = lg_get_swapchain_image(global_sw, 0);
  (void)sw_img;

  // Render Surfaces (Back to Front)
  wl_surface_t *s = compositor.surfaces_tail;
  while (s) {
    if (!s->visible) {
      s = s->prev;
      continue;
    }
    int wx = s->x;
    int wy = s->y;
    int ww = s->width;
    int wh = s->height;
    int title_h = (s->type == WL_SURFACE_TOPLEVEL) ? 40 : 0;

    if (s->type == WL_SURFACE_TOPLEVEL) {
      bool active = (s == compositor.focused_surface);
      compositor_draw_modern_window_frame(wx, wy, ww, wh, title_h, active);

      if (s->title[0]) {
        draw_string(wx + 88, wy + 12, s->title, active ? WINDOW_TEXT : WINDOW_MUTED);
      }

      int btn_y = wy + 12;
      int btn_w = 12;
      int btn_h = 12;
      int spacing = 18;
      int start_x = wx + 20;

      uint32_t c_red = active ? 0xFFE26767 : 0xFF495565;
      uint32_t c_yel = active ? 0xFFD8A84F : 0xFF495565;
      uint32_t c_grn = active ? 0xFF4FAF7A : 0xFF495565;

      draw_rect(start_x, btn_y, btn_w, btn_h, c_red);
      draw_rect(start_x + spacing, btn_y, btn_w, btn_h, c_yel);
      draw_rect(start_x + spacing * 2, btn_y, btn_w, btn_h, c_grn);
    }

    // 5. Content Rendering
    if (s->current_buffer && s->current_buffer->pixels) {
      video_blit(s->current_buffer->pixels, s->current_buffer->width, 0, 0, 
                 s->current_buffer->width, s->current_buffer->height, 
                 wx, wy + title_h);
    }

    // 6. Widget Rendering
    for (int i = 0; i < s->widget_count; i++) {
      int bx = wx + s->widgets[i].x;
      int by = wy + title_h + s->widgets[i].y;
      uint32_t color = s->widgets[i].hovered ? WIDGET_BG_HOVER : s->widgets[i].color;
      compositor_draw_modern_button(bx, by, s->widgets[i].w, s->widgets[i].h,
                                    s->widgets[i].text,
                                    color ? color : WIDGET_BG);
    }

    s = s->prev;
  }

  lg_end_command_buffer(comp_cmd);
  lg_submit_info_t submit = {1, &comp_cmd};
  lg_queue_submit(global_lg_queue, 1, &submit);

  int mx = get_mouse_x();
  int my = get_mouse_y();
  draw_mouse_cursor(mx, my, 0);

  lg_queue_present(global_lg_queue, global_sw, 0);
}

wl_surface_t *wl_create_surface(uint32_t w, uint32_t h,
                                wl_surface_type_t type) {
  wl_surface_t *s = (wl_surface_t *)kmalloc(sizeof(wl_surface_t));
  memset(s, 0, sizeof(wl_surface_t));
  static uint32_t id_counter = 1;
  s->id = id_counter++;
  s->width = w;
  s->height = h;
  s->type = type;
  s->visible = true;
  s->titlebar_height = TITLEBAR_HEIGHT;
  s->min_width = 220;
  s->min_height = 140;

  if (type == WL_SURFACE_BACKGROUND) {
    if (!compositor.surfaces_tail) {
      compositor.surfaces_head = s;
      compositor.surfaces_tail = s;
    } else {
      s->prev = compositor.surfaces_tail;
      compositor.surfaces_tail->next = s;
      compositor.surfaces_tail = s;
      s->next = NULL;
    }
  } else {
    if (!compositor.surfaces_head) {
      compositor.surfaces_head = s;
      compositor.surfaces_tail = s;
    } else {
      s->next = compositor.surfaces_head;
      compositor.surfaces_head->prev = s;
      compositor.surfaces_head = s;
      s->prev = NULL;
    }
    compositor_focus_surface(s);
  }
  return s;
}

void wl_attach_buffer(wl_surface_t *surface, wl_buffer_t *buffer) {
  if (surface) {
    surface->current_buffer = buffer;
  }
}

void wl_commit(wl_surface_t *surface) {
  if (surface) {
    surface->dirty = true;
    compositor_repaint();
  }
}

static int last_mx = 0, last_my = 0;
static bool is_dragging = false;

void wl_handle_mouse(int mx, int my, bool left, bool right) {
  (void)right;

  wl_surface_t *s = compositor.surfaces_head;
  wl_surface_t *hit = NULL;

  // Hit Test (Consider Titlebar)
  while (s) {
    if (!s->visible) {
      s = s->next;
      continue;
    }
    int th = (s->type == WL_SURFACE_TOPLEVEL) ? 40 : 0;
    if (mx >= s->x && mx < s->x + (int)s->width && my >= s->y &&
        my < s->y + (int)s->height + th) {
      hit = s;
      break;
    }
    s = s->next;
  }

  compositor.mouse_focus = hit;

  if (left && hit) {
    compositor_raise_surface(hit);
  }

  // Drag Logic (Stateful)
  if (left) {
    if (!is_dragging && hit && hit->type == WL_SURFACE_TOPLEVEL) {
      int ryc = my - hit->y;
      if (!hit->is_maximized &&
          compositor_point_in_resize_handle(hit, mx, my)) {
        is_resizing = true;
        resize_start_w = (int)hit->width;
        resize_start_h = (int)hit->height;
        resize_start_mx = mx;
        resize_start_my = my;
      } else if (ryc >= 0 && ryc < 40 && !hit->traffic_light_hover &&
                 !hit->is_maximized) {
        is_dragging = true;
      }
    }

    if (is_dragging && compositor.focused_surface) {
      compositor.focused_surface->x += (mx - last_mx);
      compositor.focused_surface->y += (my - last_my);
      compositor_repaint();
    } else if (is_resizing && compositor.focused_surface) {
      int new_w = resize_start_w + (mx - resize_start_mx);
      int new_h = resize_start_h + (my - resize_start_my);

      if (new_w < (int)compositor.focused_surface->min_width) {
        new_w = compositor.focused_surface->min_width;
      }
      if (new_h < (int)compositor.focused_surface->min_height) {
        new_h = compositor.focused_surface->min_height;
      }

      compositor.focused_surface->width = (uint32_t)new_w;
      compositor.focused_surface->height = (uint32_t)new_h;
      compositor_repaint();
    }
  } else {
    is_dragging = false;
    is_resizing = false;
  }

  // Button Hover Detection (Only if not dragging)
  if (!is_dragging && !is_resizing) {
    wl_surface_t *it = compositor.surfaces_head;
    while (it) {
      it->traffic_light_hover = false;
      it->hovered_button = -1;
      if (it->visible && it->type == WL_SURFACE_TOPLEVEL) {
        int rx = mx - it->x;
        int ry = my - it->y;
        if (rx >= 10 && rx <= 70 && ry >= 0 && ry <= 40) {
          it->traffic_light_hover = true;
          if (rx >= 14 && rx <= 26) it->hovered_button = 0;
          else if (rx >= 34 && rx <= 46) it->hovered_button = 1;
          else if (rx >= 54 && rx <= 66) it->hovered_button = 2;
        }
      }
      it = it->next;
    }
  }

  // Widget Interaction (Only if not dragging)
  if (!is_dragging && !is_resizing && hit) {
    int th = (hit->type == WL_SURFACE_TOPLEVEL) ? 40 : 0;
    int rx = mx - hit->x;
    int ry = my - (hit->y + th);
    for (int i = 0; i < hit->widget_count; i++) {
      bool inside = (rx >= hit->widgets[i].x &&
                     rx < hit->widgets[i].x + hit->widgets[i].w &&
                     ry >= hit->widgets[i].y &&
                     ry < hit->widgets[i].y + hit->widgets[i].h);
      hit->widgets[i].hovered = inside;
      if (left && inside && hit->widgets[i].callback) {
        hit->widgets[i].callback(&hit->widgets[i], hit);
        last_mx = mx; last_my = my; // Update but consume
        return; 
      }
    }
  }

  // Button Click Logic (Only if not dragging)
  if (!is_dragging && !is_resizing && left && hit && hit->traffic_light_hover &&
      hit->hovered_button != -1) {
      if (hit->hovered_button == 0) { // Close
        compositor_unlink_surface(hit);
        compositor_focus_surface(compositor.surfaces_head);
        kfree(hit);
        compositor_repaint();
      } else if (hit->hovered_button == 1) { // Minimize
        hit->visible = false;
        hit->is_minimized = true;
        compositor_focus_surface(compositor.surfaces_head);
        compositor_repaint();
      } else if (hit->hovered_button == 2) { // Maximize / restore
        compositor_toggle_maximize(hit);
        compositor_repaint();
      }
  }

  last_mx = mx;
  last_my = my;
}

void wl_handle_key(char key) {
  extern bool check_alt_f4();
  if (check_alt_f4() && compositor.focused_surface &&
      compositor.focused_surface->type == WL_SURFACE_TOPLEVEL) {
    wl_surface_t *tc = compositor.focused_surface;
    compositor_unlink_surface(tc);
    compositor_focus_surface(compositor.surfaces_head);
    kfree(tc);
    compositor_repaint();
    return;
  }
  (void)key;
}

int wl_list_taskbar_surfaces(wl_surface_t **out, int max_entries) {
  wl_surface_t *s = compositor.surfaces_tail;
  int count = 0;

  if (!out || max_entries <= 0) {
    return 0;
  }

  while (s && count < max_entries) {
    if (s->type == WL_SURFACE_TOPLEVEL) {
      out[count++] = s;
    }
    s = s->prev;
  }

  return count;
}

void wl_activate_surface(wl_surface_t *surface) {
  if (!surface) {
    return;
  }

  surface->visible = true;
  surface->is_minimized = false;
  compositor_raise_surface(surface);
}

void wl_toggle_taskbar_surface(wl_surface_t *surface) {
  if (!surface) {
    return;
  }

  if (surface->visible && surface == compositor.focused_surface) {
    surface->visible = false;
    surface->is_minimized = true;
    compositor_focus_surface(compositor.surfaces_head);
  } else {
    wl_activate_surface(surface);
  }
}
