#ifndef LIWUS_USB_HID_H
#define LIWUS_USB_HID_H

#include <stddef.h>
#include <stdint.h>

/* Consume standard 8-byte HID boot-keyboard and 3-byte boot-mouse reports. */
int usb_hid_keyboard_report(const uint8_t *report, size_t length);
int usb_hid_mouse_report(const uint8_t *report, size_t length);
void usb_hid_reset(void);

#endif
