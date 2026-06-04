#include "io.h"
#include <stdbool.h>
#include <stdint.h>

volatile char last_key = 0;
static bool shift_pressed = false;
static bool altgr_pressed = false;
static bool extended = false;

static char char_queue[256];
static uint8_t char_read = 0;
static uint8_t char_write = 0;

static bool key_state[256];

typedef struct {
  uint8_t scancode;
  int pressed;
} key_event_t;

static key_event_t event_queue[64];
static uint8_t event_read = 0;
static uint8_t event_write = 0;

// Mapeamento ABNT2 (PS/2 Set 1) com inicializadores explícitos para precisão total
static unsigned char kbd_abnt2[128] = {
    [0x01] = 27,   [0x02] = '1',  [0x03] = '2',  [0x04] = '3',  [0x05] = '4',
    [0x06] = '5',  [0x07] = '6',  [0x08] = '7',  [0x09] = '8',  [0x0A] = '9',
    [0x0B] = '0',  [0x0C] = '-',  [0x0D] = '=',  [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q',  [0x11] = 'w',  [0x12] = 'e',  [0x13] = 'r',  [0x14] = 't',
    [0x15] = 'y',  [0x16] = 'u',  [0x17] = 'i',  [0x18] = 'o',  [0x19] = 'p',
    [0x1A] = 0xB4, [0x1B] = '[',  [0x1C] = '\n', [0x1E] = 'a',  [0x1F] = 's',
    [0x20] = 'd',  [0x21] = 'f',  [0x22] = 'g',  [0x23] = 'h',  [0x24] = 'j',
    [0x25] = 'k',  [0x26] = 'l',  [0x27] = 0xE7, [0x28] = '~',  [0x29] = '\'',
    [0x2B] = ']',  [0x2C] = 'z',  [0x2D] = 'x',  [0x2E] = 'c',  [0x2F] = 'v',
    [0x30] = 'b',  [0x31] = 'n',  [0x32] = 'm',  [0x33] = ',',  [0x34] = '.',
    [0x35] = ';',  [0x39] = ' ',  [0x56] = '\\', [0x73] = '/'
};

static unsigned char kbd_abnt2_shift[128] = {
    [0x01] = 27,   [0x02] = '!',  [0x03] = '@',  [0x04] = '#',  [0x05] = '$',
    [0x06] = '%',  [0x07] = 0xA8, [0x08] = '&',  [0x09] = '*',  [0x0A] = '(',
    [0x0B] = ')',  [0x0C] = '_',  [0x0D] = '+',  [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'Q',  [0x11] = 'W',  [0x12] = 'E',  [0x13] = 'R',  [0x14] = 'T',
    [0x15] = 'Y',  [0x16] = 'U',  [0x17] = 'I',  [0x18] = 'O',  [0x19] = 'P',
    [0x1A] = '`',  [0x1B] = '{',  [0x1C] = '\n', [0x1E] = 'A',  [0x1F] = 'S',
    [0x20] = 'D',  [0x21] = 'F',  [0x22] = 'G',  [0x23] = 'H',  [0x24] = 'J',
    [0x25] = 'K',  [0x26] = 'L',  [0x27] = 0xC7, [0x28] = '^',  [0x29] = '\"',
    [0x2B] = '}',  [0x2C] = 'Z',  [0x2D] = 'X',  [0x2E] = 'C',  [0x2F] = 'V',
    [0x30] = 'B',  [0x31] = 'N',  [0x32] = 'M',  [0x33] = '<',  [0x34] = '>',
    [0x35] = ':',  [0x39] = ' ',  [0x56] = '|',  [0x73] = '?'
};

static void push_char(char ch) {
  uint8_t next = (uint8_t)(char_write + 1);
  if (next != char_read) {
    char_queue[char_write] = ch;
    char_write = next;
  }
}

static void push_event(uint8_t scancode, int pressed) {
  uint8_t next = (uint8_t)(event_write + 1);
  if (next != event_read) {
    event_queue[event_write].scancode = scancode;
    event_queue[event_write].pressed = pressed;
    event_write = next;
  }
}

void keyboard_handler() {
  uint8_t scancode = inb(0x60);

  if (scancode == 0xE0) {
    extended = true;
    return;
  }

  if (scancode & 0x80) { // Key Released
    uint8_t released = scancode & 0x7F;
    key_state[released] = false;
    if (released == 0x2A || released == 0x36) shift_pressed = false;
    if (released == 0x38) altgr_pressed = false;
    push_event(released, 0);
    extended = false;
  } else { // Key Pressed
    key_state[scancode] = true;
    if (extended) {
      push_event(scancode | 0x80, 1);
      extended = false;
      return;
    }
    if (scancode == 0x2A || scancode == 0x36) {
      shift_pressed = true;
    } else if (scancode == 0x38) {
      altgr_pressed = true;
    } else {
      char key = 0;

      // Casos especiais de scancodes ABNT2 fixos
      if (scancode == 0x73) key = shift_pressed ? '?' : '/';
      else if (scancode == 0x56) key = shift_pressed ? '|' : '\\';
      else if (scancode < 128) {
        key = shift_pressed ? kbd_abnt2_shift[scancode] : kbd_abnt2[scancode];
      }

      if (key) {
        last_key = key;
        push_char(key);
      }
    }
    push_event(scancode, 1);
    extended = false;
  }
}

int keyboard_pop_char(char *out) {
  if (char_read == char_write) return 0;
  if (out) *out = char_queue[char_read];
  char_read = (uint8_t)(char_read + 1);
  return 1;
}

int keyboard_get_event(void *ev) {
  if (event_read == event_write) return 0;
  key_event_t *e = (key_event_t *)ev;
  e->scancode = event_queue[event_read].scancode;
  e->pressed = event_queue[event_read].pressed;
  event_read = (uint8_t)(event_read + 1);
  return 1;
}

char get_last_key() { char k = last_key; last_key = 0; return k; }
bool check_ctrl_c() { return false; }
bool check_alt_f4() { return false; }
bool check_win_key() { return false; }
bool keyboard_is_pressed(uint8_t s) { return key_state[s]; }
