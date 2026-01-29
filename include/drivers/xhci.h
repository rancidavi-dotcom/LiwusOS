#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>
#include "pci.h"
#include "usb_spec.h"

/* Um TRB (Transfer Request Block) tem sempre 16 bytes. */
typedef struct {
    uint64_t param;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) xhci_trb_t;

/* Entrada da Event Ring Segment Table. */
typedef struct {
    uint64_t base;
    uint32_t size;
    uint32_t reserved;
} __attribute__((packed)) xhci_erst_entry_t;

void xhci_init(pci_device_t *dev);
void usb_poll_xhci(void);

#endif
