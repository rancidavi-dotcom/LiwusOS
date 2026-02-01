#include "compositor.h"
#include "kheap.h"
#include "lgx.h"
#include "mouse.h"
#include "string.h"
#include "video.h"

static wl_compositor_t compositor;

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
  // 1. Limpar Background
  video_reset_target();
  clear_screen(0xFF333333);

  if (!global_lg_device || !comp_cmd)
    return;

  lg_command_buffer_begin_info_t begin = {true};
  lg_begin_command_buffer(comp_cmd, &begin);
  lg_image_t sw_img = lg_get_swapchain_image(global_sw, 0);

  // Render Surfaces (Back to Front)
  wl_surface_t *s = compositor.surfaces_tail;
  while (s) {
    int wx = s->x;
    int wy = s->y;
    int ww = s->width;
    int wh = s->height;
    int title_h = (s->type == WL_SURFACE_TOPLEVEL) ? 40 : 0;

    if (s->type == WL_SURFACE_TOPLEVEL) {
      // 1. Shadow
      draw_rect(wx + 4, wy + 4, ww, wh + title_h, 0x40000000);

      bool active = (s == compositor.focused_surface);
      uint32_t bg_color = 0xFFFFFFFF;
      uint32_t title_color = active ? 0xFFE5E5E5 : 0xFFF0F0F0;

      // 2. Window Body & Titlebar
      draw_rect(wx, wy + title_h, ww, wh, bg_color);
      draw_rect(wx, wy, ww, title_h, title_color);
      draw_rect(wx, wy + title_h - 1, ww, 1, 0xFFCCCCCC);

      // 3. Window Title
      if (s->title[0]) {
        int t_len = strlen(s->title) * 8;
        int t_x = wx + (ww - t_len) / 2;
        int t_y = wy + (title_h - 16) / 2;
        draw_string(t_x, t_y, s->title, active ? 0xFF000000 : 0xFF888888);
      }

      // 4. Traffic Lights (Buttons)
      int btn_y = wy + (title_h / 2);
      int btn_r = 6;
      int spacing = 20;
      int start_x = wx + 20;

      uint32_t c_red = active ? 0xFFFF5F57 : 0xFFDDDDDD;
      uint32_t c_yel = active ? 0xFFFFBD2E : 0xFFDDDDDD;
      uint32_t c_grn = active ? 0xFF28C840 : 0xFFDDDDDD;

      draw_filled_circle(start_x, btn_y, btn_r, c_red);
      draw_filled_circle(start_x + spacing, btn_y, btn_r, c_yel);
      draw_filled_circle(start_x + spacing * 2, btn_y, btn_r, c_grn);
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
      uint32_t color = s->widgets[i].hovered ? 0xFF888888 : s->widgets[i].color;
      draw_rect(bx, by, s->widgets[i].w, s->widgets[i].h, color);
      draw_rect(bx, by, s->widgets[i].w, 1, 0xC0FFFFFF);
      draw_rect(bx, by + s->widgets[i].h - 1, s->widgets[i].w, 1, 0x80000000);

      int tw = strlen(s->widgets[i].text) * 8;
      int tx = bx + (s->widgets[i].w - tw) / 2;
      int ty = by + (s->widgets[i].h - 16) / 2;
      draw_string(tx, ty, s->widgets[i].text, 0xFFFFFF);
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
    compositor.focused_surface = s;
  }
  return s;
}

void wl_attach_buffer(wl_surface_t *surface, wl_buffer_t *buffer) {
  if (surface) {
    surface->current_buffer = buffer;
    surface->width = buffer->width;
    surface->height = buffer->height;
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
    int th = (s->type == WL_SURFACE_TOPLEVEL) ? 40 : 0;
    if (mx >= s->x && mx < s->x + (int)s->width && my >= s->y &&
        my < s->y + (int)s->height + th) {
      hit = s;
      break;
    }
    s = s->next;
  }

  compositor.mouse_focus = hit;

  // Drag Logic (Stateful)
  if (left) {
    if (!is_dragging && hit && hit->type == WL_SURFACE_TOPLEVEL) {
      int ryc = my - hit->y;
      if (ryc >= 0 && ryc < 40 && !hit->traffic_light_hover) {
        is_dragging = true;
        compositor.focused_surface = hit;
        // Bring to Front logic... (moved here to happen once at start of drag)
        if (hit != compositor.surfaces_head) {
            if (hit->prev) hit->prev->next = hit->next;
            if (hit->next) hit->next->prev = hit->prev;
            if (hit == compositor.surfaces_tail) compositor.surfaces_tail = hit->prev;
            hit->next = compositor.surfaces_head;
            hit->prev = NULL;
            compositor.surfaces_head->prev = hit;
            compositor.surfaces_head = hit;
        }
      }
    }

    if (is_dragging && compositor.focused_surface) {
      compositor.focused_surface->x += (mx - last_mx);
      compositor.focused_surface->y += (my - last_my);
      compositor_repaint();
    }
  } else {
    is_dragging = false;
  }

  // Button Hover Detection (Only if not dragging)
  if (!is_dragging) {
    wl_surface_t *it = compositor.surfaces_head;
    while (it) {
      it->traffic_light_hover = false;
      it->hovered_button = -1;
      if (it->type == WL_SURFACE_TOPLEVEL) {
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
  if (!is_dragging && hit) {
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
  if (!is_dragging && left && hit && hit->traffic_light_hover && hit->hovered_button != -1) {
      if (hit->hovered_button == 0) { // Close
        if (hit->prev) hit->prev->next = hit->next;
        if (hit->next) hit->next->prev = hit->prev;
        if (hit == compositor.surfaces_head) compositor.surfaces_head = hit->next;
        if (hit == compositor.surfaces_tail) compositor.surfaces_tail = hit->prev;
        if (compositor.focused_surface == hit) compositor.focused_surface = compositor.surfaces_head;
        kfree(hit);
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
    if (tc->prev)
      tc->prev->next = tc->next;
    if (tc->next)
      tc->next->prev = tc->prev;
    if (tc == compositor.surfaces_head)
      compositor.surfaces_head = tc->next;
    if (tc == compositor.surfaces_tail)
      compositor.surfaces_tail = tc->prev;
    compositor.focused_surface = compositor.surfaces_head;
    kfree(tc);
    compositor_repaint();
    return;
  }
  (void)key;
}
