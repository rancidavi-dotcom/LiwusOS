#ifndef EXPLORER_H
#define EXPLORER_H

#include "gui.h"

widget_t* init_explorer();
void explorer_click_handler(int rx, int ry);
void update_explorer_key(char k);
wl_surface_t *get_explorer_surface(void);
void open_explorer();

#endif
