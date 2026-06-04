#include "usb.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "string.h"

// Tabela simples de USB HID para ASCII (sem shift)
static const char usb_hid_map[] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\n', 27, '\b', '\t', ' ',
    '-', '=', '[', ']', '\\', 0, ';', '\'', '`', ',', '.', '/'
};

static uint8_t last_report[8] = {0};

extern void push_char(char c);

void usb_hid_handle_report(usb_device_t *dev, uint8_t *data, int len) {
    if (dev->type == 1) { // Keyboard
        if (len < 8) return;
        
        // Verifica se houve mudança em relação ao último report (evita repetição infinita)
        if (memcmp(data, last_report, 8) == 0) return;
        memcpy(last_report, data, 8);

        // Byte 2-7 são keycodes pressionados
        for (int i = 2; i < 8; i++) {
            uint8_t code = data[i];
            if (code >= 4 && code < sizeof(usb_hid_map)) {
                char c = usb_hid_map[code];
                if (c) {
                    serial_print("USB KBD: "); 
                    char s[2] = {c, 0}; serial_print(s);
                    serial_print("\n");
                    push_char(c);
                }
            }
        }
    } else if (dev->type == 2) { // Mouse
        if (len < 3) return;
        // int8_t buttons = data[0];
        int8_t x = (int8_t)data[1];
        int8_t y = (int8_t)data[2];
        
        // No LiwusOS o mouse PS/2 usa mouse_handle_event(x, y, buttons)
        extern void mouse_handle_event(int x, int y, int buttons);
        mouse_handle_event(x, y, data[0]);
    }
}
