/*
 * gui/apps/gui_terminal.h
 *
 * Self-contained GUI terminal emulator with scrollback buffer.
 * Uses vga_output_hook to capture command output from terminal/commands.c.
 */
#ifndef GUI_TERMINAL_H
#define GUI_TERMINAL_H

#include "../scene/node.h"
#include <stdbool.h>
#include <stdint.h>

/* Terminal dimensions in characters */
#define GTERM_COLS  80
#define GTERM_ROWS  24

/* Character cell (char + color) */
typedef struct {
    char    ch;
    uint32_t fg;
} gterm_cell_t;

/* The opaque terminal state */
typedef struct gui_terminal gui_terminal_t;

/* Create the terminal node and attach it to parent scene graph */
node_t *gui_terminal_create(const char *win_name, int x, int y, int w, int h);

#endif /* GUI_TERMINAL_H */
