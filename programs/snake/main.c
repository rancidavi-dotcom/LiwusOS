#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/syscall.h"

#define FB_W 320
#define FB_H 200
#define CELL 8
#define GRID_X 1
#define GRID_Y 3
#define GRID_W 38
#define GRID_H 20
#define MAX_SNAKE (GRID_W * GRID_H)

static uint32_t fb[FB_W * FB_H];

static void fill_rect(int x, int y, int w, int h, uint32_t col) {
    for (int j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= FB_H) continue;
        uint32_t *row = &fb[py * FB_W];
        for (int i = 0; i < w; i++) {
            int px = x + i;
            if (px >= 0 && px < FB_W) row[px] = col;
        }
    }
}

/* Mini fonte 5x7 para caracteres maiusculos, digitos e simbolos basicos */
static uint8_t glyph5x7(char c, int row) {
    static const uint8_t digits[10][7] = {
        {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, /* 0 */
        {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, /* 1 */
        {0x0E,0x11,0x01,0x06,0x08,0x10,0x1F}, /* 2 */
        {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, /* 3 */
        {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, /* 4 */
        {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, /* 5 */
        {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, /* 6 */
        {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, /* 7 */
        {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, /* 8 */
        {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, /* 9 */
    };
    static const uint8_t alpha[26][7] = {
        {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, /* A */
        {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, /* B */
        {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, /* C */
        {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, /* D */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, /* E */
        {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, /* F */
        {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, /* G */
        {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, /* H */
        {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, /* I */
        {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, /* J */
        {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, /* K */
        {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, /* L */
        {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, /* M */
        {0x11,0x19,0x15,0x13,0x11,0x11,0x11}, /* N */
        {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, /* O */
        {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, /* P */
        {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, /* Q */
        {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, /* R */
        {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, /* S */
        {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, /* T */
        {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, /* U */
        {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, /* V */
        {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, /* W */
        {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, /* X */
        {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, /* Y */
        {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, /* Z */
    };
    if (c >= '0' && c <= '9') return digits[c - '0'][row];
    if (c >= 'A' && c <= 'Z') return alpha[c - 'A'][row];
    if (c >= 'a' && c <= 'z') return alpha[c - 'a'][row];
    if (c == ':') return (row == 2 || row == 4) ? 0x04 : 0x00;
    if (c == '-') return (row == 3) ? 0x0E : 0x00;
    if (c == '/') return (row >= 1 && row <= 5) ? (1U << (row - 1)) : 0x00;
    return 0x00;
}

static void draw_text(int x, int y, const char *s, uint32_t col) {
    while (*s) {
        char c = *s++;
        for (int r = 0; r < 7; r++) {
            uint8_t bits = glyph5x7(c, r);
            for (int col_i = 0; col_i < 5; col_i++) {
                if (bits & (1U << (4 - col_i))) {
                    int px = x + col_i;
                    int py = y + r;
                    if (px >= 0 && px < FB_W && py >= 0 && py < FB_H) {
                        fb[py * FB_W + px] = col;
                    }
                }
            }
        }
        x += 6;
    }
}

static uint32_t rng_state = 0x13579BDFU;
static uint32_t next_rand(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    rng_state = (uint32_t)(liw_timer_ticks() ^ 0xA5A55A5AU);
    if (rng_state == 0) rng_state = 1;

    int sx[MAX_SNAKE], sy[MAX_SNAKE];
    int len = 4;
    int dir_x = 1, dir_y = 0;
    int food_x = 15, food_y = 10;
    int score = 0, best = 0;
    int game_over = 0;

    for (int i = 0; i < len; i++) {
        sx[i] = 10 - i;
        sy[i] = 10;
    }

    /* Limpa eventos de teclado residuais (ex: tecla Enter ao iniciar pelo Konsole) */
    {
        liw_key_event_t flush_ev;
        while (liw_key_event(&flush_ev) == 1) {}
    }

    int running = 1;
    while (running) {
        int next_dx = dir_x, next_dy = dir_y;
        liw_key_event_t ev;
        while (liw_key_event(&ev) == 1) {
            if (!ev.pressed) continue;
            uint8_t sc = ev.scancode;
            if (sc == 0x01 || sc == 0x10) { /* ESC ou Q */
                running = 0;
                break;
            }
            if (sc == 0x13 && game_over) { /* R reinicia */
                len = 4;
                dir_x = 1; dir_y = 0;
                next_dx = 1; next_dy = 0;
                for (int i = 0; i < len; i++) {
                    sx[i] = 10 - i;
                    sy[i] = 10;
                }
                score = 0;
                game_over = 0;
            }
            if ((sc == 0x11 || sc == 0x48) && dir_y == 0) { next_dx = 0; next_dy = -1; }
            else if ((sc == 0x1F || sc == 0x50) && dir_y == 0) { next_dx = 0; next_dy = 1; }
            else if ((sc == 0x1E || sc == 0x4B) && dir_x == 0) { next_dx = -1; next_dy = 0; }
            else if ((sc == 0x20 || sc == 0x4D) && dir_x == 0) { next_dx = 1; next_dy = 0; }
        }
        if (!running) break;

        dir_x = next_dx;
        dir_y = next_dy;

        if (!game_over) {
            int nx = sx[0] + dir_x;
            int ny = sy[0] + dir_y;

            if (nx < 0 || nx >= GRID_W || ny < 0 || ny >= GRID_H) {
                game_over = 1;
            } else {
                for (int i = 0; i < len; i++) {
                    if (sx[i] == nx && sy[i] == ny) {
                        game_over = 1;
                        break;
                    }
                }
            }

            if (!game_over) {
                for (int i = len; i > 0; i--) {
                    sx[i] = sx[i - 1];
                    sy[i] = sy[i - 1];
                }
                sx[0] = nx;
                sy[0] = ny;

                if (nx == food_x && ny == food_y) {
                    if (len < MAX_SNAKE - 1) len++;
                    score += 10;
                    if (score > best) best = score;
                    for (int tries = 0; tries < 64; tries++) {
                        int fx = (int)(next_rand() % GRID_W);
                        int fy = (int)(next_rand() % GRID_H);
                        int hit = 0;
                        for (int i = 0; i < len; i++) {
                            if (sx[i] == fx && sy[i] == fy) { hit = 1; break; }
                        }
                        food_x = fx;
                        food_y = fy;
                        if (!hit) break;
                    }
                }
            }
        }

        /* Renderiza quadro Breeze Dark */
        fill_rect(0, 0, FB_W, FB_H, 0x1B1E20);
        fill_rect(0, 0, FB_W, 18, 0x232629);
        fill_rect(0, 18, FB_W, 1, 0x3DAEE9);

        char hud[64];
        snprintf(hud, sizeof(hud), "LIWUS SNAKE   SCORE: %d   BEST: %d", score, best);
        draw_text(8, 6, hud, 0xEFF0F1);
        draw_text(254, 6, "ESC: SAIR", 0x939A9E);

        /* Arena */
        int ax = GRID_X * CELL;
        int ay = GRID_Y * CELL;
        int aw = GRID_W * CELL;
        int ah = GRID_H * CELL;
        fill_rect(ax - 1, ay - 1, aw + 2, ah + 2, 0x3B4045);
        fill_rect(ax, ay, aw, ah, 0x141719);

        /* Comida */
        fill_rect(ax + food_x * CELL + 1, ay + food_y * CELL + 1, CELL - 2, CELL - 2, 0xDA4453);

        /* Cobra */
        for (int i = 0; i < len; i++) {
            uint32_t c = (i == 0) ? 0x3DAEE9 : 0x27AE60;
            fill_rect(ax + sx[i] * CELL + 1, ay + sy[i] * CELL + 1, CELL - 2, CELL - 2, c);
        }

        if (game_over) {
            fill_rect(70, 78, 180, 44, 0x232629);
            fill_rect(70, 78, 180, 2, 0xDA4453);
            draw_text(106, 88, "FIM DE JOGO!", 0xDA4453);
            draw_text(82, 104, "R: JOGAR   ESC: SAIR", 0xEFF0F1);
        }

        fill_rect(0, FB_H - 12, FB_W, 12, 0x232629);
        draw_text(8, FB_H - 9, "CONTROLES: WASD OU SETAS PARA MOVER", 0x939A9E);

        liw_fb_blit(fb, FB_W, FB_H);
        liw_sleep_ms(75);
    }

    printf("Liwus Snake encerrado. Pontuacao maxima: %d\n", best);
    return 0;
}
