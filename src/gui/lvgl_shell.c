#include "lvgl_shell.h"

#include "book.h"
#include "browser.h"
#include "compositor.h"
#include "editor.h"
#include "explorer.h"
#include "kheap.h"
#include "settings.h"
#include "string.h"
#include "terminal.h"
#include "video.h"
#include <lvgl.h>

static wl_surface_t *shell_surface = NULL;
static wl_buffer_t shell_buffer;
static lv_display_t *shell_display = NULL;
static lv_indev_t *shell_pointer = NULL;

static lv_obj_t *desktop_root = NULL;
static lv_obj_t *terminal_window = NULL;
static lv_obj_t *terminal_output = NULL;
static lv_obj_t *terminal_input = NULL;
static lv_obj_t *terminal_status = NULL;

static int pointer_x = 0;
static int pointer_y = 0;
static bool pointer_pressed = false;
static uint32_t last_tick = 0;
static bool shell_dirty = false;
static bool shell_ready = false;

static void lvgl_shell_mark_dirty(void) { shell_dirty = true; }

static void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area,
                          uint8_t *px_map) {
  (void)area;
  (void)px_map;
  lv_display_flush_ready(disp);
}

static void lvgl_pointer_read(lv_indev_t *indev, lv_indev_data_t *data) {
  (void)indev;
  data->point.x = pointer_x;
  data->point.y = pointer_y;
  data->state =
      pointer_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
  data->continue_reading = false;
}

static void lvgl_open_terminal(lv_event_t *e) {
  (void)e;
  if (terminal_window) {
    lv_obj_clear_flag(terminal_window, LV_OBJ_FLAG_HIDDEN);
    lvgl_shell_mark_dirty();
  }
}

static void lvgl_open_files(lv_event_t *e) {
  (void)e;
  open_explorer();
}

static void lvgl_open_editor(lv_event_t *e) {
  (void)e;
  open_editor();
}

static void lvgl_open_web(lv_event_t *e) {
  (void)e;
  open_browser();
}

static void lvgl_open_settings(lv_event_t *e) {
  (void)e;
  open_settings();
}

static void lvgl_open_book(lv_event_t *e) {
  (void)e;
  open_book();
}

static void lvgl_close_terminal(lv_event_t *e) {
  (void)e;
  if (terminal_window) {
    lv_obj_add_flag(terminal_window, LV_OBJ_FLAG_HIDDEN);
    lvgl_shell_mark_dirty();
  }
}

static void lvgl_submit_terminal(lv_event_t *e) {
  char line[256];
  const char *text;

  (void)e;

  if (!terminal_input) {
    return;
  }

  text = lv_textarea_get_text(terminal_input);
  if (!text) {
    return;
  }

  strncpy(line, text, sizeof(line) - 1);
  line[sizeof(line) - 1] = '\0';

  if (line[0]) {
    terminal_submit_line(line);
  }

  lv_textarea_set_text(terminal_input, "");
  lvgl_shell_mark_dirty();
}

static void lvgl_input_ready(lv_event_t *e) {
  if (lv_event_get_code(e) == LV_EVENT_READY) {
    lvgl_submit_terminal(e);
  }
}

static lv_obj_t *lvgl_make_button(lv_obj_t *parent, int x, int y, int w,
                                  const char *label_text,
                                  lv_event_cb_t cb) {
  lv_obj_t *btn = lv_button_create(parent);
  lv_obj_t *label = lv_label_create(btn);

  lv_obj_set_pos(btn, x, y);
  lv_obj_set_size(btn, w, 30);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_radius(btn, 6, 0);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x202833), 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_label_set_text(label, label_text);
  lv_obj_center(label);
  return btn;
}

static void lvgl_build_ui(void) {
  lv_obj_t *topbar;
  lv_obj_t *title;
  lv_obj_t *hero;
  lv_obj_t *hero_title;
  lv_obj_t *hero_text;
  lv_obj_t *term_title;
  lv_obj_t *close_btn;
  lv_obj_t *close_label;

  desktop_root = lv_screen_active();
  lv_obj_set_style_bg_color(desktop_root, lv_color_hex(0x10151b), 0);
  lv_obj_set_style_bg_opa(desktop_root, LV_OPA_COVER, 0);

  topbar = lv_obj_create(desktop_root);
  lv_obj_set_pos(topbar, 0, 0);
  lv_obj_set_size(topbar, (int32_t)screen_width, 44);
  lv_obj_set_style_radius(topbar, 0, 0);
  lv_obj_set_style_border_width(topbar, 0, 0);
  lv_obj_set_style_bg_color(topbar, lv_color_hex(0x171e26), 0);
  lv_obj_clear_flag(topbar, LV_OBJ_FLAG_SCROLLABLE);

  title = lv_label_create(topbar);
  lv_label_set_text(title, "LiwusOS LVGL");
  lv_obj_set_pos(title, 16, 12);
  lv_obj_set_style_text_color(title, lv_color_hex(0xf3f5f7), 0);

  lvgl_make_button(topbar, 170, 7, 84, "Terminal", lvgl_open_terminal);
  lvgl_make_button(topbar, 262, 7, 72, "Files", lvgl_open_files);
  lvgl_make_button(topbar, 342, 7, 72, "Edit", lvgl_open_editor);
  lvgl_make_button(topbar, 422, 7, 72, "Web", lvgl_open_web);
  lvgl_make_button(topbar, 502, 7, 84, "Config", lvgl_open_settings);
  lvgl_make_button(topbar, 594, 7, 72, "Book", lvgl_open_book);

  hero = lv_obj_create(desktop_root);
  lv_obj_set_pos(hero, 28, 72);
  lv_obj_set_size(hero, 380, 140);
  lv_obj_set_style_bg_color(hero, lv_color_hex(0x161d25), 0);
  lv_obj_set_style_border_width(hero, 0, 0);
  lv_obj_set_style_radius(hero, 10, 0);
  lv_obj_clear_flag(hero, LV_OBJ_FLAG_SCROLLABLE);

  hero_title = lv_label_create(hero);
  lv_label_set_text(hero_title, "Shell LVGL ativa");
  lv_obj_set_pos(hero_title, 18, 18);
  lv_obj_set_style_text_color(hero_title, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_text_font(hero_title, &lv_font_montserrat_18, 0);

  hero_text = lv_label_create(hero);
  lv_label_set_text(
      hero_text,
      "A GUI visivel do LiwusOS agora sobe em LVGL.\n"
      "Terminal, barra superior e shell principal ja vivem\n"
      "na camada nova. Explorer e outros apps continuam\n"
      "abrindo enquanto a gente migra o resto com seguranca.");
  lv_obj_set_pos(hero_text, 18, 54);
  lv_obj_set_style_text_color(hero_text, lv_color_hex(0xc1c8d0), 0);

  terminal_window = lv_obj_create(desktop_root);
  lv_obj_set_pos(terminal_window, 36, 236);
  lv_obj_set_size(terminal_window, 760, 330);
  lv_obj_set_style_bg_color(terminal_window, lv_color_hex(0x11161d), 0);
  lv_obj_set_style_border_color(terminal_window, lv_color_hex(0x293546), 0);
  lv_obj_set_style_border_width(terminal_window, 1, 0);
  lv_obj_set_style_radius(terminal_window, 8, 0);
  lv_obj_clear_flag(terminal_window, LV_OBJ_FLAG_SCROLLABLE);

  term_title = lv_label_create(terminal_window);
  lv_label_set_text(term_title, "Liwus Terminal");
  lv_obj_set_pos(term_title, 16, 12);
  lv_obj_set_style_text_color(term_title, lv_color_hex(0xffffff), 0);

  close_btn = lv_button_create(terminal_window);
  lv_obj_set_pos(close_btn, 716, 8);
  lv_obj_set_size(close_btn, 32, 24);
  lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x8a3030), 0);
  lv_obj_set_style_border_width(close_btn, 0, 0);
  lv_obj_add_event_cb(close_btn, lvgl_close_terminal, LV_EVENT_CLICKED, NULL);
  close_label = lv_label_create(close_btn);
  lv_label_set_text(close_label, "X");
  lv_obj_center(close_label);

  terminal_output = lv_textarea_create(terminal_window);
  lv_obj_set_pos(terminal_output, 16, 44);
  lv_obj_set_size(terminal_output, 728, 226);
  lv_textarea_set_text(terminal_output, "");
  lv_obj_set_style_bg_color(terminal_output, lv_color_hex(0x0a0f13), 0);
  lv_obj_set_style_text_color(terminal_output, lv_color_hex(0xd8e1ea), 0);
  lv_obj_set_style_border_width(terminal_output, 0, 0);
  lv_obj_set_style_radius(terminal_output, 6, 0);

  terminal_input = lv_textarea_create(terminal_window);
  lv_obj_set_pos(terminal_input, 16, 282);
  lv_obj_set_size(terminal_input, 620, 32);
  lv_textarea_set_one_line(terminal_input, true);
  lv_textarea_set_placeholder_text(terminal_input, "Digite um comando...");
  lv_obj_add_event_cb(terminal_input, lvgl_input_ready, LV_EVENT_READY, NULL);
  lv_obj_set_style_bg_color(terminal_input, lv_color_hex(0x18202a), 0);
  lv_obj_set_style_text_color(terminal_input, lv_color_hex(0xffffff), 0);
  lv_obj_set_style_border_width(terminal_input, 0, 0);
  lv_obj_set_style_radius(terminal_input, 6, 0);

  lvgl_make_button(terminal_window, 646, 282, 98, "Executar",
                   lvgl_submit_terminal);

  terminal_status = lv_label_create(terminal_window);
  lv_label_set_text(terminal_status, "SHELL");
  lv_obj_set_pos(terminal_status, 16, 255);
  lv_obj_set_style_text_color(terminal_status, lv_color_hex(0x8ea0b2), 0);
}

static void lvgl_sync_terminal(uint32_t now_ticks) {
  const char *text;
  const char *mode_text = "SHELL";

  if (!terminal_output || !terminal_status) {
    return;
  }

  text = terminal_get_text_view(now_ticks);
  lv_textarea_set_text(terminal_output, text ? text : "");
  lv_textarea_set_cursor_pos(terminal_output, LV_TEXTAREA_CURSOR_LAST);

  switch (terminal_get_mode()) {
  case 1:
    mode_text = "EDITOR";
    break;
  case 2:
    mode_text = "TOP";
    break;
  default:
    mode_text = "SHELL";
    break;
  }

  lv_label_set_text(terminal_status, mode_text);
  terminal_clear_dirty_output();
  lvgl_shell_mark_dirty();
}

void lvgl_shell_init(void) {
  if (shell_ready) {
    return;
  }

  lv_init();

  shell_buffer.width = screen_width;
  shell_buffer.height = screen_height;
  shell_buffer.pixels =
      (uint32_t *)kmalloc(screen_width * screen_height * sizeof(uint32_t));
  shell_buffer.shm = true;
  memset(shell_buffer.pixels, 0, screen_width * screen_height * sizeof(uint32_t));

  shell_surface =
      wl_create_surface(screen_width, screen_height, WL_SURFACE_BACKGROUND);
  shell_surface->x = 0;
  shell_surface->y = 0;
  wl_attach_buffer(shell_surface, &shell_buffer);

  shell_display = lv_display_create((int32_t)screen_width, (int32_t)screen_height);
  lv_display_set_buffers(shell_display, shell_buffer.pixels, NULL,
                         screen_width * screen_height * sizeof(uint32_t),
                         LV_DISPLAY_RENDER_MODE_DIRECT);
  lv_display_set_flush_cb(shell_display, lvgl_flush_cb);

  shell_pointer = lv_indev_create();
  lv_indev_set_type(shell_pointer, LV_INDEV_TYPE_POINTER);
  lv_indev_set_read_cb(shell_pointer, lvgl_pointer_read);
  lv_indev_set_display(shell_pointer, shell_display);

  lvgl_build_ui();
  shell_ready = true;
  shell_dirty = true;
}

void lvgl_shell_set_pointer(int x, int y, bool pressed) {
  pointer_x = x;
  pointer_y = y;
  pointer_pressed = pressed;
}

void lvgl_shell_handle_key(char key) {
  char text[2];

  if (!shell_ready || !terminal_window ||
      lv_obj_has_flag(terminal_window, LV_OBJ_FLAG_HIDDEN)) {
    return;
  }

  if (terminal_get_mode() != 0) {
    update_terminal_key(key);
    lvgl_sync_terminal(last_tick);
    return;
  }

  if (!terminal_input) {
    return;
  }

  if (key == '\n') {
    lvgl_submit_terminal(NULL);
    return;
  }

  if (key == '\b') {
    lv_textarea_delete_char(terminal_input);
    lvgl_shell_mark_dirty();
    return;
  }

  if ((unsigned char)key < 32) {
    return;
  }

  text[0] = key;
  text[1] = '\0';
  lv_textarea_add_text(terminal_input, text);
  lvgl_shell_mark_dirty();
}

void lvgl_shell_tick(uint32_t now_ticks) {
  uint32_t elapsed = now_ticks - last_tick;

  if (!shell_ready) {
    return;
  }

  if (elapsed > 0) {
    lv_tick_inc(elapsed * 10U);
    last_tick = now_ticks;
  }

  if (terminal_has_dirty_output() || terminal_get_mode() == 2) {
    lvgl_sync_terminal(now_ticks);
  }

  lv_timer_handler();

  if (shell_dirty) {
    wl_commit(shell_surface);
    shell_dirty = false;
  }
}

void lvgl_shell_show_terminal(void) {
  if (!shell_ready || !terminal_window) {
    return;
  }
  lv_obj_clear_flag(terminal_window, LV_OBJ_FLAG_HIDDEN);
  lvgl_sync_terminal(last_tick);
}

int lvgl_shell_enabled(void) { return 1; }
