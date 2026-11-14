#include "usb_hid.h"

#include "keyboard.h"
#include "mouse.h"
#include <stdbool.h>

typedef struct {
  uint8_t scancode;
  bool extended;
} key_map_t;

static const key_map_t hid_keymap[0x88] = {
    [0x04] = {0x1E, false}, [0x05] = {0x30, false},
    [0x06] = {0x2E, false}, [0x07] = {0x20, false},
    [0x08] = {0x12, false}, [0x09] = {0x21, false},
    [0x0A] = {0x22, false}, [0x0B] = {0x23, false},
    [0x0C] = {0x17, false}, [0x0D] = {0x24, false},
    [0x0E] = {0x25, false}, [0x0F] = {0x26, false},
    [0x10] = {0x32, false}, [0x11] = {0x31, false},
    [0x12] = {0x18, false}, [0x13] = {0x19, false},
    [0x14] = {0x10, false}, [0x15] = {0x13, false},
    [0x16] = {0x1F, false}, [0x17] = {0x14, false},
    [0x18] = {0x16, false}, [0x19] = {0x2F, false},
    [0x1A] = {0x11, false}, [0x1B] = {0x2D, false},
    [0x1C] = {0x15, false}, [0x1D] = {0x2C, false},
    [0x1E] = {0x02, false}, [0x1F] = {0x03, false},
    [0x20] = {0x04, false}, [0x21] = {0x05, false},
    [0x22] = {0x06, false}, [0x23] = {0x07, false},
    [0x24] = {0x08, false}, [0x25] = {0x09, false},
    [0x26] = {0x0A, false}, [0x27] = {0x0B, false},
    [0x28] = {0x1C, false}, [0x29] = {0x01, false},
    [0x2A] = {0x0E, false}, [0x2B] = {0x0F, false},
    [0x2C] = {0x39, false}, [0x2D] = {0x0C, false},
    [0x2E] = {0x0D, false}, [0x2F] = {0x1A, false},
    [0x30] = {0x1B, false}, [0x31] = {0x2B, false},
    [0x33] = {0x27, false}, [0x34] = {0x28, false},
    [0x35] = {0x29, false}, [0x36] = {0x33, false},
    [0x37] = {0x34, false}, [0x38] = {0x35, false},
    [0x39] = {0x3A, false}, [0x3A] = {0x3B, false},
    [0x3B] = {0x3C, false}, [0x3C] = {0x3D, false},
    [0x3D] = {0x3E, false}, [0x3E] = {0x3F, false},
    [0x3F] = {0x40, false}, [0x40] = {0x41, false},
    [0x41] = {0x42, false}, [0x42] = {0x43, false},
    [0x43] = {0x44, false}, [0x44] = {0x57, false},
    [0x45] = {0x58, false}, [0x49] = {0x52, true},
    [0x4A] = {0x47, true}, [0x4B] = {0x49, true},
    [0x4C] = {0x53, true}, [0x4D] = {0x4F, true},
    [0x4E] = {0x51, true}, [0x4F] = {0x4D, true},
    [0x50] = {0x4B, true}, [0x51] = {0x50, true},
    [0x52] = {0x48, true},
    [0x54] = {0x35, true},
    [0x55] = {0x37, false},
    [0x56] = {0x4A, false},
    [0x57] = {0x4E, false},
    [0x58] = {0x1C, true},
    [0x59] = {0x4F, false},
    [0x5A] = {0x50, false},
    [0x5B] = {0x51, false},
    [0x5C] = {0x4B, false},
    [0x5D] = {0x4C, false},
    [0x5E] = {0x4D, false},
    [0x5F] = {0x47, false},
    [0x60] = {0x48, false},
    [0x61] = {0x49, false},
    [0x62] = {0x52, false},
    [0x63] = {0x53, false},
    [0x64] = {0x56, false},
    [0x85] = {0x7E, false},
    [0x87] = {0x73, false},
};

static uint8_t previous_modifiers;
static uint8_t previous_keys[6];

#ifdef USB_HID_HOST_TEST
static uint64_t usb_hid_irq_save(void) { return 0; }
static void usb_hid_irq_restore(uint64_t flags) { (void)flags; }
#else
static uint64_t usb_hid_irq_save(void) {
  uint64_t flags;
  __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
  return flags;
}

static void usb_hid_irq_restore(uint64_t flags) {
  if (flags & (1ULL << 9)) __asm__ volatile("sti" ::: "memory");
}
#endif

static uint16_t read_le16(const uint8_t *bytes) {
  return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

static void save_boot_interface(usb_hid_boot_interface_t *result,
                                uint8_t configuration, uint8_t interface,
                                uint8_t endpoint, uint8_t interval,
                                uint16_t packet_size, uint8_t protocol) {
  *result = (usb_hid_boot_interface_t){
      .configuration_value = configuration,
      .interface_number = interface,
      .endpoint_address = endpoint,
      .interval = interval,
      .max_packet_size = packet_size,
      .protocol = protocol};
}

int usb_hid_find_boot_interface(const uint8_t *descriptor, size_t length,
                                usb_hid_boot_interface_t *result) {
  if (!descriptor || !result || length < 9 || descriptor[0] < 9 ||
      descriptor[1] != 2)
    return -1;

  uint16_t total_length = read_le16(descriptor + 2);
  if (total_length < descriptor[0] || total_length > length ||
      descriptor[5] == 0)
    return -1;

  bool candidate = false;
  uint8_t interface_number = 0;
  uint8_t protocol = 0;
  uint8_t endpoint_address = 0;
  uint8_t interval = 0;
  uint16_t max_packet_size = 0;

  for (size_t offset = descriptor[0]; offset < total_length;) {
    if (total_length - offset < 2) return -1;
    uint8_t descriptor_length = descriptor[offset];
    uint8_t descriptor_type = descriptor[offset + 1];
    if (descriptor_length < 2 || descriptor_length > total_length - offset)
      return -1;

    const uint8_t *current = descriptor + offset;
    if (descriptor_type == 4) {
      if (descriptor_length < 9) return -1;
      if (candidate && endpoint_address) {
        save_boot_interface(result, descriptor[5], interface_number,
                            endpoint_address, interval, max_packet_size,
                            protocol);
        return 0;
      }
      candidate = current[3] == 0 && current[5] == 3 &&
                  current[6] == 1 &&
                  (current[7] == 1 || current[7] == 2);
      interface_number = current[2];
      protocol = current[7];
      endpoint_address = 0;
      max_packet_size = 0;
      interval = 0;
    } else if (descriptor_type == 5 && candidate) {
      if (descriptor_length < 7) return -1;
      uint8_t address = current[2];
      uint8_t attributes = current[3] & 0x03;
      uint16_t packet_size = read_le16(current + 4) & 0x07ff;
      uint16_t minimum = protocol == 1 ? 8 : 3;
      if ((address & 0x80) && (address & 0x0f) != 0 && attributes == 3 &&
          packet_size >= minimum) {
        endpoint_address = address;
        max_packet_size = packet_size;
        interval = current[6];
      }
    }
    offset += descriptor_length;
  }

  if (!candidate || !endpoint_address) return -1;
  save_boot_interface(result, descriptor[5], interface_number,
                      endpoint_address, interval, max_packet_size, protocol);
  return 0;
}

static void send_modifier(uint8_t bit, uint8_t scancode, bool extended,
                          uint8_t modifiers) {
  bool was_pressed = (previous_modifiers & bit) != 0;
  bool is_pressed = (modifiers & bit) != 0;
  if (was_pressed != is_pressed)
    keyboard_inject_set1(scancode, extended, is_pressed);
}

static bool report_has_key(const uint8_t keys[6], uint8_t usage) {
  for (size_t i = 0; i < 6; ++i)
    if (keys[i] == usage) return true;
  return false;
}

static bool is_rollover(const uint8_t keys[6]) {
  for (size_t i = 0; i < 6; ++i)
    if (keys[i] >= 1 && keys[i] <= 3) return true;
  return false;
}

static void send_usage(uint8_t usage, bool pressed) {
  if (usage < sizeof(hid_keymap) / sizeof(hid_keymap[0]) &&
      hid_keymap[usage].scancode)
    keyboard_inject_set1(hid_keymap[usage].scancode,
                         hid_keymap[usage].extended, pressed);
}

int usb_hid_keyboard_report(const uint8_t *report, size_t length) {
  if (!report || length < 8) return -1;
  uint64_t irq_flags = usb_hid_irq_save();
  const uint8_t *keys = report + 2;
  /* HID uses usages 1-3 to report rollover/error. Preserve the last known
   * state until the device sends a valid key array again. */
  if (is_rollover(keys)) {
    usb_hid_irq_restore(irq_flags);
    return 0;
  }

  static const uint8_t modifier_scancodes[8] = {
      0x1D, 0x2A, 0x38, 0x5B, 0x1D, 0x36, 0x38, 0x5C};
  static const bool modifier_extended[8] = {
      false, false, false, true, true, false, true, true};
  for (uint8_t bit = 0; bit < 8; ++bit)
    send_modifier((uint8_t)(1u << bit), modifier_scancodes[bit],
                  modifier_extended[bit], report[0]);

  for (size_t i = 0; i < 6; ++i)
    if (previous_keys[i] && !report_has_key(keys, previous_keys[i]))
      send_usage(previous_keys[i], false);
  for (size_t i = 0; i < 6; ++i)
    if (keys[i] && !report_has_key(previous_keys, keys[i]))
      send_usage(keys[i], true);

  previous_modifiers = report[0];
  for (size_t i = 0; i < 6; ++i) previous_keys[i] = keys[i];
  usb_hid_irq_restore(irq_flags);
  return 0;
}

int usb_hid_mouse_report(const uint8_t *report, size_t length) {
  if (!report || length < 3) return -1;
  mouse_handle_event((int8_t)report[1], (int8_t)report[2], report[0]);
  return 0;
}

void usb_hid_reset(void) {
  uint64_t irq_flags = usb_hid_irq_save();
  static const uint8_t modifier_scancodes[8] = {
      0x1D, 0x2A, 0x38, 0x5B, 0x1D, 0x36, 0x38, 0x5C};
  static const bool modifier_extended[8] = {
      false, false, false, true, true, false, true, true};
  for (uint8_t bit = 0; bit < 8; ++bit)
    send_modifier((uint8_t)(1u << bit), modifier_scancodes[bit],
                  modifier_extended[bit], 0);
  for (size_t i = 0; i < 6; ++i)
    if (previous_keys[i]) send_usage(previous_keys[i], false);
  previous_modifiers = 0;
  for (size_t i = 0; i < 6; ++i) previous_keys[i] = 0;
  usb_hid_irq_restore(irq_flags);
}
