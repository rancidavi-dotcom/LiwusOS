/*
 * gui/core/theme_engine.c
 *
 * Modern dark theme — clean blue-gray palette.
 * Neutral backgrounds with white text and a blue accent.
 */
#include "theme_engine.h"

static uint32_t s_palette[THEME_COLOR_MAX];

void theme_engine_init(void) {
    /* Modern dark desktop palette (0xAARRGGBB) */
    s_palette[THEME_COLOR_BACKGROUND]      = 0xFF1E2229; /* Desktop background, dark blue-gray */
    s_palette[THEME_COLOR_WINDOW_BG]       = 0xFF252A33; /* Window body */
    s_palette[THEME_COLOR_WINDOW_TITLEBAR] = 0xFF2D333D; /* Window titlebar */
    s_palette[THEME_COLOR_WINDOW_BORDER]   = 0xFF3D4450; /* Window border */
    s_palette[THEME_COLOR_TEXT_PRIMARY]    = 0xFFE6E8EB; /* Primary text, near-white */
    s_palette[THEME_COLOR_TEXT_SECONDARY]  = 0xFF99A1AF; /* Secondary text, gray */
    s_palette[THEME_COLOR_BUTTON_BG]       = 0xFF323842; /* Button background */
    s_palette[THEME_COLOR_BUTTON_BG_HOVER] = 0xFF3D4450; /* Button hover */
    s_palette[THEME_COLOR_BUTTON_BG_PRESS] = 0xFF262B33; /* Button pressed */
    s_palette[THEME_COLOR_BUTTON_BORDER]   = 0xFF4A5260; /* Button border */
    s_palette[THEME_COLOR_BUTTON_TEXT]     = 0xFFE6E8EB; /* Button text */
    s_palette[THEME_COLOR_CLOSE_BTN]       = 0xFFFF5F57; /* Close button, soft red */
    s_palette[THEME_COLOR_INPUT_BG]        = 0xFF1B1E24; /* Input background */
    s_palette[THEME_COLOR_INPUT_BG_FOCUS]  = 0xFF23272F; /* Input background, focused */
    s_palette[THEME_COLOR_INPUT_BORDER]    = 0xFF4A5260; /* Input border */
    s_palette[THEME_COLOR_INPUT_TEXT]      = 0xFFE6E8EB; /* Input text */
    s_palette[THEME_COLOR_INPUT_CURSOR]    = 0xFF4A9EFF; /* Input cursor, blue accent */
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
