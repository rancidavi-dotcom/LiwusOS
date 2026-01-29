/*
 * gui/apps/gui_browser.h
 *
 * Simple in-kernel web browser: fetch a URL via HTTP and render the text
 * (HTML tags stripped) in a scrollable character grid window.
 */
#ifndef GUI_BROWSER_H
#define GUI_BROWSER_H

#include "../scene/node.h"

/* Browser viewport dimensions in characters */
#define BROWSER_COLS   96
#define BROWSER_LINES  512
#define BROWSER_ADDR   160
#define BROWSER_RESP   (64 * 1024)
#define BROWSER_MAX_LINKS 160
#define BROWSER_HISTORY  8

node_t *gui_browser_create(const char *win_name, int x, int y, int w, int h);

#endif /* GUI_BROWSER_H */