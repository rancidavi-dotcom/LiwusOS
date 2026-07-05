#include "doomkeys.h"
#include "doomgeneric.h"

#include <libliw.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int s_KeyQueue[16];
static unsigned int s_KeyQueueWriteIndex = 0;
static unsigned int s_KeyQueueReadIndex = 0;

static uint32_t *fb_addr = 0;
static int fb_width = 0;
static int fb_height = 0;
static int fb_pitch = 0;

static int present_frame(const uint32_t *pixels, uint32_t width, uint32_t height)
{
    int ret;
    __asm__ volatile ("int $0x80"
                      : "=a"(ret)
                      : "a"(13), "b"(pixels), "c"(width), "d"(height),
                        "S"(-1), "D"(-1)
                      : "memory");
    return ret;
}

static int scancode_to_doomkey(int scancode, int extended)
{
    if (extended) {
        switch (scancode & 0x7F) {
            case 0x48: return KEY_UPARROW;
            case 0x4B: return KEY_LEFTARROW;
            case 0x4D: return KEY_RIGHTARROW;
            case 0x50: return KEY_DOWNARROW;
            case 0x1C: return KEYP_ENTER;
            case 0x35: return '/';
            default: return 0;
        }
    }
    switch (scancode) {
        case 0x01: return KEY_ESCAPE;
        case 0x0E: return KEY_BACKSPACE;
        case 0x0F: return KEY_TAB;
        case 0x1C: return KEY_ENTER;
        case 0x39: return ' ';
        case 0x02: return '1'; case 0x03: return '2';
        case 0x04: return '3'; case 0x05: return '4';
        case 0x06: return '5'; case 0x07: return '6';
        case 0x08: return '7'; case 0x09: return '8';
        case 0x0A: return '9'; case 0x0B: return '0';
        case 0x10: return 'q'; case 0x11: return 'w';
        case 0x12: return 'e'; case 0x13: return 'r';
        case 0x14: return 't'; case 0x15: return 'y';
        case 0x16: return 'u'; case 0x17: return 'i';
        case 0x18: return 'o'; case 0x19: return 'p';
        case 0x1E: return 'a'; case 0x1F: return 's';
        case 0x20: return 'd'; case 0x21: return 'f';
        case 0x22: return 'g'; case 0x23: return 'h';
        case 0x24: return 'j'; case 0x25: return 'k';
        case 0x26: return 'l';
        case 0x2C: return 'z'; case 0x2D: return 'x';
        case 0x2E: return 'c'; case 0x2F: return 'v';
        case 0x30: return 'b'; case 0x31: return 'n';
        case 0x32: return 'm';
        case 0x33: return ','; case 0x34: return '.';
        case 0x35: return '/';
        case 0x0C: return '-'; case 0x0D: return '=';
        case 0x1A: return '['; case 0x1B: return ']';
        case 0x27: return '\''; case 0x28: return '`';
        case 0x29: return ';';
        case 0x2B: return '\\';
        case 0x56: return '\\';
        case 0x3B: return KEY_F1;  case 0x3C: return KEY_F2;
        case 0x3D: return KEY_F3;  case 0x3E: return KEY_F4;
        case 0x3F: return KEY_F5;  case 0x40: return KEY_F6;
        case 0x41: return KEY_F7;  case 0x42: return KEY_F8;
        case 0x43: return KEY_F9;  case 0x44: return KEY_F10;
        case 0x57: return KEY_F11; case 0x58: return KEY_F12;
        default: return 0;
    }
}

static void push_key(int key)
{
    unsigned int next = (s_KeyQueueWriteIndex + 1) & 15;
    if (next != (s_KeyQueueReadIndex & 15)) {
        s_KeyQueue[s_KeyQueueWriteIndex & 15] = key;
        s_KeyQueueWriteIndex++;
    }
}

void DG_Init()
{
}

void DG_DrawFrame()
{
    if (!DG_ScreenBuffer) return;
    present_frame(DG_ScreenBuffer, 320, 200);
}

void DG_SleepMs(uint32_t ms)
{
    uint32_t start = DG_GetTicksMs();
    while (DG_GetTicksMs() - start < ms) {
    }
}

uint32_t DG_GetTicksMs()
{
    return liw_get_ticks() * 10;
}

int DG_GetKey(int* pressed, unsigned char* key)
{
    if (s_KeyQueueReadIndex != s_KeyQueueWriteIndex) {
        int k = s_KeyQueue[s_KeyQueueReadIndex & 15];
        s_KeyQueueReadIndex++;
        *pressed = 1;
        *key = (unsigned char)k;
        return 1;
    }
    return 0;
}

void DG_SetWindowTitle(const char* title)
{
    (void)title;
}

static void pump_events()
{
    for (int sc = 1; sc < 0x59; sc++) {
        if (liw_key_down(sc)) {
            int k = scancode_to_doomkey(sc, 0);
            if (k) push_key(k);
        }
    }
}

int main(int argc, char **argv)
{
    liw_fb_info_t fb = {0};
    liw_get_fb_info(&fb);
    if (!fb.address || fb.width == 0 || fb.height == 0) {
        printf("doomgeneric: framebuffer grafico indisponivel\n");
        return 1;
    }
    fb_addr = fb.address;
    fb_width = fb.width;
    fb_height = fb.height;
    fb_pitch = fb.pitch;

    doomgeneric_Create(argc, argv);
    while (1)
    {
        pump_events();
        doomgeneric_Tick();
    }
    return 0;
}
