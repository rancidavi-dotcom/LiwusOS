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

  while (!sent && retry_count < 1000) {
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
        for(int i=0; i<100; i++) asm volatile("pause");
    }
  }
}

void rtl8139_handler() {
  uint16_t status = inw(io_base + 0x3E);
  outw(io_base + 0x3E, status);

  if (status & 0x01) { // ROK
    while (!(inb(io_base + 0x37) & 0x01)) {
      uint16_t *packet_header = (uint16_t *)(rx_buffer + rx_offset);
      uint16_t packet_len = packet_header[1];
      void *packet_data = (void *)(rx_buffer + rx_offset + 4);

      if (packet_len == 0xFFF0) break;

      extern void netstack_handle_packet(void *data, uint16_t len);
      if (packet_len >= 4) {
        netstack_handle_packet(packet_data, packet_len - 4);
      }

      rx_offset = (rx_offset + packet_len + 4 + 3) & ~3U;
      outw(io_base + 0x38, (uint16_t)(rx_offset - 16));
      
      if (rx_offset >= 8192) {
        rx_offset %= 8192;
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

  rx_buffer = (uint8_t *)kmalloc_ap(8192 + 16 + 1500, &rx_buffer_phys);
  memset(rx_buffer, 0, 8192 + 16 + 1500);

  for (int i = 0; i < 4; i++) {
    tx_buffers[i] = (uint8_t *)kmalloc_ap(2048, &tx_buffer_phys[i]);
    memset(tx_buffers[i], 0, 2048);
  }

  outl(io_base + 0x30, rx_buffer_phys);
  outw(io_base + 0x3C, 0x0005);
  outl(io_base + 0x44, 0x0F | (1 << 7));
  outb(io_base + 0x37, 0x0C);

  for (int i = 0; i < 6; i++) {
    mac[i] = inb(io_base + i);
    rtl_netif.mac[i] = mac[i];
  }

  strcpy(rtl_netif.name, "eth0");
  rtl_netif.type = NET_TYPE_ETHERNET;
  rtl_netif.send_packet = rtl8139_send_packet_internal;
  rtl_netif.next = NULL;
  net_register_interface(&rtl_netif);
}

void rtl8139_send_packet(void *data, uint32_t len) {
  rtl8139_send_packet_internal(&rtl_netif, data, len);
}

uint8_t *rtl8139_get_mac() { return mac; }
