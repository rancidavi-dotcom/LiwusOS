#include "rtl8139.h"
#include "io.h"
#include "kheap.h"
#include "video.h"
#include "net.h"
#include "string.h"

static uint32_t io_base;
static uint8_t mac[6];
static uint8_t* rx_buffer;
static uint32_t rx_offset = 0;
static net_interface_t rtl_netif;

// Implementação real de envio de pacotes
void rtl8139_send_packet_internal(net_interface_t* self, void* data, uint32_t len) {
    (void)self;
    // Tenta encontrar um dos 4 descritores de transmissão livres
    for(int i=0; i<4; i++) {
        uint32_t status = inl(io_base + 0x10 + (i*4));
        if (status & 0x2000) { // Bit OWN: placa terminou de enviar
             outl(io_base + 0x20 + (i*4), (uint32_t)data);
             outl(io_base + 0x10 + (i*4), len & 0x1FFF);
             return;
        }
    }
}

void rtl8139_handler() {
    uint16_t status = inw(io_base + 0x3E);
    if (status & 0x01) { // ROK (Receive OK)
        while(!(inb(io_base + 0x37) & 0x01)) {
            uint16_t* packet_header = (uint16_t*)(rx_buffer + rx_offset);
            uint16_t packet_len = packet_header[1];
            
            void* packet_data = (void*)(rx_buffer + rx_offset + 4);
            extern void netstack_handle_packet(void* data, uint16_t len);
            netstack_handle_packet(packet_data, packet_len - 4);

            rx_offset = (rx_offset + packet_len + 4 + 3) & ~3;
            if (rx_offset >= 8192) rx_offset %= 8192;
            outw(io_base + 0x38, rx_offset - 16);
        }
    }
    outw(io_base + 0x3E, 0x05); // ACK interrupção
}

void init_rtl8139(pci_device_t* dev) {
    io_base = pci_read_config(dev->bus, dev->device, dev->function, 0x10) & (~0x3);
    
    outb(io_base + 0x52, 0x00); // Power on
    outb(io_base + 0x37, 0x10); // Reset
    while((inb(io_base + 0x37) & 0x10) != 0);
    
    rx_buffer = (uint8_t*)kmalloc(8192 + 16 + 1500);
    // Usamos um loop simples para o memset se string.h não for suficiente
    for(int i=0; i < (8192 + 16 + 1500); i++) rx_buffer[i] = 0;
    
    outl(io_base + 0x30, (uint32_t)rx_buffer);
    
    outw(io_base + 0x3C, 0x0005); // Habilita interrupção de RX e TX
    outl(io_base + 0x44, 0x0F | (1 << 7)); // Aceita Broadcast, Multicast e Unicast
    outb(io_base + 0x37, 0x0C); // Habilita RX/TX
    
    for(int i=0; i<6; i++) {
        mac[i] = inb(io_base + i);
        rtl_netif.mac[i] = mac[i];
    }

    /* Registra como interface de rede */
    strcpy(rtl_netif.name, "eth0");
    rtl_netif.type = NET_TYPE_ETHERNET;
    rtl_netif.send_packet = rtl8139_send_packet_internal;
    rtl_netif.next = NULL;
    net_register_interface(&rtl_netif);
}

void rtl8139_send_packet(void* data, uint32_t len) {
    rtl8139_send_packet_internal(&rtl_netif, data, len);
}

uint8_t* rtl8139_get_mac() {
    return mac;
}