#ifndef LIWUS_USB_HID_H
#define LIWUS_USB_HID_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint8_t configuration_value;
  uint8_t interface_number;
  uint8_t endpoint_address;
  uint8_t interval;
  uint16_t max_packet_size;
  uint8_t protocol; /* 1 = boot keyboard, 2 = boot mouse */
} usb_hid_boot_interface_t;

/* Finds the first boot keyboard/mouse interrupt-IN endpoint in a complete
 * USB configuration descriptor. Returns 0 on success. */
int usb_hid_find_boot_interface(const uint8_t *descriptor, size_t length,
                                usb_hid_boot_interface_t *result);

/* Consume standard 8-byte HID boot-keyboard and 3-byte boot-mouse reports. */
int usb_hid_keyboard_report(const uint8_t *report, size_t length);
int usb_hid_mouse_report(const uint8_t *report, size_t length);
void usb_hid_reset(void);

#endif
