#include <liwgfx.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define PROBE_W 320
#define PROBE_H 200

static uint32_t frame[PROBE_W * PROBE_H];

static void draw_frame(uint32_t ticks, int player_x, int player_y) {
  int x;
  int y;

  for (y = 0; y < PROBE_H; ++y) {
    for (x = 0; x < PROBE_W; ++x) {
      uint32_t r = (uint32_t)((x + ticks) & 0xFF);
      uint32_t g = (uint32_t)((y + (ticks >> 1)) & 0xFF);
      uint32_t b = (uint32_t)(((x ^ y) + ticks) & 0xFF);
      frame[y * PROBE_W + x] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
  }

  for (y = 0; y < 8; ++y) {
    for (x = 0; x < 8; ++x) {
      int px = player_x + x;
      int py = player_y + y;
      if (px >= 0 && px < PROBE_W && py >= 0 && py < PROBE_H) {
        frame[py * PROBE_W + px] = 0xFFFFFFFF;
      }
    }
  }
}

int main(void) {
  liw_fb_info_t fb;
  uint32_t last_print = 0;
  int player_x = PROBE_W / 2;
  int player_y = PROBE_H / 2;

  if (liw_get_fb_info(&fb) != 0) {
    printf("doomprobe: sem framebuffer\n");
    return 1;
  }

  printf("doomprobe: tela %ux%u\n", fb.width, fb.height);

  while (!liw_key_down(LIW_KEY_ESCAPE)) {
    uint32_t ticks = liw_get_ticks();

    if (liw_key_down(LIW_KEY_LEFT) || liw_key_down(LIW_KEY_A)) {
      player_x--;
    }
    if (liw_key_down(LIW_KEY_RIGHT) || liw_key_down(LIW_KEY_D)) {
      player_x++;
    }
    if (liw_key_down(LIW_KEY_UP) || liw_key_down(LIW_KEY_W)) {
      player_y--;
    }
    if (liw_key_down(LIW_KEY_DOWN) || liw_key_down(LIW_KEY_S)) {
      player_y++;
    }

    draw_frame(ticks, player_x, player_y);
    liw_present_fb(frame, PROBE_W, PROBE_H, -1, -1);

    if (ticks - last_print > 200) {
      printf("doomprobe: ticks=%u pos=(%d,%d)\n", ticks, player_x, player_y);
      last_print = ticks;
    }
  }

  printf("doomprobe: encerrando\n");
  return 0;
}
