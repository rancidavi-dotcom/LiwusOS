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

static uint8_t mouse_sensitivity = 3;
static bool mouse_acceleration = true;

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

uint8_t mouse_read(void) {
    mouse_wait(0); return inb(0x60);
}

void init_mouse(void) {
    mouse_wait(1); outb(0x64, 0xA8);

    mouse_write(0xF6); mouse_read();

    mouse_wait(1); outb(0x64, 0x20);
    mouse_wait(0); uint8_t status = (inb(0x60) | 2);
    mouse_wait(1); outb(0x64, 0x60);
    mouse_wait(1); outb(0x60, status);

    mouse_write(0xE8); mouse_read(); mouse_write(0x03); mouse_read();
    mouse_write(0xF3); mouse_read(); mouse_write(200); mouse_read();

    mouse_write(0xF4); mouse_read();
    mouse_x = 640; mouse_y = 360;
}

static int32_t apply_acceleration(int32_t delta) {
    if (!mouse_acceleration) return delta;
    int32_t abs_delta = delta >= 0 ? delta : -delta;
    if (abs_delta > 10) {
        return delta * 2;
    }
    return delta;
}

void mouse_handle_event(int x_rel, int y_rel, int buttons) {
    hardware_left_clicked = (buttons & 0x01);
    left_clicked = hardware_left_clicked || fake_left_clicked;
    right_clicked = (buttons & 0x02);

    int32_t dx = x_rel * mouse_sensitivity;
    int32_t dy = y_rel * mouse_sensitivity;
    dx = apply_acceleration(dx);
    dy = apply_acceleration(dy);

    mouse_x += dx;
    mouse_y -= dy;

    if (mouse_x < 0) mouse_x = 0;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_x >= 1024) mouse_x = 1023;
    if (mouse_y >= 768) mouse_y = 767;
}

void mouse_handler(void) {
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

        x_rel = x_rel * mouse_sensitivity;
        y_rel = y_rel * mouse_sensitivity;
        x_rel = apply_acceleration(x_rel);
        y_rel = apply_acceleration(y_rel);

        mouse_x += x_rel;
        mouse_y -= y_rel;

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= 1024) mouse_x = 1023;
        if (mouse_y >= 768) mouse_y = 767;
    }
}

int32_t get_mouse_x(void) { return mouse_x; }
int32_t get_mouse_y(void) { return mouse_y; }
bool is_left_clicked(void) { return left_clicked; }
bool is_right_clicked(void) { return right_clicked; }

void mouse_set_sensitivity(uint8_t sensitivity) {
    if (sensitivity == 0) sensitivity = 1;
    if (sensitivity > 10) sensitivity = 10;
    mouse_sensitivity = sensitivity;
}

uint8_t mouse_get_sensitivity(void) {
    return mouse_sensitivity;
}

void mouse_set_acceleration(bool enable) {
    mouse_acceleration = enable;
}

bool mouse_get_acceleration(void) {
    return mouse_acceleration;
}
