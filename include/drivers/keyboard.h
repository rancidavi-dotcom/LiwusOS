#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    KB_LAYOUT_ABNT2 = 0,
    KB_LAYOUT_US,
    KB_LAYOUT_US_INTL,
    KB_LAYOUT_COUNT
} keyboard_layout_t;

void keyboard_handler(void);
void keyboard_tick(void);
/* Prefer the normalized USB HID path and ignore legacy-controller bytes. */
void keyboard_set_usb_active(bool active);
/* Feed a normalized Set-1 key event from a non-PS/2 input driver. */
void keyboard_inject_set1(uint8_t scancode, bool is_extended, bool pressed);
char get_last_key(void);
int keyboard_pop_char(char *out);
bool check_ctrl_c(void);
bool check_alt_f4(void);
bool check_win_key(void);
bool keyboard_consume_win_key(void);
bool keyboard_is_pressed(uint8_t scancode);
int keyboard_get_event(void *ev);
void push_event(uint8_t scancode, int pressed);
void push_char(char c);
void keyboard_set_ctrl_c(void);
void keyboard_set_layout(keyboard_layout_t layout);
keyboard_layout_t keyboard_get_layout(void);
const char *keyboard_layout_name(keyboard_layout_t layout);
void keyboard_flush(void);

#endif
