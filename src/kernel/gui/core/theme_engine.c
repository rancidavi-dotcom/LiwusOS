/*
 * gui/core/theme_engine.c
 *
 * Windows 98/95 (classic) theme — silver gray, navy blue titlebars,
 * beveled 3D borders. Retro desktop environment look.
 */
#include "theme_engine.h"

static uint32_t s_palette[THEME_COLOR_MAX];

void theme_engine_init(void) {
    /* Windows 98/95 classic palette (0xAARRGGBB) */
    s_palette[THEME_COLOR_BACKGROUND]      = 0xFF008080; /* Desktop background, classic teal */
    s_palette[THEME_COLOR_WINDOW_BG]       = 0xFFC0C0C0; /* Window body, silver gray */
    s_palette[THEME_COLOR_WINDOW_TITLEBAR] = 0xFF000080; /* Window titlebar, navy blue */
    s_palette[THEME_COLOR_WINDOW_BORDER]   = 0xFF808080; /* Window outer border */
    s_palette[THEME_COLOR_TEXT_PRIMARY]    = 0xFF000000; /* Primary text, black */
    s_palette[THEME_COLOR_TEXT_SECONDARY]  = 0xFF404040; /* Secondary text, dark gray */
    s_palette[THEME_COLOR_BUTTON_BG]       = 0xFFC0C0C0; /* Button background, silver */
    s_palette[THEME_COLOR_BUTTON_BG_HOVER] = 0xFFDFDFDF; /* Button hover, light gray */
    s_palette[THEME_COLOR_BUTTON_BG_PRESS] = 0xFF808080; /* Button pressed / 3D shadow */
    s_palette[THEME_COLOR_BUTTON_BORDER]   = 0xFFFFFFFF; /* Button border, hilite white */
    s_palette[THEME_COLOR_BUTTON_TEXT]     = 0xFF000000; /* Button text, black */
    s_palette[THEME_COLOR_CLOSE_BTN]       = 0xFFC0C0C0; /* Close button, gray (X glyph) */
    s_palette[THEME_COLOR_INPUT_BG]        = 0xFFFFFFFF; /* Input background, white */
    s_palette[THEME_COLOR_INPUT_BG_FOCUS]  = 0xFFFFFFFF; /* Input background, focused */
    s_palette[THEME_COLOR_INPUT_BORDER]    = 0xFF808080; /* Input border, gray */
    s_palette[THEME_COLOR_INPUT_TEXT]      = 0xFF000000; /* Input text, black */
    s_palette[THEME_COLOR_INPUT_CURSOR]    = 0xFF000000; /* Input cursor, black block */
}

uint32_t theme_engine_get_color(theme_color_id_t id) {
    if (id < 0 || id >= THEME_COLOR_MAX) return 0xFFFFFFFF;
    return s_palette[id];
}

void theme_engine_set_color(theme_color_id_t id, uint32_t color) {
    if (id >= 0 && id < THEME_COLOR_MAX) {
        s_palette[id] = color;
    }
}
