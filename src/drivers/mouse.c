#include "mouse.h"
#include "io.h"

extern uint32_t screen_width;
extern uint32_t screen_height;

static int32_t mouse_x = 640;
static int32_t mouse_y = 360;
static uint8_t mouse_cycle = 0;
static uint8_t mouse_byte[3];
static bool left_clicked = false;
static bool hardware_left_clicked = false;
static bool fake_left_clicked = false;
static bool right_clicked = false;

void mouse_set_fake_click(bool clicked) {
    fake_left_clicked = clicked;
    left_clicked = hardware_left_clicked || fake_left_clicked;
}

void mouse_wait(uint8_t type) {
    uint32_t timeout = 100000;
    if (type == 0) { while (timeout--) { if ((inb(0x64) & 1) == 1) return; } }
    else { while (timeout--) { if ((inb(0x64) & 2) == 0) return; } }
}

void mouse_write(uint8_t write) {
    mouse_wait(1); outb(0x64, 0xD4);
    mouse_wait(1); outb(0x60, write);
}

uint8_t mouse_read() {
    mouse_wait(0); return inb(0x60);
}

void init_mouse() {
    mouse_wait(1); outb(0x64, 0xA8); // Enable auxiliary device
    
    mouse_write(0xF6); mouse_read(); // Set default settings
    
    mouse_wait(1); outb(0x64, 0x20);
    mouse_wait(0); uint8_t status = (inb(0x60) | 2);
    mouse_wait(1); outb(0x64, 0x60);
    mouse_wait(1); outb(0x60, status);
    
    /* Configurações extras de sensibilidade */
    mouse_write(0xE8); mouse_read(); mouse_write(0x03); mouse_read(); /* Resolução máxima */
    mouse_write(0xF3); mouse_read(); mouse_write(200); mouse_read();  /* Sample rate 200Hz */
    
    mouse_write(0xF4); mouse_read();
    mouse_x = 640; mouse_y = 360;
}

void mouse_handle_event(int x_rel, int y_rel, int buttons) {
    hardware_left_clicked = (buttons & 0x01);
    left_clicked = hardware_left_clicked || fake_left_clicked;
    right_clicked = (buttons & 0x02);
    
    mouse_x += x_rel;
    mouse_y -= y_rel;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= 1024) mouse_x = 1023;
    if (mouse_y >= 768) mouse_y = 767;
}

void mouse_handler() {
    uint8_t status = inb(0x64);
    if (!(status & 1)) return;

    uint8_t data = inb(0x60);
    
    if (mouse_cycle == 0 && !(data & 0x08)) return;
    
    mouse_byte[mouse_cycle++] = data;

    if (mouse_cycle == 3) {
        mouse_cycle = 0;
        
        if (mouse_byte[0] & 0x80 || mouse_byte[0] & 0x40) return;

        hardware_left_clicked = (mouse_byte[0] & 0x01);
        left_clicked = hardware_left_clicked || fake_left_clicked;
        right_clicked = (mouse_byte[0] & 0x02);

        int32_t x_rel = (int32_t)mouse_byte[1];
        int32_t y_rel = (int32_t)mouse_byte[2];

        if (mouse_byte[0] & 0x10) x_rel -= 256;
        if (mouse_byte[0] & 0x20) y_rel -= 256;

        mouse_x += (x_rel * 3);
        mouse_y -= (y_rel * 3);

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= 1024) mouse_x = 1023;
        if (mouse_y >= 768) mouse_y = 767;
    }
}

int32_t get_mouse_x() { return mouse_x; }
int32_t get_mouse_y() { return mouse_y; }
bool is_left_clicked() { return left_clicked; }bool is_right_clicked() { return right_clicked; }
