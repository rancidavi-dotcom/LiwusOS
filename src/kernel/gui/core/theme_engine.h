/*
 * gui/core/theme_engine.h
 *
 * Provides a centralized color palette and styling variables for widgets.
 */
#ifndef GUI_THEME_ENGINE_H
#define GUI_THEME_ENGINE_H

#include <stdint.h>

typedef enum {
    THEME_COLOR_BACKGROUND,         /* Main canvas background */
    THEME_COLOR_WINDOW_BG,          /* Window background */
    THEME_COLOR_WINDOW_TITLEBAR,    /* Window titlebar */
    THEME_COLOR_WINDOW_BORDER,      /* Window outer border */
    THEME_COLOR_TEXT_PRIMARY,       /* Main text */
    THEME_COLOR_TEXT_SECONDARY,     /* Subdued text */
    THEME_COLOR_BUTTON_BG,          /* Button background */
    THEME_COLOR_BUTTON_BG_HOVER,    /* Button hovered */
    THEME_COLOR_BUTTON_BG_PRESS,    /* Button pressed */
    THEME_COLOR_BUTTON_BORDER,      /* Button border */
    THEME_COLOR_BUTTON_TEXT,        /* Button text */
    THEME_COLOR_CLOSE_BTN,          /* Window close button */
    THEME_COLOR_INPUT_BG,           /* Input background */
    THEME_COLOR_INPUT_BG_FOCUS,     /* Input background focused */
    THEME_COLOR_INPUT_BORDER,       /* Input border */
    THEME_COLOR_INPUT_TEXT,         /* Input text */
    THEME_COLOR_INPUT_CURSOR,       /* Input cursor */
    THEME_COLOR_MAX                 /* Sentinel */
} theme_color_id_t;

/* Initialize the default theme */
void theme_engine_init(void);

/* Get a specific color from the current theme */
uint32_t theme_engine_get_color(theme_color_id_t id);

/* Set a specific color in the current theme */
void theme_engine_set_color(theme_color_id_t id, uint32_t color);

#endif
