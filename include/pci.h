#ifndef PCI_H
#define PCI_H

#include <stdint.h>

typedef struct {
  uint16_t vendor_id;
  uint16_t device_id;
  uint8_t class_id;
  uint8_t subclass_id;
  uint8_t interrupt_line;
  uint16_t bus;
  uint16_t device;
  uint16_t function;
} pci_device_t;

void pci_init();
pci_device_t *pci_get_gpu();
pci_device_t *pci_get_net();
pci_device_t *pci_get_device(uint16_t vendor_id, uint16_t device_id);
pci_device_t *pci_get_wireless();
pci_device_t *pci_get_usb(uint8_t interface_type);
uint32_t pci_read_config(uint16_t bus, uint16_t device, uint16_t function,
                         uint16_t offset);
void pci_write_config(uint16_t bus, uint16_t device, uint16_t function,
                          uint16_t offset, uint32_t val);

#endif
