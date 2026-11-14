#include "pci.h"
#include "io.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "vmm.h"
#include "acpi.h"
#include "kernel_layout.h"
#include "vga.h"

/* Keep the general device inventory large enough for bridges and functions
 * found on real machines. USB discovery has its own registry below so it is
 * not lost when unrelated devices fill this list. */
#define PCI_DEVICE_LIMIT 256
#define PCI_USB_DEVICE_LIMIT 32
static pci_device_t *devices[PCI_DEVICE_LIMIT];
static int pci_count = 0;
static pci_device_t *usb_devices[PCI_USB_DEVICE_LIMIT];
static int usb_device_count = 0;

/* Acesso PCI via ECAM (MMCONFIG) quando o firmware fornece MCFG. */
static uint64_t ecam_base = 0;
static uint8_t ecam_start = 0;
static uint8_t ecam_end = 0;
static int ecam_on = 0;

static uint64_t ecam_addr(uint16_t bus, uint16_t device, uint16_t function,
                          uint16_t offset) {
  return ecam_base + ((uint64_t)(bus - ecam_start) << 20) +
         ((uint64_t)device << 15) + ((uint64_t)function << 12) +
         (offset & 0xFFF);
}

static uint32_t ecam_read(uint16_t bus, uint16_t device, uint16_t function,
                          uint16_t offset) {
  uint64_t a = ecam_addr(bus, device, function, offset);
  uint64_t page = a & ~0xFFFULL;
  vmm_map_page_in(kernel_directory, (void *)page, phys_to_virt(page),
                  PTE_P | PTE_W | PTE_PCD | PTE_NX);
  return *(volatile uint32_t *)phys_to_virt(a);
}

static void ecam_write(uint16_t bus, uint16_t device, uint16_t function,
                       uint16_t offset, uint32_t val) {
  uint64_t a = ecam_addr(bus, device, function, offset);
  uint64_t page = a & ~0xFFFULL;
  vmm_map_page_in(kernel_directory, (void *)page, phys_to_virt(page),
                  PTE_P | PTE_W | PTE_PCD | PTE_NX);
  *(volatile uint32_t *)phys_to_virt(a) = val;
}

void pci_enable_ecam(uint64_t base, uint8_t start_bus, uint8_t end_bus) {
  if (!base || end_bus < start_bus) {
    ecam_on = 0;
    return;
  }
  ecam_base = base;
  ecam_start = start_bus;
  ecam_end = end_bus;
  ecam_on = 1;
}

int pci_ecam_enabled(void) { return ecam_on; }

uint32_t pci_read_config(uint16_t bus, uint16_t device, uint16_t function,
                         uint16_t offset) {
  if (ecam_on && bus >= ecam_start && bus <= ecam_end)
    return ecam_read(bus, device, function, offset);

  uint32_t address =
      (uint32_t)((((uint32_t)bus) << 16) | (((uint32_t)device) << 11) |
                 (((uint32_t)function) << 8) | (offset & 0xFC) |
                 ((uint32_t)0x80000000));
  outl(0xCF8, address);
  return inl(0xCFC);
}

void pci_write_config(uint16_t bus, uint16_t device, uint16_t function,
                          uint16_t offset, uint32_t val) {
  if (ecam_on && bus >= ecam_start && bus <= ecam_end) {
    ecam_write(bus, device, function, offset, val);
    return;
  }

  uint32_t address =
      (uint32_t)((((uint32_t)bus) << 16) | (((uint32_t)device) << 11) |
                 (((uint32_t)function) << 8) | (offset & 0xFC) |
                 ((uint32_t)0x80000000));
  outl(0xCF8, address);
  outl(0xCFC, val);
}

/* Habilita ECAM a partir da tabela MCFG, se presente. */
static void pci_probe_mcfg(void) {
  acpi_sdt_header_t *mcfg = acpi_find_table("MCFG");
  if (!mcfg) {
    serial_print("PCI: MCFG ausente, usando config CF8/CFC\n");
    return;
  }
  /* Header + 8 bytes reservados + 1+ entradas de 16 bytes. */
  if (mcfg->length < sizeof(acpi_sdt_header_t) + 8 + 16) {
    serial_print("PCI: MCFG sem entradas, usando CF8/CFC\n");
    return;
  }
  uint8_t *e = (uint8_t *)mcfg + sizeof(acpi_sdt_header_t) + 8;
  uint64_t base;
  uint16_t seg;
  uint8_t sbus, ebus;
  memcpy(&base, e + 0, 8);
  memcpy(&seg, e + 8, 2);
  memcpy(&sbus, e + 10, 1);
  memcpy(&ebus, e + 11, 1);

  pci_enable_ecam(base, sbus, ebus);
  serial_print("PCI: MCFG ECAM base=");
  serial_print_hex(base);
  serial_print(" bus ");
  serial_print_hex(sbus);
  serial_print("-");
  serial_print_hex(ebus);
  serial_print(ecam_on ? " (ECAM ativo)\n" : " (ECAM invalido)\n");
  (void)seg;
}

void pci_init() {
  pci_count = 0;
  usb_device_count = 0;
  pci_probe_mcfg();
  for (uint16_t bus = 0; bus < 256; bus++) {
    for (uint16_t dev = 0; dev < 32; dev++) {
      uint32_t val = pci_read_config(bus, dev, 0, 0);
      if ((val & 0xFFFF) == 0xFFFF) continue;

      // Check header type for multi-function support (byte at offset 0x0E)
      uint32_t htype_reg = pci_read_config(bus, dev, 0, 0x0C);
      uint8_t funcs = (htype_reg & 0x800000) ? 8 : 1;

      for (uint8_t fn = 0; fn < funcs; fn++) {
        val = pci_read_config(bus, dev, fn, 0);
        if ((val & 0xFFFF) == 0xFFFF) continue;

        pci_device_t *d = (pci_device_t *)kmalloc(sizeof(pci_device_t));
        d->bus = bus;
        d->device = dev;
        d->function = fn;
        d->vendor_id = val & 0xFFFF;
        d->device_id = (val >> 16) & 0xFFFF;
        uint32_t class_reg = pci_read_config(bus, dev, fn, 0x08);
        d->class_id = (class_reg >> 24) & 0xFF;
        d->subclass_id = (class_reg >> 16) & 0xFF;

        if (d->class_id == 0x0C && d->subclass_id == 0x03) {
            uint8_t pi = (class_reg >> 8) & 0xFF;
            serial_print("PCI: Found USB Controller (");
            if (pi == 0x00) serial_print("UHCI");
            else if (pi == 0x10) serial_print("OHCI");
            else if (pi == 0x20) serial_print("EHCI");
            else if (pi == 0x30) serial_print("xHCI");
            else serial_print("Unknown");
            serial_print(")\n");
            if (usb_device_count < PCI_USB_DEVICE_LIMIT)
              usb_devices[usb_device_count++] = d;
        }

        uint32_t int_reg = pci_read_config(bus, dev, fn, 0x3C);
        d->interrupt_line = int_reg & 0xFF;

        if (d->vendor_id == 0x1AF4) {
          serial_print("PCI: FOUND VIRTIO DEVICE!\n");
        }

        if (pci_count < PCI_DEVICE_LIMIT)
          devices[pci_count++] = d;
      }
    }
  }
}

pci_device_t *pci_get_gpu() {
  for (int i = 0; i < pci_count; i++)
    if (devices[i]->class_id == 0x03)
      return devices[i];
  return (void *)0;
}

pci_device_t *pci_get_device(uint16_t vendor_id, uint16_t device_id) {
  for (int i = 0; i < pci_count; i++) {
    if (devices[i]->vendor_id == vendor_id &&
        devices[i]->device_id == device_id)
      return devices[i];
  }
  return (void *)0;
}

pci_device_t *pci_get_net() {
  for (int i = 0; i < pci_count; i++)
    if (devices[i]->class_id == 0x02 && devices[i]->subclass_id == 0x00)
      return devices[i];
  return (void *)0;
}

pci_device_t *pci_get_wireless() {
  for (int i = 0; i < pci_count; i++) {
    // Class 0x02 Subclass 0x80 (Other) or 0x11 (Wireless)
    if (devices[i]->class_id == 0x02 &&
        (devices[i]->subclass_id == 0x80 || devices[i]->subclass_id == 0x11)) {
      return devices[i];
    }
  }
  return (void *)0;
}

pci_device_t *pci_get_usb(uint8_t interface_type) {
  for (int i = 0; i < usb_device_count; i++) {
      if (interface_type == 0xFF) return usb_devices[i];

      uint32_t class_reg = pci_read_config(usb_devices[i]->bus,
                                           usb_devices[i]->device,
                                           usb_devices[i]->function, 0x08);
      uint8_t prog_if = (class_reg >> 8) & 0xFF;
      if (prog_if == interface_type) return usb_devices[i];
  }
  return (void *)0;
}

int pci_usb_device_count(uint8_t interface_type) {
  int count = 0;
  for (int i = 0; i < usb_device_count; i++) {
    uint32_t class_reg = pci_read_config(usb_devices[i]->bus,
                                         usb_devices[i]->device,
                                         usb_devices[i]->function, 0x08);
    uint8_t prog_if = (class_reg >> 8) & 0xFF;
    if (interface_type == 0xFF || prog_if == interface_type) count++;
  }
  return count;
}

pci_device_t *pci_get_usb_by_index(uint8_t interface_type, int index) {
  if (index < 0) return (void *)0;
  for (int i = 0; i < usb_device_count; i++) {
    uint32_t class_reg = pci_read_config(usb_devices[i]->bus,
                                         usb_devices[i]->device,
                                         usb_devices[i]->function, 0x08);
    uint8_t prog_if = (class_reg >> 8) & 0xFF;
    if (interface_type != 0xFF && prog_if != interface_type) continue;
    if (index-- == 0) return usb_devices[i];
  }
  return (void *)0;
}

static char *pci_hex_field(char *out, uint64_t value, uint8_t digits) {
  static const char hex[] = "0123456789abcdef";
  for (int shift = (int)(digits - 1) * 4; shift >= 0; shift -= 4)
    *out++ = hex[(value >> shift) & 0xF];
  return out;
}

static const char *pci_usb_type(uint8_t prog_if) {
  switch (prog_if) {
  case 0x00: return "UHCI";
  case 0x10: return "OHCI";
  case 0x20: return "EHCI";
  case 0x30: return "xHCI";
  default: return "USB?";
  }
}

void pci_print_usb_inventory(void) {
  if (usb_device_count == 0) {
    serial_print("[usb-pci] no PCI USB controllers found\n");
    vga_puts("[usb-pci] no PCI USB controllers found\n");
    return;
  }

  for (int i = 0; i < usb_device_count; i++) {
    pci_device_t *d = usb_devices[i];
    uint32_t class_reg = pci_read_config(d->bus, d->device, d->function, 0x08);
    uint8_t prog_if = (class_reg >> 8) & 0xFF;
    char line[64];
    char *p = line;
    const char *type = pci_usb_type(prog_if);
    const char *prefix = "[usb-pci] ";
    while (*prefix) *p++ = *prefix++;
    while (*type) *p++ = *type++;
    *p++ = ' ';
    p = pci_hex_field(p, d->bus, 2);
    *p++ = ':';
    p = pci_hex_field(p, d->device, 2);
    *p++ = '.';
    p = pci_hex_field(p, d->function, 1);
    *p++ = ' ';
    p = pci_hex_field(p, d->vendor_id, 4);
    *p++ = ':';
    p = pci_hex_field(p, d->device_id, 4);
    *p = '\0';
    serial_print(line);
    serial_print("\n");
    vga_puts(line);
    vga_puts("\n");
  }
}

static char *pci_text(char *out, const char *text) {
  while (*text) *out++ = *text++;
  return out;
}

static char *pci_bdf(char *out, const pci_device_t *d) {
  out = pci_hex_field(out, d->bus, 2);
  *out++ = ':';
  out = pci_hex_field(out, d->device, 2);
  *out++ = '.';
  return pci_hex_field(out, d->function, 1);
}

static void pci_usb_diag_line(const char *line) {
  serial_print(line);
  serial_print("\n");
  vga_puts(line);
  vga_puts("\n");
}

/* Read-only EHCI inspection. Never claims ownership, resets the controller,
 * changes port registers, or enables PCI memory/bus-master decoding. */
static void pci_ehci_probe_readonly(const pci_device_t *d) {
  uint32_t command = pci_read_config(d->bus, d->device, d->function, 0x04);
  uint16_t pci_command = (uint16_t)command;
  uint32_t bar_low = pci_read_config(d->bus, d->device, d->function, 0x10);
  uint64_t bar = 0;
  uint8_t bar_type;

  if (!bar_low || bar_low == 0xFFFFFFFF || (bar_low & 1)) {
    pci_usb_diag_line("[ehci-ro] unusable memory BAR0");
    return;
  }
  bar_type = (bar_low >> 1) & 0x3;
  if (bar_type == 0x2) {
    uint32_t bar_high = pci_read_config(d->bus, d->device, d->function, 0x14);
    bar = ((uint64_t)bar_high << 32) | (bar_low & ~0xFULL);
  } else if (bar_type == 0) {
    bar = bar_low & ~0xFULL;
  } else {
    pci_usb_diag_line("[ehci-ro] unsupported BAR0 type");
    return;
  }

  char line[80];
  char *p = pci_text(line, "[ehci-ro] ");
  p = pci_bdf(p, d);
  p = pci_text(p, " BAR0=");
  p = pci_hex_field(p, bar, 16);
  p = pci_text(p, (pci_command & (1u << 1)) ? " MSE=1" : " MSE=0");
  *p = '\0';
  pci_usb_diag_line(line);

  /* Do not touch an address if the firmware has not enabled MMIO decoding. */
  if (!(pci_command & (1u << 1))) {
    pci_usb_diag_line("[ehci-ro] skipped; PCI memory decoding is disabled");
    return;
  }
  if (!bar || bar >= DIRECT_MAP_SIZE || DIRECT_MAP_SIZE - bar < 0x200) {
    pci_usb_diag_line("[ehci-ro] BAR0 is outside the supported direct map");
    return;
  }

  uint64_t first_page = bar & ~((uint64_t)PAGE_SIZE - 1);
  uint64_t last_page = (bar + 0x1FF) & ~((uint64_t)PAGE_SIZE - 1);
  for (uint64_t page = first_page;; page += PAGE_SIZE) {
    if (vmm_map_page_in(kernel_directory, (void *)page, phys_to_virt(page),
                        PTE_P | PTE_W | PTE_PCD | PTE_NX) < 0) {
      pci_usb_diag_line("[ehci-ro] MMIO mapping failed");
      return;
    }
    if (page == last_page) break;
  }

  volatile uint8_t *regs = (volatile uint8_t *)phys_to_virt(bar);
  uint8_t cap_length = regs[0];
  if (cap_length < 0x10 || cap_length > 0x40) {
    p = pci_text(line, "[ehci-ro] ");
    p = pci_bdf(p, d);
    p = pci_text(p, " invalid CAPLENGTH=");
    p = pci_hex_field(p, cap_length, 2);
    *p = '\0';
    pci_usb_diag_line(line);
    return;
  }

  volatile uint32_t *cap_regs = (volatile uint32_t *)regs;
  uint32_t hcsparams = cap_regs[1];
  uint32_t hccparams = cap_regs[2];
  uint32_t port_count = hcsparams & 0xF;
  if (port_count == 0 || port_count > 15) {
    pci_usb_diag_line("[ehci-ro] invalid HCSPARAMS port count");
    return;
  }

  p = pci_text(line, "[ehci-ro] ");
  p = pci_bdf(p, d);
  p = pci_text(p, " CAP=");
  p = pci_hex_field(p, cap_length, 2);
  p = pci_text(p, " HCS=");
  p = pci_hex_field(p, hcsparams, 8);
  p = pci_text(p, " HCC=");
  p = pci_hex_field(p, hccparams, 8);
  p = pci_text(p, " ports=");
  p = pci_hex_field(p, port_count, 1);
  *p = '\0';
  pci_usb_diag_line(line);

  volatile uint32_t *op_regs =
      (volatile uint32_t *)(regs + cap_length);
  uint32_t usbcmd = op_regs[0];
  uint32_t usbsts = op_regs[1];
  uint32_t configflag = op_regs[0x10];
  uint8_t legacy_offset = (hccparams >> 8) & 0xFF;
  uint32_t legacy = 0xFFFFFFFF;
  if (legacy_offset >= 0x40 && legacy_offset <= 0xFC)
    legacy = pci_read_config(d->bus, d->device, d->function, legacy_offset);

  p = pci_text(line, "[ehci-ro] ");
  p = pci_bdf(p, d);
  p = pci_text(p, " CMD=");
  p = pci_hex_field(p, usbcmd, 8);
  p = pci_text(p, " STS=");
  p = pci_hex_field(p, usbsts, 8);
  p = pci_text(p, " CF=");
  p = pci_hex_field(p, configflag, 8);
  p = pci_text(p, " LEG=");
  p = pci_hex_field(p, legacy, 8);
  *p = '\0';
  pci_usb_diag_line(line);

  uint32_t connected = 0;
  for (uint32_t port = 0; port < port_count; port++) {
    uint32_t portsc = op_regs[0x11 + port]; /* Operational + 0x44 + 4*n */
    p = pci_text(line, "[ehci-ro] ");
    p = pci_bdf(p, d);
    p = pci_text(p, " PORT");
    p = pci_hex_field(p, port, 1);
    p = pci_text(p, "=");
    p = pci_hex_field(p, portsc, 8);
    *p = '\0';
    pci_usb_diag_line(line);
    if (portsc & 1) connected++;
  }
  if (!connected) {
    p = pci_text(line, "[ehci-ro] ");
    p = pci_bdf(p, d);
    p = pci_text(p, " no connected root ports");
    *p = '\0';
    pci_usb_diag_line(line);
  }
}

void pci_probe_ehci_readonly(void) {
  for (int i = 0; i < usb_device_count; i++) {
    pci_device_t *d = usb_devices[i];
    uint32_t class_reg = pci_read_config(d->bus, d->device, d->function, 0x08);
    if (((class_reg >> 8) & 0xFF) == 0x20)
      pci_ehci_probe_readonly(d);
  }
}

pci_device_t *pci_get_ahci() {
  for (int i = 0; i < pci_count; i++) {
    if (devices[i]->class_id == 0x01 && devices[i]->subclass_id == 0x06) {
      uint32_t class_reg = pci_read_config(devices[i]->bus, devices[i]->device, devices[i]->function, 0x08);
      uint8_t prog_if = (class_reg >> 8) & 0xFF;
      if (prog_if == 0x01) { // AHCI
        return devices[i];
      }
    }
  }
  return (void *)0;
}

pci_device_t *pci_get_audio() {
  for (int i = 0; i < pci_count; i++) {
    // Class 0x04 (Multimedia), Subclass 0x01 (Audio)
    if (devices[i]->class_id == 0x04 && devices[i]->subclass_id == 0x01) {
      return devices[i];
    }
  }
  return (void *)0;
}

pci_device_t *pci_get_hda() {
  pci_device_t *fallback = (void *)0;
  for (int i = 0; i < pci_count; i++) {
    // Class 0x04 (Multimedia), Subclass 0x03 (HD Audio)
    if (devices[i]->class_id == 0x04 && devices[i]->subclass_id == 0x03) {
      // Prioridade máxima: Áudio PCH Onboard da placa-mãe (Bus 0, Device 27 [0x1B] ou Device 31)
      if (devices[i]->bus == 0 && (devices[i]->device == 27 || devices[i]->device == 31)) {
        return devices[i];
      }
      // Prioridade: IDs de PCH Intel conhecidos (Cougar Point 0x1C20 / Panther Point 0x1E20 / etc.)
      if (devices[i]->vendor_id == 0x8086 &&
          (devices[i]->device_id == 0x1c20 || devices[i]->device_id == 0x1e20 ||
           devices[i]->device_id == 0x8c20 || devices[i]->device_id == 0x9c20 ||
           devices[i]->device_id == 0xa170 || devices[i]->device_id == 0x2668 ||
           devices[i]->device_id == 0x27d8 || devices[i]->device_id == 0x284b ||
           devices[i]->device_id == 0x293e || devices[i]->device_id == 0x3a3e ||
           devices[i]->device_id == 0x3b56)) {
        return devices[i];
      }
      if (!fallback) fallback = devices[i];
    }
  }
  return fallback;
}

int pci_get_hda_count(void) {
  int count = 0;
  for (int i = 0; i < pci_count; i++) {
    if (devices[i]->class_id == 0x04 && devices[i]->subclass_id == 0x03) {
      count++;
    }
  }
  return count;
}

pci_device_t *pci_get_hda_by_index(int index) {
  int cur = 0;
  // Primeiro retorna dispositivos que sejam PCH onboard
  for (int i = 0; i < pci_count; i++) {
    if (devices[i]->class_id == 0x04 && devices[i]->subclass_id == 0x03) {
      if (devices[i]->bus == 0 && (devices[i]->device == 27 || devices[i]->device == 31)) {
        if (cur == index) return devices[i];
        cur++;
      }
    }
  }
  // Depois retorna os demais dispositivos HDA (HDMI, placas adicionais, etc.)
  for (int i = 0; i < pci_count; i++) {
    if (devices[i]->class_id == 0x04 && devices[i]->subclass_id == 0x03) {
      if (!(devices[i]->bus == 0 && (devices[i]->device == 27 || devices[i]->device == 31))) {
        if (cur == index) return devices[i];
        cur++;
      }
    }
  }
  return (void *)0;
}

int pci_get_device_count(void) {
  return pci_count;
}

pci_device_t *pci_get_device_by_index(int index) {
  if (index >= 0 && index < pci_count) return devices[index];
  return (void *)0;
}

pci_device_t *pci_get_nvme() {
  for (int i = 0; i < pci_count; i++) {
    // Class 0x01 (Mass Storage), Subclass 0x08 (NVM Express)
    if (devices[i]->class_id == 0x01 && devices[i]->subclass_id == 0x08) {
      return devices[i];
    }
  }
  return (void *)0;
}
