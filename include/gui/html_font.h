#ifndef GUI_HTML_FONT_H
#define GUI_HTML_FONT_H

#include "lvgl.h"

extern lv_font_t g_html_font_12;
extern lv_font_t g_html_font_14;
extern lv_font_t g_html_font_16;
extern lv_font_t g_html_font_18;
extern lv_font_t g_html_font_20;
extern lv_font_t g_html_font_24;

void html_fonts_init(void);
const lv_font_t *html_font_for_px(int px);

#endif /* GUI_HTML_FONT_H */
