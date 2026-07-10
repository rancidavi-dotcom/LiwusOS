/*
 * gui/input/input_manager.c
 */
#include "input_manager.h"
#include "kheap.h"
#include "string.h"
#include "mouse.h"
#include "keyboard.h"

/* --------------------------------------------------------------------------
 * Scancode set — track which keys were down last frame
 * -------------------------------------------------------------------------- */

#define SCANCODE_MAX 128

struct input_manager {
    gui_event_bus_t *bus;

    /* Previous frame state */
    int     prev_mx, prev_my;
    bool    prev_btn[4];           /* index 1=Left 2=Right 3=Middle */
    bool    prev_keys[SCANCODE_MAX];

    /* Current frame state (filled by poll) */
    int     cur_mx, cur_my;
    bool    cur_btn[4];
    bool    cur_keys[SCANCODE_MAX];

    uint8_t modifiers;
};

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

input_manager_t *input_manager_create(gui_event_bus_t *bus) {
    input_manager_t *im = (input_manager_t *)kmalloc(sizeof(input_manager_t));
    if (!im) return NULL;
    memset(im, 0, sizeof(input_manager_t));
    im->bus = bus;
    return im;
}

void input_manager_destroy(input_manager_t *im) {
    if (im) kfree(im);
}

/* --------------------------------------------------------------------------
 * Modifier helper
 * -------------------------------------------------------------------------- */

static uint8_t compute_modifiers(input_manager_t *im) {
    uint8_t m = 0;
    /* Scancodes: LShift=0x2A RShift=0x36 LCtrl=0x1D LAlt=0x38 LWin=0x5B */
    if (im->cur_keys[0x2A] || im->cur_keys[0x36]) m |= MOD_SHIFT;
    if (im->cur_keys[0x1D])                         m |= MOD_CTRL;
    if (im->cur_keys[0x38])                         m |= MOD_ALT;
    if (im->cur_keys[0x5B])                         m |= MOD_SUPER;
    return m;
}

/* --------------------------------------------------------------------------
 * Poll
 * -------------------------------------------------------------------------- */

void input_manager_poll(input_manager_t *im) {
    if (!im) return;

    /* Save previous state */
    im->prev_mx = im->cur_mx;
    im->prev_my = im->cur_my;
    for (int i = 0; i < 4; i++) im->prev_btn[i] = im->cur_btn[i];
    for (int i = 0; i < SCANCODE_MAX; i++) im->prev_keys[i] = im->cur_keys[i];

    /* Read current hardware state */
    im->cur_mx   = get_mouse_x();
    im->cur_my   = get_mouse_y();
    /* LCtrl (0x1D) acts as an additional left-click trigger */
    bool lctrl_held = (bool)keyboard_is_pressed(0x1D);
    im->cur_btn[1] = is_left_clicked()  || lctrl_held;
    im->cur_btn[2] = is_right_clicked();
    im->cur_btn[3] = false;  /* middle — extend when driver supports */

    for (int sc = 1; sc < SCANCODE_MAX; sc++) {
        im->cur_keys[sc] = (bool)keyboard_is_pressed((uint8_t)sc);
    }
    im->modifiers = compute_modifiers(im);

    /* --- Post delta events ---------------------------------------------- */
    gui_event_bus_t *bus = im->bus;

    /* Mouse move */
    int dx = im->cur_mx - im->prev_mx;
    int dy = im->cur_my - im->prev_my;
    if (dx != 0 || dy != 0) {
        event_post_mouse_move(bus, im->cur_mx, im->cur_my, dx, dy);
    }

    /* Mouse buttons */
    for (int btn = 1; btn <= 3; btn++) {
        if (im->cur_btn[btn] && !im->prev_btn[btn]) {
            event_post_mouse_button(bus, im->cur_mx, im->cur_my, (uint8_t)btn, true);
        } else if (!im->cur_btn[btn] && im->prev_btn[btn]) {
            event_post_mouse_button(bus, im->cur_mx, im->cur_my, (uint8_t)btn, false);
        }
    }

    /* Keyboard */
    for (int sc = 1; sc < SCANCODE_MAX; sc++) {
        if (im->cur_keys[sc] && !im->prev_keys[sc]) {
            event_post_key(bus, (uint8_t)sc, true);
        } else if (!im->cur_keys[sc] && im->prev_keys[sc]) {
            event_post_key(bus, (uint8_t)sc, false);
        }
    }
}

/* --------------------------------------------------------------------------
 * Immediate queries
 * -------------------------------------------------------------------------- */

int  input_mouse_x(const input_manager_t *im)       { return im ? im->cur_mx : 0; }
int  input_mouse_y(const input_manager_t *im)       { return im ? im->cur_my : 0; }
bool input_mouse_button(const input_manager_t *im, uint8_t b) {
    return (im && b <= 3) ? im->cur_btn[b] : false;
}
bool input_key_held(const input_manager_t *im, uint8_t sc) {
    return (im && sc < SCANCODE_MAX) ? im->cur_keys[sc] : false;
}
uint8_t input_modifiers(const input_manager_t *im) {
    return im ? im->modifiers : 0;
}
