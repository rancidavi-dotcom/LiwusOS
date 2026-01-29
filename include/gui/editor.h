#ifndef EDITOR_H
#define EDITOR_H

#include "gui.h"

widget_t *init_editor();
void editor_click_handler(int rx, int ry);
void update_editor_key(char k);
wl_surface_t *get_editor_surface(void);
void open_editor();

#endif
