#include "io.h"
#include <stdbool.h>
#include <stdint.h>

volatile char last_key = 0;
static bool ctrl_pressed = false;
static bool alt_pressed = false;
volatile bool ctrl_c_pressed = false;
volatile bool alt_f4_pressed = false;
volatile bool win_pressed = false;
static bool extended = false;

unsigned char kbd_us[128] = {
    0,   27,  '1',  '2',  '3',  '4', '5', '6',  '7', '8', '9', '0',
    '-', '=', '\b', '\t', 'q',  'w', 'e', 'r',  't', 'y', 'u', 'i',
    'o', 'p', '[',  ']',  '\n', 0,   'a', 's',  'd', 'f', 'g', 'h',
    'j', 'k', 'l',  ';',  '\'', '`', 0,   '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm',  ',',  '.',  '/', 0,   '*',  0,   ' '};

void keyboard_handler() {
  uint8_t scancode = inb(0x60);

  if (scancode == 0xE0) {
    extended = true;
    return;
  }

  if (extended) {
    if (scancode == 0x5B) { // Left Windows Press
      win_pressed = true;
    } else if (scancode == 0xDB) { // Left Windows Release
      win_pressed = false;
    }
    extended = false;
    return;
  }

  // Control key (0x1D press, 0x9D release)
  if (scancode == 0x1D)
    ctrl_pressed = true;
  else if (scancode == 0x9D)
    ctrl_pressed = false;

  // Alt key (0x38 press, 0xB8 release)
  if (scancode == 0x38)
    alt_pressed = true;
  else if (scancode == 0xB8)
    alt_pressed = false;

  // F4 key (0x3E) - Check for Alt+F4
  if (alt_pressed && scancode == 0x3E) {
    alt_f4_pressed = true;
    return;
  }

  if (!(scancode & 0x80)) {
    if (scancode < 128) {
      char key = kbd_us[scancode];
      if (ctrl_pressed && (key == 'c' || key == 'C')) {
        ctrl_c_pressed = true;
      } else {
        last_key = key;
      }
    }
  }
}

char get_last_key() {
  char k = last_key;
  last_key = 0;
  return k;
}

bool check_ctrl_c() {
  bool state = ctrl_c_pressed;
  ctrl_c_pressed = false;
  return state;
}

bool check_alt_f4() {
  bool state = alt_f4_pressed;
  alt_f4_pressed = false;
  return state;
}

bool check_win_key() {
  bool state = win_pressed;
  win_pressed = false;
  return state;
}
