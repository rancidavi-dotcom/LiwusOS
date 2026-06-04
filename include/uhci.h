#ifndef UHCI_H
#define UHCI_H

#include <stdint.h>
#include "pci.h"

// Registradores UHCI
#define UHCI_USBCMD    0x00
#define UHCI_USBSTS    0x02
#define UHCI_USBINTR   0x04
#define UHCI_FRNUM     0x06
#define UHCI_FRBASEADD 0x08
#define UHCI_SOFMOD    0x0C
#define UHCI_PORTSC1   0x10
#define UHCI_PORTSC2   0x12

typedef struct {
    uint32_t frame_list[1024];
    uint32_t io_base;
} uhci_controller_t;

void uhci_init(pci_device_t *dev);

#endif
