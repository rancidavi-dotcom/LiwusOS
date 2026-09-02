/*
 * gui/core/theme_engine.c
 */
#include "theme_engine.h"

static uint32_t s_palette[THEME_COLOR_MAX];

void theme_engine_init(void) {
    /* Modern, dark, glassmorphism-inspired default palette (slate/indigo vibes) */
    s_palette[THEME_COLOR_BACKGROUND]      = 0xFF0B1120; /* Very dark slate for canvas */
    s_palette[THEME_COLOR_WINDOW_BG]       = 0x881E293B; /* Slate-800 with 50% opacity for true glassmorphism */
    s_palette[THEME_COLOR_WINDOW_TITLEBAR] = 0xEE0F172A; /* Slate-900 with 93% opacity */
    s_palette[THEME_COLOR_WINDOW_BORDER]   = 0xFF475569; /* Slate-600 */
    s_palette[THEME_COLOR_TEXT_PRIMARY]    = 0xFFF8FAFC; /* Slate-50 */
    s_palette[THEME_COLOR_TEXT_SECONDARY]  = 0xFF94A3B8; /* Slate-400 */
    s_palette[THEME_COLOR_BUTTON_BG]       = 0xFF334155; /* Slate-700 */
    s_palette[THEME_COLOR_BUTTON_BG_HOVER] = 0xFF475569; /* Slate-600 */
    s_palette[THEME_COLOR_BUTTON_BG_PRESS] = 0xFF1E293B; /* Slate-800 */
    s_palette[THEME_COLOR_BUTTON_BORDER]   = 0xFF64748B; /* Slate-500 */
    s_palette[THEME_COLOR_BUTTON_TEXT]     = 0xFFFFFFFF; /* White */
    s_palette[THEME_COLOR_CLOSE_BTN]       = 0xFFEF4444; /* Red-500 */
    s_palette[THEME_COLOR_INPUT_BG]        = 0xFF0E1623;
    s_palette[THEME_COLOR_INPUT_BG_FOCUS]  = 0xFF1A2A3A;
    s_palette[THEME_COLOR_INPUT_BORDER]    = 0xFF355070;
    s_palette[THEME_COLOR_INPUT_TEXT]      = 0xFFF8FAFC;
    s_palette[THEME_COLOR_INPUT_CURSOR]    = 0xFF9CCBFF;
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
