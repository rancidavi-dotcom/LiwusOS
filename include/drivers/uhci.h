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

#define UHCI_MAX_INT_SLOTS 2

typedef struct uhci_td {
    uint32_t link;
    uint32_t status;
    uint32_t token;
    uint32_t buffer;
    // Padding para alinhar a 16 ou 32 bytes se necessário, 
    // mas o hardware UHCI processa os 4 dwords acima.
    uint32_t reserved[4]; 
} __attribute__((packed, aligned(16))) uhci_td_t;

typedef struct uhci_qh {
    uint32_t head;
    uint32_t element;
} __attribute__((packed, aligned(16))) uhci_qh_t;

typedef struct {
    uint32_t frame_list[1024];
    uint32_t io_base;
} uhci_controller_t;

void uhci_init(pci_device_t *dev);
int uhci_send_control(pci_device_t *dev, uint8_t addr, void *setup, void *data, uint16_t len);
int uhci_register_interrupt(pci_device_t *dev, uint8_t addr, uint8_t endpoint, void *buffer, uint16_t len);

// Multi-slot interrupt polling
int uhci_get_int_slot_count(void);
uhci_td_t *uhci_get_int_td(int slot);
void *uhci_get_int_buffer(int slot);
int uhci_get_int_len(int slot);
uint8_t uhci_get_int_dev_type(int slot);
void uhci_rearm_int(int slot);
void uhci_set_int_dev_type(int slot, uint8_t type);

// Legacy (slot 0) - kept for backward compat
uhci_td_t *uhci_get_interrupt_td(void);
void *uhci_get_interrupt_buffer(void);
int uhci_get_interrupt_len(void);
void uhci_rearm_interrupt(void);

#endif
