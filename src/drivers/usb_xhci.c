#include "usb_xhci.h"

#include "kernel_layout.h"
#include "pci.h"
#include "serial.h"
#include "vmm.h"

#define PCI_BAR0_OFFSET 0x10
#define PCI_COMMAND_OFFSET 0x04
#define PCI_COMMAND_MEMORY 0x0002

static int map_xhci_register_page(uint64_t physical_page) {
  uint64_t virtual_page = (uint64_t)phys_to_virt(physical_page);
  return vmm_map_page_in(kernel_directory, (void *)physical_page,
                         (void *)virtual_page,
                         PTE_P | PTE_W | PTE_PCD | PTE_NX);
}

static uint64_t xhci_bar_base(const pci_device_t *device) {
  uint32_t low = pci_read_config(device->bus, device->device,
                                 device->function, PCI_BAR0_OFFSET);
  if (low == 0 || low == 0xFFFFFFFF || (low & 1)) return 0;

  uint32_t bar_type = (low >> 1) & 0x3;
  uint64_t base = low & ~0xFULL;
  if (bar_type == 0x2) {
    uint32_t high = pci_read_config(device->bus, device->device,
                                    device->function, PCI_BAR0_OFFSET + 4);
    base |= (uint64_t)high << 32;
  } else if (bar_type != 0) {
    return 0;
  }
  return base;
}

void usb_xhci_probe(void) {
  pci_device_t *device = pci_get_usb(0x30);
  if (!device) {
    serial_print("USB: no PCI xHCI controller found; USB support inactive\n");
    return;
  }

  uint64_t bar = xhci_bar_base(device);
  if (!bar || bar >= DIRECT_MAP_SIZE) {
    serial_print("USB: xHCI has no usable memory BAR0\n");
    return;
  }

  /* Enable PCI memory decoding only. Bus mastering stays disabled until a
   * DMA ring implementation exists and can safely own the controller. */
  uint32_t command_status = pci_read_config(device->bus, device->device,
                                            device->function,
                                            PCI_COMMAND_OFFSET);
  uint16_t command = (uint16_t)command_status | PCI_COMMAND_MEMORY;
  pci_write_config(device->bus, device->device, device->function,
                   PCI_COMMAND_OFFSET,
                   (uint32_t)command);

  uint64_t page = bar & ~((uint64_t)PAGE_SIZE - 1);
  if (map_xhci_register_page(page) < 0) {
    serial_print("USB: could not map xHCI capability registers\n");
    return;
  }

  volatile uint8_t *registers = (volatile uint8_t *)phys_to_virt(bar);
  uint8_t cap_length = registers[0];
  uint16_t version = *(volatile uint16_t *)(registers + 2);
  uint32_t structural = *(volatile uint32_t *)(registers + 4);
  uint8_t ports = (uint8_t)(structural >> 24);

  if (cap_length < 0x20 || cap_length > 0x80 || version == 0 ||
      version == 0xFFFF || ports == 0) {
    serial_print("USB: xHCI capability registers returned invalid values\n");
    return;
  }

  serial_print("USB: xHCI capability probe OK, PCI ");
  serial_print_hex(((uint64_t)device->bus << 16) |
                   ((uint64_t)device->device << 8) | device->function);
  serial_print(" BAR0=");
  serial_print_hex(bar);
  serial_print(" version=");
  serial_print_hex(version);
  serial_print(" ports=");
  serial_print_hex(ports);
  serial_print(" (controller not started; USB devices unavailable)\n");
}
