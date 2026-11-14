#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/syscall.h"

#define FB_W 320
#define FB_H 200
#define COLS (FB_W / 6)
#define ROWS (FB_H / 8)

static uint32_t fb[FB_W * FB_H];
static int head_y[COLS];
static int speed[COLS];
static int trail_len[COLS];
static uint8_t grid_char[ROWS][COLS];

static uint32_t rng = 0x9E3779B9U;
static uint32_t xorshift(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static void draw_glyph(int cx, int cy, uint8_t pat, uint32_t col) {
    int bx = cx * 6;
    int by = cy * 8;
    for (int r = 0; r < 7; r++) {
        uint8_t row_bits = (uint8_t)((pat * (uint8_t)(r * 19 + 7)) ^ (pat >> (r & 3)));
        for (int c = 0; c < 4; c++) {
            if (row_bits & (1U << c)) {
                int px = bx + c + 1;
                int py = by + r;
                if (px >= 0 && px < FB_W && py >= 0 && py < FB_H) {
                    fb[py * FB_W + px] = col;
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    int max_frames = 0;
    if (argc >= 2 && argv[1]) {
        max_frames = (int)strtol(argv[1], NULL, 10);
    }

    rng = (uint32_t)(liw_timer_ticks() ^ 0xC0FFEE11U);
    if (rng == 0) rng = 42;

    for (int c = 0; c < COLS; c++) {
        head_y[c] = -(int)(xorshift() % ROWS);
        speed[c] = 1 + (int)(xorshift() % 2);
        trail_len[c] = 6 + (int)(xorshift() % 12);
        for (int r = 0; r < ROWS; r++) {
            grid_char[r][c] = (uint8_t)(1 + (xorshift() % 250));
        }
    }

    int frame = 0;
    {
        liw_key_event_t flush_ev;
        while (liw_key_event(&flush_ev) == 1) {}
    }
    int running = 1;
    while (running) {
        liw_key_event_t ev;
        while (liw_key_event(&ev) == 1) {
            if (ev.pressed && (ev.scancode == 0x01 || ev.scancode == 0x10 || ev.scancode == 0x39)) {
                running = 0;
                break;
            }
        }
        if (!running) break;

        memset(fb, 0, sizeof(fb));

        for (int c = 0; c < COLS; c++) {
            if ((frame % speed[c]) == 0) {
                head_y[c]++;
                if (head_y[c] >= 0 && head_y[c] < ROWS) {
                    grid_char[head_y[c]][c] = (uint8_t)(1 + (xorshift() % 250));
                }
                if (head_y[c] - trail_len[c] > ROWS) {
                    head_y[c] = -(int)(xorshift() % 8);
                    trail_len[c] = 6 + (int)(xorshift() % 14);
                }
            }

            for (int r = 0; r < ROWS; r++) {
                int dist = head_y[c] - r;
                if (dist == 0) {
                    draw_glyph(c, r, grid_char[r][c], 0xD8FFE8);
                } else if (dist > 0 && dist <= trail_len[c]) {
                    uint32_t g = (uint32_t)(230 - (dist * 180) / trail_len[c]);
                    uint32_t col = (g << 8) | 0x08U;
                    draw_glyph(c, r, grid_char[r][c], col);
                }
            }
        }

        liw_fb_blit(fb, FB_W, FB_H);
        liw_sleep_ms(40);

        frame++;
        if (max_frames > 0 && frame >= max_frames) break;
    }

    printf("Matrix Digital Rain encerrado (%d quadros).\n", frame);
    return 0;
}
