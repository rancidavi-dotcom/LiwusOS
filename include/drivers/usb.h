#ifndef USB_H
#define USB_H

#include <stdint.h>
#include "pci.h"

typedef struct usb_device {
    uint8_t address;
    uint8_t port;
    uint8_t type; // 1=KBD, 2=MOUSE
    uint8_t hcd;  // 0=unknown, 1=UHCI, 2=EHCI, 3=xHCI
    uint8_t slot; // xHCI slot id (0 quando não aplicável)
    uint8_t ep_in; // xHCI DCI do endpoint de interrupção IN
    void *controller;
    void *qh; // EHCI QH
    struct usb_device *next;
} usb_device_t;

void usb_init(void);
void usb_start_polling(void);
void usb_register_device(usb_device_t *dev);

#endif
