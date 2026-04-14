#include <lvgl.h>

static lv_theme_t lvgl_null_theme;
static bool lvgl_null_theme_init = false;

void lv_theme_apply(lv_obj_t *obj) { (void)obj; }

lv_theme_t *lv_theme_default_init(lv_display_t *disp, lv_color_t color_primary,
                                  lv_color_t color_secondary, bool dark,
                                  const lv_font_t *font) {
  lvgl_null_theme.disp = disp;
  lvgl_null_theme.color_primary = color_primary;
  lvgl_null_theme.color_secondary = color_secondary;
  lvgl_null_theme.font_small = font;
  lvgl_null_theme.font_normal = font;
  lvgl_null_theme.font_large = font;
  lvgl_null_theme.flags = dark ? 1U : 0U;
  lvgl_null_theme.apply_cb = NULL;
  lvgl_null_theme.parent = NULL;
  lvgl_null_theme.user_data = NULL;
  lvgl_null_theme_init = true;
  return &lvgl_null_theme;
}

lv_theme_t *lv_theme_default_get(void) {
  return lvgl_null_theme_init ? &lvgl_null_theme : NULL;
}

bool lv_theme_default_is_inited(void) { return lvgl_null_theme_init; }

void lv_theme_default_deinit(void) { lvgl_null_theme_init = false; }

void lv_bin_decoder_init(void) {}

void lv_span_stack_init(void) {}

void lv_span_stack_deinit(void) {}
