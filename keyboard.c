#include "io.h"
#include <stdint.h>
#include <stdbool.h>

volatile char last_key = 0;
static bool ctrl_pressed = false;
volatile bool ctrl_c_pressed = false;

unsigned char kbd_us[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0,
    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' '
};

void keyboard_handler() {
    uint8_t scancode = inb(0x60);
    
    // Verifica se e o scancode do Control (0x1D)
    if (scancode == 0x1D) ctrl_pressed = true;
    else if (scancode == 0x9D) ctrl_pressed = false;

    if (!(scancode & 0x80)) {
        if (scancode < 128) {
            char key = kbd_us[scancode];
            if (ctrl_pressed && (key == 'c' || key == 'C')) {
                ctrl_c_pressed = true;
            } else {
                last_key = key;
            }
        }
    }
}

char get_last_key() {
    char k = last_key;
    last_key = 0;
    return k;
}

bool check_ctrl_c() {
    bool state = ctrl_c_pressed;
    ctrl_c_pressed = false;
    return state;
}

