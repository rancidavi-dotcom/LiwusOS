#ifndef EHCI_H
#define EHCI_H

#include <stdint.h>
#include "pci.h"

// Registradores de Capacidade (Offsets a partir do BAR)
#define EHCI_CAPLENGTH    0x00
#define EHCI_HCIVERSION   0x02
#define EHCI_HCSPARAMS    0x04
#define EHCI_HCCPARAMS    0x08

// Registradores Operacionais (Offsets a partir de BAR + CapLength)
#define EHCI_USBCMD       0x00
#define EHCI_USBSTS       0x04
#define EHCI_USBINTR      0x08
#define EHCI_FRINDEX      0x0C
#define EHCI_CTRLDSSEG    0x10
#define EHCI_PERIODICBASE 0x14
#define EHCI_ASYNCLIST    0x18
#define EHCI_CONFIGFLAG   0x40
#define EHCI_PORTSC       0x44

// Estruturas de Dados EHCI (Precisam ser alinhadas a 32 bytes)
typedef struct ehci_qtd {
    uint32_t next_qtd;
    uint32_t alt_next_qtd;
    uint32_t token;
    uint32_t buffer[5];
} __attribute__((packed)) ehci_qtd_t;

typedef struct ehci_qh {
    uint32_t horizontal_link;
    uint32_t endpoint_char;
    uint32_t endpoint_caps;
    uint32_t current_qtd;
    ehci_qtd_t overlay;
} __attribute__((packed)) ehci_qh_t;

void ehci_init(pci_device_t *dev);

#endif
