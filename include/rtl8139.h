#ifndef RTL8139_H
#define RTL8139_H

#include <stdint.h>
#include "pci.h"

void init_rtl8139(pci_device_t* dev);
void rtl8139_send_packet(void* data, uint32_t len);
uint8_t* rtl8139_get_mac();

#endif
