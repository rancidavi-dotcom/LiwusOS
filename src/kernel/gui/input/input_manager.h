/*
 * gui/input/input_manager.h
 *
 * Input Manager — the ONLY module that reads hardware state.
 *
 * Architecture:
 *
 *   [mouse.c / keyboard.c]
 *         │
 *         ▼
 *   input_manager_poll()      ← called once per compositor frame
 *         │
 *         ▼
 *   event_bus_post() → EventBus → Subscribers (widgets, tools, camera)
 *
 * Rules:
 *   - No widget or tool may call get_mouse_x() or keyboard_is_pressed()
 *     directly.
 *   - All input state is normalised here into typed gui_event_t payloads.
 *   - Mouse coordinates are delivered in SCREEN space; tools that need
 *     WORLD space call camera_screen_to_world() themselves.
 */
#ifndef GUI_INPUT_MANAGER_H
#define GUI_INPUT_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "../core/event_bus.h"
#include "../scene/camera.h"

/* --------------------------------------------------------------------------
 * Modifier bit-masks (matches gui_key_payload_t.modifiers)
 * -------------------------------------------------------------------------- */

#define MOD_SHIFT  (1u << 0)
#define MOD_CTRL   (1u << 1)
#define MOD_ALT    (1u << 2)
#define MOD_SUPER  (1u << 3)

/* --------------------------------------------------------------------------
 * Input Manager state (opaque to callers)
 * -------------------------------------------------------------------------- */

typedef struct input_manager input_manager_t;

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

input_manager_t *input_manager_create(gui_event_bus_t *bus);
void             input_manager_destroy(input_manager_t *im);

/*
 * Call once per compositor frame.
 * Reads hardware state, diffs against previous frame,
 * and posts delta events (MOVE, BUTTON, KEY_DOWN/UP, CHAR) to the bus.
 */
void input_manager_poll(input_manager_t *im);

/* --------------------------------------------------------------------------
 * Immediate state queries (for tools that need to know "right now")
 * -------------------------------------------------------------------------- */

int  input_mouse_x(const input_manager_t *im);
int  input_mouse_y(const input_manager_t *im);
bool input_mouse_button(const input_manager_t *im, uint8_t button); /* 1=L 2=R 3=M */
bool input_key_held(const input_manager_t *im, uint8_t scancode);
uint8_t input_modifiers(const input_manager_t *im);

#endif /* GUI_INPUT_MANAGER_H */
