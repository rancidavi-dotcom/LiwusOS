/*
 * gui/core/theme_engine.c
 *
 * CRT Green Phosphor theme — IBM 5151 inspired.
 * Everything is green on black, evoking vintage monochrome monitors.
 */
#include "theme_engine.h"

static uint32_t s_palette[THEME_COLOR_MAX];

void theme_engine_init(void) {
    /* IBM 5151 green phosphor CRT palette */
    s_palette[THEME_COLOR_BACKGROUND]      = 0xFF0A0A12; /* Near-black with faint blue tint */
    s_palette[THEME_COLOR_WINDOW_BG]       = 0xFF0A1510; /* Dark green-black */
    s_palette[THEME_COLOR_WINDOW_TITLEBAR] = 0xFF0A2E1A; /* Dark green titlebar */
    s_palette[THEME_COLOR_WINDOW_BORDER]   = 0xFF00AA00; /* Phosphor green border */
    s_palette[THEME_COLOR_TEXT_PRIMARY]    = 0xFF00FF41; /* Bright phosphor green */
    s_palette[THEME_COLOR_TEXT_SECONDARY]  = 0xFF00CC33; /* Medium green */
    s_palette[THEME_COLOR_BUTTON_BG]       = 0xFF0A2E1A; /* Dark green button */
    s_palette[THEME_COLOR_BUTTON_BG_HOVER] = 0xFF1A4A2A; /* Lighter green hover */
    s_palette[THEME_COLOR_BUTTON_BG_PRESS] = 0xFF050A08; /* Very dark green press */
    s_palette[THEME_COLOR_BUTTON_BORDER]   = 0xFF00AA00; /* Phosphor green */
    s_palette[THEME_COLOR_BUTTON_TEXT]     = 0xFF00FF41; /* Bright green text */
    s_palette[THEME_COLOR_CLOSE_BTN]       = 0xFFFF4444; /* CRT red */
    s_palette[THEME_COLOR_INPUT_BG]        = 0xFF050A10; /* Near-black input */
    s_palette[THEME_COLOR_INPUT_BG_FOCUS]  = 0xFF0A1520; /* Slightly lighter focused */
    s_palette[THEME_COLOR_INPUT_BORDER]    = 0xFF008800; /* Dark green border */
    s_palette[THEME_COLOR_INPUT_TEXT]      = 0xFF00FF41; /* Bright green */
    s_palette[THEME_COLOR_INPUT_CURSOR]    = 0xFF00FF41; /* Bright green cursor */
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
