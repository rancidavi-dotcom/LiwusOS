#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include <liwus/syscall.h>
#include "snes.h"

#define SCREEN_W 512
#define SCREEN_H 480
#define MAX_ROM_BYTES (16 * 1024 * 1024)
#define PATH_CAP 160

enum { PAGE_LIBRARY, PAGE_PLAY };

static uint32_t screen[SCREEN_W * SCREEN_H];
static uint32_t game_pixels[SCREEN_W * SCREEN_H];
static Snes *snes;
static int page = PAGE_LIBRARY;
static bool paused;
static char rom_path[PATH_CAP] = "/roms/";
static char loaded_name[64];
static char message[64] = "DIGITE O CAMINHO DA ROM E PRESSIONE ENTER";

static const uint32_t C_BG = 0x000C101B;
static const uint32_t C_PANEL = 0x00151D2B;
static const uint32_t C_PANEL2 = 0x001C293B;
static const uint32_t C_LINE = 0x00344860;
static const uint32_t C_TEXT = 0x00E8EEF6;
static const uint32_t C_MUTED = 0x008C9AAF;
static const uint32_t C_CYAN = 0x0046D9C8;
static const uint32_t C_ORANGE = 0x00FFB454;
static const uint32_t C_RED = 0x00FF6B78;

static const uint8_t font[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30}, {14,17,16,16,16,17,14},
    {30,17,17,17,17,17,30}, {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17}, {14,4,4,4,4,4,14},
    {7,2,2,2,2,18,12}, {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17}, {14,17,17,17,17,17,14},
    {30,17,17,30,16,16,16}, {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4}, {17,17,17,17,17,17,14},
    {17,17,17,17,17,10,4}, {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14}, {14,17,1,6,8,16,31},
    {30,1,1,14,1,1,30}, {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {6,8,16,30,17,17,14}, {31,1,2,4,8,8,8}, {14,17,17,14,17,17,14},
    {14,17,17,15,1,2,12}
};

static void rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    for (int py = y; py < y + h; py++) {
        uint32_t *row = &screen[py * SCREEN_W + x];
        for (int px = 0; px < w; px++) row[px] = color;
    }
}

static void text(int x, int y, const char *s, uint32_t color, int scale) {
    while (*s) {
        char c = *s++;
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        int index = -1;
        if (c >= 'A' && c <= 'Z') index = c - 'A';
        else if (c >= '0' && c <= '9') index = 26 + c - '0';
        if (index >= 0) {
            for (int row = 0; row < 7; row++) {
                uint8_t bits = font[index][row];
                for (int col = 0; col < 5; col++) {
                    if (bits & (1u << (4 - col)))
                        rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        } else if (c == '.') rect(x, y + 5 * scale, scale, scale, color);
        else if (c == '/') rect(x + 3 * scale, y + 2 * scale, scale, 4 * scale, color);
        else if (c == '-') rect(x, y + 3 * scale, 4 * scale, scale, color);
        else if (c == '=') { rect(x, y + 2 * scale, 4 * scale, scale, color); rect(x, y + 5 * scale, 4 * scale, scale, color); }
        else if (c == ':') { rect(x, y + 2 * scale, scale, scale, color); rect(x, y + 5 * scale, scale, scale, color); }
        else if (c == '>') { rect(x, y + 2 * scale, scale, scale, color); rect(x + scale, y + 3 * scale, scale, scale, color); rect(x, y + 4 * scale, scale, scale, color); }
        else if (c == '_') rect(x, y + 6 * scale, 4 * scale, scale, color);
        x += 6 * scale;
    }
}

static void clear_screen(uint32_t color) {
    for (unsigned int i = 0; i < SCREEN_W * SCREEN_H; i++) screen[i] = color;
}

static void present(void) {
    (void)liw_fb_blit(screen, SCREEN_W, SCREEN_H);
}

static void draw_library(void) {
    clear_screen(C_BG);
    rect(0, 0, SCREEN_W, 5, C_CYAN);
    text(34, 29, "LIWUS  /  PLAY", C_MUTED, 1);
    text(420, 29, "ESC SAIR", C_MUTED, 1);
    text(34, 58, "SUPER NINTENDO", C_TEXT, 3);
    text(36, 91, "16-BIT EMULATOR", C_CYAN, 1);

    rect(32, 126, 448, 2, C_LINE);
    rect(32, 151, 448, 184, C_PANEL);
    rect(32, 151, 4, 184, C_CYAN);
    text(52, 171, "CARREGAR JOGO", C_ORANGE, 2);
    text(52, 207, "CAMINHO DA ROM", C_MUTED, 1);
    rect(50, 226, 410, 36, C_PANEL2);
    rect(50, 226, 3, 36, C_CYAN);
    text(62, 240, rom_path, C_TEXT, 1);
    rect(50, 278, 410, 1, C_LINE);
    text(52, 296, message, (message[0] == 'R' && message[1] == 'O') ? C_RED : C_MUTED, 1);

    rect(32, 354, 216, 98, C_PANEL);
    text(48, 370, "CONTROLES", C_CYAN, 1);
    text(48, 393, "SETAS  MOVIMENTO", C_TEXT, 1);
    text(48, 412, "Z=B  X=A  A=Y  S=X", C_MUTED, 1);
    text(48, 419, "D=L  C=R  SHIFT SELECT", C_MUTED, 1);
    text(48, 435, "ENTER START", C_MUTED, 1);
    rect(264, 354, 216, 98, C_PANEL);
    text(280, 370, "SISTEMA", C_CYAN, 1);
    text(280, 393, "LO ROM  HI ROM  CX4", C_TEXT, 1);
    text(280, 412, "AUDIO AINDA INDISPONIVEL", C_MUTED, 1);

    if (loaded_name[0]) {
        rect(32, 449, 448, 1, C_LINE);
        text(34, 461, "JOGO CARREGADO", C_MUTED, 1);
        text(161, 461, loaded_name, C_TEXT, 1);
        text(424, 461, "ENTER >", C_ORANGE, 1);
    } else {
        text(34, 461, "ROM PROPRIA  /  SFC, SMC", C_MUTED, 1);
        text(404, 461, "LIWUS", C_ORANGE, 1);
    }
}

static void draw_pause(void) {
    clear_screen(C_BG);
    rect(0, 0, SCREEN_W, 5, C_CYAN);
    rect(38, 38, 436, 404, C_PANEL);
    rect(38, 38, 4, 404, C_CYAN);
    text(66, 72, "LIWUS  /  PAUSA", C_MUTED, 1);
    text(66, 111, "JOGO PAUSADO", C_TEXT, 3);
    text(68, 162, loaded_name, C_CYAN, 2);
    rect(66, 205, 380, 1, C_LINE);
    text(68, 235, "ENTER  CONTINUAR", C_ORANGE, 2);
    text(68, 287, "ESC  VOLTAR A BIBLIOTECA", C_TEXT, 1);
    text(68, 319, "SETAS  MOVIMENTO", C_MUTED, 1);
    text(68, 343, "Z=B  X=A  A=Y  S=X", C_MUTED, 1);
    text(68, 367, "D=L  C=R  SHIFT SELECT", C_MUTED, 1);
    text(68, 383, "ENTER START", C_MUTED, 1);
    text(68, 399, "AUDIO AINDA INDISPONIVEL NO LIWUSOS", C_MUTED, 1);
}

static bool load_rom(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        strncpy(message, "ROM NAO ENCONTRADA - VERIFIQUE O CAMINHO", sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        return false;
    }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return false; }
    long length = ftell(file);
    if (length < 0x8000 || length > MAX_ROM_BYTES || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        strncpy(message, "ROM INVALIDA OU MAIOR QUE 16 MB", sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        return false;
    }
    uint8_t *data = malloc((size_t)length);
    if (!data) { fclose(file); return false; }
    size_t got = fread(data, 1, (size_t)length, file);
    fclose(file);
    if (got != (size_t)length) { free(data); return false; }

    Snes *next = snes_init();
    bool ok = next && snes_loadRom(next, data, (int)length);
    free(data);
    if (!ok) {
        if (next) snes_free(next);
        strncpy(message, "FORMATO DE ROM NAO SUPORTADO", sizeof(message) - 1);
        message[sizeof(message) - 1] = '\0';
        return false;
    }
    if (snes) snes_free(snes);
    snes = next;
    const char *base = path;
    for (const char *p = path; *p; p++) if (*p == '/') base = p + 1;
    size_t n = strlen(base);
    if (n >= sizeof(loaded_name)) n = sizeof(loaded_name) - 1;
    memcpy(loaded_name, base, n);
    loaded_name[n] = '\0';
    strncpy(message, "ROM CARREGADA - PRESSIONE ENTER PARA JOGAR", sizeof(message) - 1);
    message[sizeof(message) - 1] = '\0';
    return true;
}

static int button_for_scancode(uint8_t sc) {
    sc &= 0x7F;
    switch (sc) {
        case 0x2C: return 0; /* B: Z */
        case 0x1E: return 1; /* Y: A */
        case 0x36: return 2; /* Select: right shift */
        case 0x1C: return 3; /* Start: Enter */
        case 0xC8: case 0x48: return 4;
        case 0xD0: case 0x50: return 5;
        case 0xCB: case 0x4B: return 6;
        case 0xCD: case 0x4D: return 7;
        case 0x2D: return 8; /* A: X */
        case 0x1F: return 9; /* X: S */
        case 0x20: return 10; /* L: D */
        case 0x2E: return 11; /* R: C */
        default: return -1;
    }
}

static char path_char(uint8_t sc) {
    static const char row1[] = "1234567890-=";
    static const char row2[] = "qwertyuiop[]";
    static const char row3[] = "asdfghjkl;'`";
    static const char row4[] = "\\zxcvbnm,./";
    if (sc >= 0x02 && sc <= 0x0D) return row1[sc - 0x02];
    if (sc >= 0x10 && sc <= 0x1B) return row2[sc - 0x10];
    if (sc >= 0x1E && sc <= 0x29) return row3[sc - 0x1E];
    if (sc == 0x2B) return row4[0];
    if (sc >= 0x2C && sc <= 0x35) return row4[sc - 0x2B];
    if (sc == 0x39) return ' ';
    return 0;
}

static void handle_event(const liw_key_event_t *ev) {
    uint8_t sc = ev->scancode;
    if (page == PAGE_PLAY) {
        if (ev->pressed && sc == 0x01) { page = PAGE_LIBRARY; paused = false; return; }
        if (ev->pressed && sc == 0x19) { paused = !paused; return; }
        if (ev->pressed && sc == 0x1C && paused) { paused = false; return; }
        int button = button_for_scancode(sc);
        if (button >= 0) snes_setButtonState(snes, 1, button, ev->pressed != 0);
        return;
    }

    if (!ev->pressed) return;
    if (sc == 0x01) liw_exit(0);
    if (sc == 0x1C) {
        if (snes) { page = PAGE_PLAY; paused = false; }
        else (void)load_rom(rom_path);
        return;
    }
    if (sc == 0x0E) {
        size_t n = strlen(rom_path);
        if (n > 0) rom_path[n - 1] = '\0';
        return;
    }
    char c = path_char(sc);
    size_t n = strlen(rom_path);
    if (c && n + 1 < sizeof(rom_path)) { rom_path[n] = c; rom_path[n + 1] = '\0'; }
}

int main(int argc, char **argv) {
    (void)liw_mkdir("/roms", 0755);
    liw_key_event_t ev;
    while (liw_key_event(&ev) > 0) { }
    if (argc > 1 && argv[1]) {
        strncpy(rom_path, argv[1], sizeof(rom_path) - 1);
        rom_path[sizeof(rom_path) - 1] = '\0';
        (void)load_rom(rom_path);
    }

    for (;;) {
        while (liw_key_event(&ev) > 0) handle_event(&ev);
        if (page == PAGE_PLAY && snes) {
            if (!paused) {
                snes_runFrame(snes);
                snes_setPixels(snes, (uint8_t *)game_pixels);
                (void)liw_fb_blit(game_pixels, SCREEN_W, SCREEN_H);
            } else {
                draw_pause();
                present();
                liw_sleep_ms(8);
            }
        } else {
            draw_library();
            present();
            liw_sleep_ms(16);
        }
    }
}
