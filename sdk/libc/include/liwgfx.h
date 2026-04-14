#ifndef LIWLIB_LIWGFX_H
#define LIWLIB_LIWGFX_H

#include <stdint.h>

typedef struct {
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t format;
} liw_fb_info_t;

int liw_get_fb_info(liw_fb_info_t *out);
int liw_present_fb(const uint32_t *pixels, uint32_t width, uint32_t height,
                   int x, int y);
uint32_t liw_get_ticks(void);
int liw_key_down(uint8_t scancode);
int liw_get_key_event(uint8_t *scancode, int *pressed);

#define LIW_KEY_ESCAPE 0x01
#define LIW_KEY_ENTER 0x1C
#define LIW_KEY_CTRL 0x1D
#define LIW_KEY_SPACE 0x39
#define LIW_KEY_UP 0x48
#define LIW_KEY_LEFT 0x4B
#define LIW_KEY_RIGHT 0x4D
#define LIW_KEY_DOWN 0x50
#define LIW_KEY_W 0x11
#define LIW_KEY_A 0x1E
#define LIW_KEY_S 0x1F
#define LIW_KEY_D 0x20

#endif
