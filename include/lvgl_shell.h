#ifndef LVGL_SHELL_H
#define LVGL_SHELL_H

#include <stdbool.h>
#include <stdint.h>

void lvgl_shell_init(void);
void lvgl_shell_set_pointer(int x, int y, bool pressed);
void lvgl_shell_handle_key(char key);
void lvgl_shell_tick(uint32_t now_ticks);
void lvgl_shell_show_terminal(void);
int lvgl_shell_enabled(void);

#endif
