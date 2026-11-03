#include "usb_hid.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
  uint8_t scancode;
  bool extended;
  bool pressed;
} key_event_t;

static key_event_t events[64];
static size_t event_count;
static int mouse_x;
static int mouse_y;
static int mouse_buttons;

void keyboard_inject_set1(uint8_t scancode, bool extended, bool pressed) {
  assert(event_count < sizeof(events) / sizeof(events[0]));
  events[event_count++] = (key_event_t){scancode, extended, pressed};
}

void mouse_handle_event(int x_rel, int y_rel, int buttons) {
  mouse_x = x_rel;
  mouse_y = y_rel;
  mouse_buttons = buttons;
}

static void expect_key(size_t index, uint8_t code, bool extended, bool pressed) {
  assert(index < event_count);
  assert(events[index].scancode == code);
  assert(events[index].extended == extended);
  assert(events[index].pressed == pressed);
}

int main(void) {
  uint8_t configuration[] = {
      9, 2, 41, 0, 2, 1, 0, 0x80, 50,
      9, 4, 0, 0, 0, 0xFF, 0, 0, 0,
      9, 4, 4, 0, 2, 3, 1, 1, 0,
      7, 5, 0x01, 3, 8, 0, 10,
      7, 5, 0x81, 3, 8, 0, 10};
  usb_hid_boot_interface_t boot_interface;
  assert(usb_hid_find_boot_interface(configuration, sizeof(configuration),
                                     &boot_interface) == 0);
  assert(boot_interface.configuration_value == 1);
  assert(boot_interface.interface_number == 4);
  assert(boot_interface.endpoint_address == 0x81);
  assert(boot_interface.interval == 10 &&
         boot_interface.max_packet_size == 8 &&
         boot_interface.protocol == 1);
  assert(usb_hid_find_boot_interface(configuration,
                                     sizeof(configuration) - 1,
                                     &boot_interface) < 0);
  uint8_t malformed[] = {9, 2, 10, 0, 1, 1, 0, 0x80, 50, 0, 4};
  assert(usb_hid_find_boot_interface(malformed, sizeof(malformed),
                                     &boot_interface) < 0);

  const uint8_t key_a[] = {0, 0, 0x04, 0, 0, 0, 0, 0};
  const uint8_t key_a_repeat[] = {0, 0, 0x04, 0, 0, 0, 0, 0};
  const uint8_t shift_a[] = {0x02, 0, 0x04, 0, 0, 0, 0, 0};
  const uint8_t rollover[] = {0, 0, 1, 0, 0, 0, 0, 0};
  const uint8_t shift_only[] = {0x02, 0, 0, 0, 0, 0, 0, 0};
  const uint8_t released[] = {0, 0, 0, 0, 0, 0, 0, 0};
  const uint8_t right_arrow[] = {0, 0, 0x4F, 0, 0, 0, 0, 0};

  assert(usb_hid_keyboard_report(NULL, 8) < 0);
  assert(usb_hid_keyboard_report(key_a, 7) < 0);
  assert(usb_hid_keyboard_report(key_a, sizeof(key_a)) == 0);
  assert(event_count == 1);
  expect_key(0, 0x1E, false, true);

  assert(usb_hid_keyboard_report(key_a_repeat, sizeof(key_a_repeat)) == 0);
  assert(event_count == 1); /* No synthetic repeat while the key is held. */

  assert(usb_hid_keyboard_report(shift_a, sizeof(shift_a)) == 0);
  assert(event_count == 2);
  expect_key(1, 0x2A, false, true);
  assert(usb_hid_keyboard_report(rollover, sizeof(rollover)) == 0);
  assert(event_count == 2); /* Rollover does not release the last known keys. */

  assert(usb_hid_keyboard_report(shift_only, sizeof(shift_only)) == 0);
  assert(event_count == 3);
  expect_key(2, 0x1E, false, false);
  assert(usb_hid_keyboard_report(released, sizeof(released)) == 0);
  assert(event_count == 4);
  expect_key(3, 0x2A, false, false);

  assert(usb_hid_keyboard_report(right_arrow, sizeof(right_arrow)) == 0);
  assert(event_count == 5);
  expect_key(4, 0x4D, true, true);
  usb_hid_reset();
  assert(event_count == 6);
  expect_key(5, 0x4D, true, false);

  const uint8_t mouse[] = {5, 0xFE, 3, 0};
  assert(usb_hid_mouse_report(mouse, 2) < 0);
  assert(usb_hid_mouse_report(mouse, sizeof(mouse)) == 0);
  assert(mouse_x == -2 && mouse_y == 3 && mouse_buttons == 5);

  puts("USB HID PASS: descriptor bounds, key transitions, modifiers, rollover, mouse deltas");
  return 0;
}
