#include "usb.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "string.h"

static const uint8_t hid_to_ps2[256] = {
    0,0,0,0,
    0x1E,0x30,0x2E,0x20, 0x12,0x21,0x22,0x23,
    0x17,0x24,0x25,0x26, 0x32,0x31,0x18,0x19,
    0x10,0x13,0x1F,0x14, 0x16,0x2F,0x11,0x2D,
    0x15,0x2C,
    0x02,0x03,0x04,0x05, 0x06,0x07,0x08,0x09,
    0x0A,0x0B,
    0x1C,0x01,0x0E,0x0F, 0x39,
    0x0C,0x0D,0x1A,0x1B, 0x2B,0x2B,0x27,0x28,
    0x29,0x33,0x34,0x35,
    0x3A,
    0x3B,0x3C,0x3D,0x3E, 0x3F,0x40,0x41,0x42,
    0x43,0x44,0x57,0x58,
    0,0x46,0,
    0xD2,0xC7,0xC9,0xD3, 0xCF,0xD1,0xCD,0xCB,
    0xD0,0xC8,
    0x45,
};

static const char hid_to_ascii[] = {
    0,0,0,0, 'a','b','c','d','e','f','g','h','i','j','k','l',
    'm','n','o','p','q','r','s','t','u','v','w','x','y','z',
    '1','2','3','4','5','6','7','8','9','0',
    '\n',0,'\b','\t',' ',
    '-','=','[',']','\\',0,';','\'','`',',','.','/'
};

static const char hid_to_ascii_shift[] = {
    0,0,0,0, 'A','B','C','D','E','F','G','H','I','J','K','L',
    'M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
    '!','@','#','$','%','^','&','*','(',')',
    '\n',0,'\b','\t',' ',
    '_','+','{','}','|',0,':','"','~','<','>','?'
};

void usb_hid_handle_report(usb_device_t *dev, uint8_t *data, int len) {
    (void)dev;
    if (len < 8) return;

    uint8_t mod = data[0];
    static uint8_t prev_mod = 0;
    static uint8_t prev_keys[6] = {0};

    if ((mod & 0x01) && !(prev_mod & 0x01)) push_event(0x1D, 1);
    if (!(mod & 0x01) && (prev_mod & 0x01)) push_event(0x1D, 0);
    if ((mod & 0x02) && !(prev_mod & 0x02)) push_event(0x2A, 1);
    if (!(mod & 0x02) && (prev_mod & 0x02)) push_event(0x2A, 0);
    if ((mod & 0x04) && !(prev_mod & 0x04)) push_event(0x38, 1);
    if (!(mod & 0x04) && (prev_mod & 0x04)) push_event(0x38, 0);
    if ((mod & 0x10) && !(prev_mod & 0x10)) push_event(0x1D, 1);
    if (!(mod & 0x10) && (prev_mod & 0x10)) push_event(0x1D, 0);
    if ((mod & 0x20) && !(prev_mod & 0x20)) push_event(0x36, 1);
    if (!(mod & 0x20) && (prev_mod & 0x20)) push_event(0x36, 0);
    if ((mod & 0x40) && !(prev_mod & 0x40)) push_event(0x38, 1);
    if (!(mod & 0x40) && (prev_mod & 0x40)) push_event(0x38, 0);

    prev_mod = mod;

    int shift = (mod & 0x02) || (mod & 0x20);
    int ctrl = (mod & 0x01) || (mod & 0x10);

    for (int i = 2; i < 8; i++) {
        uint8_t code = data[i];
        if (code == 0) continue;

        int was_pressed = 0;
        for (int j = 0; j < 6; j++) {
            if (prev_keys[j] == code) { was_pressed = 1; break; }
        }
        if (was_pressed) continue;

        if (ctrl && code == 0x06) {
            push_char(3);
            keyboard_set_ctrl_c();
        } else {
            if (hid_to_ps2[code])
                push_event(hid_to_ps2[code], 1);
            if (code >= 4 && code < sizeof(hid_to_ascii) + 4) {
                char c = shift ? hid_to_ascii_shift[code] : hid_to_ascii[code];
                if (c) push_char(c);
            }
        }
    }

    for (int i = 0; i < 6; i++) {
        uint8_t code = prev_keys[i];
        if (code == 0) continue;
        int still_pressed = 0;
        for (int j = 2; j < 8; j++) {
            if (data[j] == code) { still_pressed = 1; break; }
        }
        if (!still_pressed && hid_to_ps2[code])
            push_event(hid_to_ps2[code], 0);
    }

    memcpy(prev_keys, data + 2, 6);
}
