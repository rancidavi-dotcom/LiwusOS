#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <liwus/syscall.h>

#include "doomkeys.h"
#include "doomgeneric.h"

static unsigned char scancode_to_doom_key(uint8_t sc) {
    switch (sc) {
        case 0x01: return KEY_ESCAPE;
        case 0x02: return '1';
        case 0x03: return '2';
        case 0x04: return '3';
        case 0x05: return '4';
        case 0x06: return '5';
        case 0x07: return '6';
        case 0x08: return '7';
        case 0x09: return '8';
        case 0x0A: return '9';
        case 0x0B: return '0';
        case 0x0C: return KEY_MINUS;
        case 0x0D: return KEY_EQUALS;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;

        case 0x10: return 'q';
        case 0x11: return 'w';
        case 0x12: return 'e';
        case 0x13: return 'r';
        case 0x14: return 't';
        case 0x15: return 'y';
        case 0x16: return 'u';
        case 0x17: return 'i';
        case 0x18: return 'o';
        case 0x19: return 'p';
        case 0x1A: return '[';
        case 0x1B: return ']';
        case 0x1C:
        case 0x9C: return KEY_ENTER;
        case 0x1D:
        case 0x9D: return KEY_FIRE;

        case 0x1E: return 'a';
        case 0x1F: return 's';
        case 0x20: return 'd';
        case 0x21: return 'f';
        case 0x22: return 'g';
        case 0x23: return 'h';
        case 0x24: return 'j';
        case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x27: return ';';
        case 0x28: return '\'';
        case 0x29: return '`';
        case 0x2A:
        case 0x36: return KEY_RSHIFT;
        case 0x2B: return '\\';

        case 0x2C: return 'z';
        case 0x2D: return 'x';
        case 0x2E: return 'c';
        case 0x2F: return 'v';
        case 0x30: return 'b';
        case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x33: return ',';
        case 0x34: return '.';
        case 0x35: return '/';
        case 0x38:
        case 0xB8: return KEY_RALT;
        case 0x39: return KEY_USE;

        case 0x3B: return KEY_F1;
        case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;
        case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;
        case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;
        case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;
        case 0x44: return KEY_F10;
        case 0x57: return KEY_F11;
        case 0x58: return KEY_F12;

        case 0x47:
        case 0xC7: return KEY_HOME;
        case 0x48:
        case 0xC8: return KEY_UPARROW;
        case 0x49:
        case 0xC9: return KEY_PGUP;
        case 0x4A: return KEYP_MINUS;
        case 0x4B:
        case 0xCB: return KEY_LEFTARROW;
        case 0x4D:
        case 0xCD: return KEY_RIGHTARROW;
        case 0x4F:
        case 0xCF: return KEY_END;
        case 0x50:
        case 0xD0: return KEY_DOWNARROW;
        case 0x51:
        case 0xD1: return KEY_PGDN;
        case 0x52:
        case 0xD2: return KEY_INS;
        case 0x53:
        case 0xD3: return KEY_DEL;

        default: return 0;
    }
}

void DG_Init(void) {
    liw_key_event_t kev;
    while (liw_key_event(&kev) > 0) {
        /* Drain launch key/click residue */
    }
}

void DG_DrawFrame(void) {
    if (DG_ScreenBuffer) {
        liw_fb_blit(DG_ScreenBuffer, DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    }
}

void DG_SleepMs(uint32_t ms) {
    liw_sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    return (uint32_t)(liw_timer_ticks() * 10ULL);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    liw_key_event_t kev;
    while (liw_key_event(&kev) > 0) {
        unsigned char k = scancode_to_doom_key(kev.scancode);
        if (k != 0) {
            *pressed = kev.pressed ? 1 : 0;
            *doomKey = k;
            return 1;
        }
    }
    return 0;
}

void DG_SetWindowTitle(const char *title) {
    (void)title;
}

static const char *find_default_iwad(void) {
    static const char *const candidates[] = {
        "/doom1.wad",
        "doom1.wad",
        "/sys/doom1.wad",
        "/initrd/doom1.wad",
        NULL
    };
    for (int i = 0; candidates[i]; i++) {
        FILE *f = fopen(candidates[i], "rb");
        if (f) {
            fclose(f);
            return candidates[i];
        }
    }
    return "/doom1.wad";
}

int main(int argc, char **argv) {
    int has_iwad = 0;
    for (int i = 1; i < argc; i++) {
        if (argv[i] && strcmp(argv[i], "-iwad") == 0) {
            has_iwad = 1;
            break;
        }
    }

    if (!has_iwad && argc < 30) {
        static char *patched_argv[32];
        int n = 0;
        if (argc <= 0 || !argv || !argv[0]) {
            patched_argv[n++] = "doom";
        } else {
            for (int i = 0; i < argc && n < 29; i++) {
                patched_argv[n++] = argv[i];
            }
        }
        patched_argv[n++] = "-iwad";
        patched_argv[n++] = (char *)find_default_iwad();
        patched_argv[n] = NULL;
        doomgeneric_Create(n, patched_argv);
    } else {
        doomgeneric_Create(argc, argv);
    }

    for (;;) {
        doomgeneric_Tick();
    }
    return 0;
}
