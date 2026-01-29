#include "r8169.h"
#include "io.h"
#include "kheap.h"
#include "net.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include <stdbool.h>
#include <stdint.h>

#define R8169_REG_MAC0       0x00
#define R8169_REG_CMD        0x37
#define R8169_REG_IMR        0x3C
#define R8169_REG_ISR        0x3E
#define R8169_REG_TX_DESC_LO 0x40
#define R8169_REG_TX_DESC_HI 0x44
#define R8169_REG_RX_DESC_LO 0x50
#define R8169_REG_RX_DESC_HI 0x54
#define R8169_REG_RCR        0x58
#define R8169_REG_TCR        0x5C
#define R8169_REG_PHYAR      0x60

#define R8169_CMD_RESET      (1 << 4)
#define R8169_CMD_RX_EN      (1 << 3)
#define R8169_CMD_TX_EN      (1 << 2)

#define R8169_RCR_AAP        (1 << 0)
#define R8169_RCR_APM        (1 << 1)
#define R8169_RCR_AM         (1 << 2)
#define R8169_RCR_AB         (1 << 3)
#define R8169_RCR_WRAP       (1 << 7)

#define R8169_TCR_IFG_SHIFT  24
#define R8169_TCR_MXDMA_SHIFT 8

#define R8169_TX_DESC_NUM    16
#define R8169_RX_DESC_NUM    16
#define R8169_RX_BUF_SIZE    1536

#define R8169_TX_OWN         (1 << 31)
#define R8169_TX_EOF         (1 << 30)
#define R8169_TX_SOF         (1 << 29)
#define R8169_TX_FS          (1 << 28)
#define R8169_TX_LS          (1 << 27)

#define R8169_RX_OWN         (1 << 31)
#define R8169_RX_EOF         (1 << 30)
#define R8169_RX_SOF         (1 << 29)
#define R8169_RX_FS          (1 << 28)
#define R8169_RX_LS          (1 << 27)

typedef struct {
    uint32_t opts1;
    uint32_t opts2;
    uint64_t addr;
} __attribute__((packed)) r8169_desc_t;

static uint32_t io_base = 0;
static uint8_t mac[6];
static r8169_desc_t *tx_ring = NULL;
static uint64_t tx_ring_phys = 0;
static uint8_t *tx_buffers[R8169_TX_DESC_NUM];
static uint64_t tx_buffer_phys[R8169_TX_DESC_NUM];
static uint32_t tx_prod = 0;

static r8169_desc_t *rx_ring = NULL;
static uint64_t rx_ring_phys = 0;
static uint8_t *rx_buffers[R8169_RX_DESC_NUM];
static uint64_t rx_buffer_phys[R8169_RX_DESC_NUM];
static uint32_t rx_prod = 0;

static net_interface_t r8169_netif;
static pci_device_t *r8169_dev = NULL;

static uint32_t r8169_read_mac(uint32_t reg) {
    return inl(io_base + reg);
}

static void r8169_write_mac(uint32_t reg, uint32_t val) {
    outl(io_base + reg, val);
}

static void r8169_write_mac8(uint32_t reg, uint8_t val) {
    outb(io_base + reg, val);
}

static uint8_t r8169_read_mac8(uint32_t reg) {
    return inb(io_base + reg);
}

static void r8169_reset() {
    r8169_write_mac8(R8169_REG_CMD, R8169_CMD_RESET);
    for (int i = 0; i < 10000; i++) {
        if (!(r8169_read_mac8(R8169_REG_CMD) & R8169_CMD_RESET)) break;
    }
}

static void r8169_init_rings() {
    tx_ring = (r8169_desc_t *)kmalloc_ap(R8169_TX_DESC_NUM * sizeof(r8169_desc_t), &tx_ring_phys);
    rx_ring = (r8169_desc_t *)kmalloc_ap(R8169_RX_DESC_NUM * sizeof(r8169_desc_t), &rx_ring_phys);

    for (int i = 0; i < R8169_TX_DESC_NUM; i++) {
        tx_buffers[i] = (uint8_t *)kmalloc_ap(1536, &tx_buffer_phys[i]);
        tx_ring[i].opts1 = 0;
        tx_ring[i].opts2 = 0;
        tx_ring[i].addr = tx_buffer_phys[i];
    }

    for (int i = 0; i < R8169_RX_DESC_NUM; i++) {
        rx_buffers[i] = (uint8_t *)kmalloc_ap(R8169_RX_BUF_SIZE, &rx_buffer_phys[i]);
        rx_ring[i].opts1 = R8169_RX_OWN;
        rx_ring[i].opts2 = R8169_RX_BUF_SIZE;
        rx_ring[i].addr = rx_buffer_phys[i];
    }

    r8169_write_mac(R8169_REG_TX_DESC_LO, (uint32_t)tx_ring_phys);
    r8169_write_mac(R8169_REG_TX_DESC_HI, (uint32_t)(tx_ring_phys >> 32));
    r8169_write_mac(R8169_REG_RX_DESC_LO, (uint32_t)rx_ring_phys);
    r8169_write_mac(R8169_REG_RX_DESC_HI, (uint32_t)(rx_ring_phys >> 32));
}

static void r8169_start() {
    uint32_t cmd = r8169_read_mac8(R8169_REG_CMD);
    cmd |= R8169_CMD_RX_EN | R8169_CMD_TX_EN;
    r8169_write_mac8(R8169_REG_CMD, cmd);

    r8169_write_mac(R8169_REG_IMR, 0xFFFF);
    r8169_write_mac(R8169_REG_RCR, R8169_RCR_AAP | R8169_RCR_APM | R8169_RCR_AM | R8169_RCR_AB | R8169_RCR_WRAP);
    r8169_write_mac(R8169_REG_TCR, (0x20 << R8169_TCR_IFG_SHIFT) | (0x6 << R8169_TCR_MXDMA_SHIFT));
}

void r8169_send_packet_internal(net_interface_t *self, void *data, uint32_t len) {
    (void)self;
    if (!data || len == 0) return;
    if (len < 60) len = 60;
    if (len > 1500) len = 1500;

    uint32_t next = (tx_prod + 1) % R8169_TX_DESC_NUM;
    int retries = 0;
    while (tx_ring[tx_prod].opts1 & R8169_TX_OWN) {
        if (++retries > 10000) return;
    }

    memcpy(tx_buffers[tx_prod], data, len);
    tx_ring[tx_prod].opts1 = R8169_TX_OWN | R8169_TX_SOF | R8169_TX_EOF | (len & 0x1FFF);
    tx_ring[tx_prod].opts2 = 0;

    asm volatile("mfence" ::: "memory");
    tx_prod = next;
}

void r8169_handler() {
    uint32_t status = r8169_read_mac(R8169_REG_ISR);
    if (!status) return;
    r8169_write_mac(R8169_REG_ISR, status);

    if (status & 0x01) {
        while (1) {
            r8169_desc_t *desc = &rx_ring[rx_prod];
            if (desc->opts1 & R8169_RX_OWN) break;

            uint32_t opts1 = desc->opts1;
            uint32_t len = opts1 & 0x1FFF;

            if (len > 4 && len < R8169_RX_BUF_SIZE) {
                extern void netstack_handle_packet(void *data, uint16_t len);
                netstack_handle_packet(rx_buffers[rx_prod], (uint16_t)(len - 4));
            }

            desc->opts1 = R8169_RX_OWN;
            desc->opts2 = R8169_RX_BUF_SIZE;
            asm volatile("mfence" ::: "memory");
            rx_prod = (rx_prod + 1) % R8169_RX_DESC_NUM;
        }
    }
}

void init_r8169(pci_device_t *dev) {
    r8169_dev = dev;

    uint32_t bar0 = pci_read_config(dev->bus, dev->device, dev->function, 0x10);
    io_base = bar0 & ~0xF;
    serial_print("r8169: IO base = 0x");
    char h[16];
    for (int i = 28; i >= 0; i -= 4) {
        char c = "0123456789ABCDEF"[(io_base >> i) & 0xF];
        h[7 - i/4] = c;
    }
    h[8] = '\n'; h[9] = 0;
    serial_print(h);

    uint32_t cmd = pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    cmd |= 0x07;
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, cmd);

    uint32_t irq = pci_read_config(dev->bus, dev->device, dev->function, 0x3C) & 0xFF;
    serial_print("r8169: IRQ = "); itoa(irq, h, 10); serial_print(h); serial_print("\n");

    extern void irq_install_handler(int irq, void (*handler)(void));
    irq_install_handler(irq, r8169_handler);

    r8169_reset();

    for (int i = 0; i < 6; i++) {
        mac[i] = r8169_read_mac8(R8169_REG_MAC0 + i);
    }

    r8169_init_rings();
    r8169_start();

    strcpy(r8169_netif.name, "eth0");
    r8169_netif.type = NET_TYPE_ETHERNET;
    memcpy(r8169_netif.mac, mac, 6);
    r8169_netif.send_packet = r8169_send_packet_internal;
    r8169_netif.driver_data = NULL;
    r8169_netif.next = NULL;

    net_register_interface(&r8169_netif);

    serial_print("r8169: MAC = ");
    for (int i = 0; i < 6; i++) {
        char b[3];
        itoa(mac[i] >> 4, b, 16); serial_print(b);
        itoa(mac[i] & 0xF, b, 16); serial_print(b);
        if (i < 5) serial_print(":");
    }
    serial_print("\n");
    serial_print("r8169: RTL8168/8111 initialized\n");
}