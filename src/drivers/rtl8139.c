#include "rtl8139.h"
#include "io.h"
#include "kheap.h"
#include "net.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include <stdbool.h>

static uint32_t io_base;
static uint8_t mac[6];
static uint8_t *rx_buffer;
static uint64_t rx_buffer_phys;
static uint32_t rx_offset = 0;
static net_interface_t rtl_netif;
static uint8_t *tx_buffers[4];
static uint64_t tx_buffer_phys[4];
static uint8_t tx_next = 0;

void rtl8139_send_packet_internal(net_interface_t *self, void *data,
                                  uint32_t len) {
  uint8_t slot;
  uint32_t status;
  uint32_t transmit_len = len;

  (void)self;

  if (!data || len == 0) {
    return;
  }

  if (transmit_len < 60) {
    transmit_len = 60;
  }

  if (transmit_len > 1792) {
    transmit_len = 1792;
  }

  bool sent = false;
  int retry_count = 0;

  while (!sent && retry_count < 200) {
    asm volatile("cli");
    for (int tries = 0; tries < 4; tries++) {
      slot = (uint8_t)((tx_next + tries) & 3);
      status = inl(io_base + 0x10 + (slot * 4));
      if (status & (1 << 13)) { // OWN bit (CPU owns it)
        memset(tx_buffers[slot], 0, transmit_len);
        memcpy(tx_buffers[slot], data, len);
        outl(io_base + 0x20 + (slot * 4), tx_buffer_phys[slot]);
        outl(io_base + 0x10 + (slot * 4), transmit_len & 0x1FFF);
        tx_next = (uint8_t)((slot + 1) & 3);
        sent = true;
        break;
      }
    }
    asm volatile("sti");
    if (!sent) {
        retry_count++;
        for(int i=0; i<20; i++) asm volatile("pause");
    }
  }
}

void rtl8139_handler() {
  uint16_t status = inw(io_base + 0x3E);
  outw(io_base + 0x3E, status);

  if (status & 0x10) { // RxOverflow
    static unsigned dbg_ov = 0;
    if (dbg_ov++ < 10) {
      extern void serial_print(const char*s);
      extern char *itoa(int value, char *str, int base);
      char dbg[48];
      serial_print("[net] RX OVERFLOW rx_offset=");
      itoa((int)rx_offset, dbg, 10); serial_print(dbg);
      serial_print(" missed=");
      itoa((int)inl(io_base + 0x4C), dbg, 10); serial_print(dbg);
      serial_print("\n");
    }
  }

  if (status & 0x01) { // ROK
    while (1) {
      uint32_t *packet_header = (uint32_t *)(rx_buffer + rx_offset);
      uint32_t header = *packet_header;
      uint16_t packet_len = header >> 16;
      uint16_t pkt_status = header & 0xFFFF;

      if (pkt_status & 0x8000) { // OWN bit = NIC still owns it
        break;
      }
      if (packet_len == 0xFFF0 || packet_len == 0) {
        break;
      }
      {
        static unsigned dbg_isz = 0;
        if (dbg_isz++ < 40) {
          extern void serial_print(const char*s);
          extern char *itoa(int value, char *str, int base);
          char dbg[48];
          serial_print("[net] isr len=");
          itoa(packet_len - 4, dbg, 10); serial_print(dbg);
          serial_print(" off=");
          itoa((int)rx_offset, dbg, 10); serial_print(dbg);
          serial_print("\n");
        }
      }

      void *packet_data = (void *)(rx_buffer + rx_offset + 4);
      extern void netstack_handle_packet(void *data, uint16_t len);
      if (packet_len >= 4) {
        netstack_handle_packet(packet_data, packet_len - 4);
      }

      rx_offset = (rx_offset + packet_len + 4 + 3) & ~3U;
      outw(io_base + 0x38, (uint16_t)(rx_offset - 16));
      if (rx_offset >= 16384) {
        uint32_t wrapped = rx_offset - 16384;
        *(uint32_t *)(rx_buffer + wrapped) = 0xFFF00000;
        rx_offset = wrapped;
      } else {
        *(uint32_t *)(rx_buffer + rx_offset) = 0xFFF00000;
      }
    }
  }
}

void init_rtl8139(pci_device_t *dev) {
  uint32_t command;
  io_base = pci_read_config(dev->bus, dev->device, dev->function, 0x10) & (~0x3);
  command = pci_read_config(dev->bus, dev->device, dev->function, 0x04);
  command |= 0x00000005;
  pci_write_config(dev->bus, dev->device, dev->function, 0x04, command);

  outb(io_base + 0x52, 0x00);
  outb(io_base + 0x37, 0x10);
  { volatile int rtl_timeout = 1000000; while (rtl_timeout-- && ((inb(io_base + 0x37) & 0x10) != 0)); }

  rx_buffer = (uint8_t *)kmalloc_ap(16384 + 16 + 1500, &rx_buffer_phys);
  memset(rx_buffer, 0, 16384 + 16 + 1500);

  /* Seed the ring with the "empty" descriptor marker.  QEMU's rtl8139 model
   * never sets the OWN bit on received frames, so the only reliable
   * end-of-drain signal is the 0xFFF0 "empty" length marker: the driver
   * rewrites it at the consumption frontier after every frame. */
  {
    uint32_t seed = 0xFFF00000;
    for (size_t i = 0; i < 16384; i += 4)
      *(uint32_t *)(rx_buffer + i) = seed;
  }

  for (int i = 0; i < 4; i++) {
    tx_buffers[i] = (uint8_t *)kmalloc_ap(2048, &tx_buffer_phys[i]);
    memset(tx_buffers[i], 0, 2048);
  }

  /* kmalloc_ap retorna VA = PHY + HEADER_SIZE(8) mas grava *phys = base do bloco.
     Como o kernel usa mapeamento identidade, o ponteiro de CPU dos buffers DMA
     deve apontar para o proprio endereco fisico (o NIC/DMA opera sobre ele).
     Sem isso o driver le/escreve 8 bytes adiante do anel real. */
  rx_buffer = (uint8_t *)(uintptr_t)rx_buffer_phys;
  for (int i = 0; i < 4; i++) {
    tx_buffers[i] = (uint8_t *)(uintptr_t)tx_buffer_phys[i];
  }

  outl(io_base + 0x30, rx_buffer_phys);
  outw(io_base + 0x38, 0x0000);  // Initialize CAPR = 0 (CBR starts at 0 after Rx Reset)
  outw(io_base + 0x3C, 0x009F);
  outl(io_base + 0x44, 0x8F | (1 << 11));  // RCR: AB|AM|AP|AER|WRAP + Rx buffer 16K
  outb(io_base + 0x37, 0x0C);  // CR: RE=1, TE=1 (enable receiver & transmitter)

  for (int i = 0; i < 6; i++) {
    mac[i] = inb(io_base + i);
    rtl_netif.mac[i] = mac[i];
  }

  strcpy(rtl_netif.name, "eth0");
  rtl_netif.type = NET_TYPE_ETHERNET;
  rtl_netif.send_packet = rtl8139_send_packet_internal;
  rtl_netif.next = NULL;
  net_register_interface(&rtl_netif);

/* O kernel roteia as linhas 10 e 11 do IOAPIC para o vetor 43 (apic.c), e
   irq_handler computa irq = vetor - 32 = 11: a interrupcao da NIC sempre
   chega como IRQ 11 interna, entao o handler deve ficar em irq_handlers[11].
   A linha do device (0x3C) vira apenas informativo. */
  uint8_t irq_line = dev->interrupt_line;
  if (irq_line == 0 || irq_line > 15) {
    static const uint8_t pirq_to_line[4] = {11, 10, 11, 10};
    uint16_t pin = pci_read_config(dev->bus, dev->device, dev->function, 0x3D) & 0xFF;
    if (pin == 0) pin = 1;
    irq_line = pirq_to_line[(dev->device + pin - 1) & 3];
  }
  serial_print("rtl8139: irq line=");
  char irq_db[8];
  extern char *itoa(int value, char *str, int base);
  itoa(irq_line, irq_db, 10);
  serial_print(irq_db);
  serial_print(" -> handler irq 11\n");
  extern void irq_install_handler(uint8_t irq, void (*handler)(void));
  irq_install_handler(11, rtl8139_handler);
}

void rtl8139_send_packet(void *data, uint32_t len) {
  rtl8139_send_packet_internal(&rtl_netif, data, len);
}

uint8_t *rtl8139_get_mac() { return mac; }
