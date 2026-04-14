#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

void keyboard_handler(void);
char get_last_key(void);
int keyboard_pop_char(char *out);
bool check_ctrl_c(void);
bool check_alt_f4(void);
bool check_win_key(void);
bool keyboard_is_pressed(uint8_t scancode);
int keyboard_pop_event(uint8_t *scancode, bool *pressed);

#endif
