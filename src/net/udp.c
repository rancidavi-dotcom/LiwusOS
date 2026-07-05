#include "udp.h"
#include "kheap.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"

#define MAX_UDP_CALLBACKS 16

typedef struct {
    uint16_t port;
    udp_callback_t callback;
} udp_registration_t;

static udp_registration_t registrations[MAX_UDP_CALLBACKS];
static int registration_count = 0;

void udp_init() {
    memset(registrations, 0, sizeof(registrations));
    registration_count = 0;
}

void udp_register_callback(uint16_t port, udp_callback_t callback) {
    if (registration_count < MAX_UDP_CALLBACKS) {
        registrations[registration_count].port = port;
        registrations[registration_count].callback = callback;
        registration_count++;
    }
}

void udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, void *data, uint16_t len) {
    uint16_t packet_len = sizeof(udp_header_t) + len;
    uint8_t *packet = (uint8_t *)kmalloc(packet_len);
    memset(packet, 0, packet_len);

    udp_header_t *hdr = (udp_header_t *)packet;
    hdr->src_port = htons(src_port);
    hdr->dest_port = htons(dest_port);
    hdr->len = htons(packet_len);
    hdr->checksum = 0; // Checksum is optional in UDP, keeping 0 for simplicity

    memcpy(packet + sizeof(udp_header_t), data, len);

    netstack_send_ipv4(dest_ip, 17, packet, packet_len); // Proto 17 = UDP
    kfree(packet);
}

void udp_handle_packet(void *packet, uint16_t len, uint32_t src_ip, uint32_t dest_ip) {
    (void)dest_ip;
    udp_header_t *hdr = (udp_header_t *)packet;
    uint16_t dest_port = htons(hdr->dest_port);
    uint16_t src_port = htons(hdr->src_port);
    uint16_t data_len = htons(hdr->len) - sizeof(udp_header_t);
    void *data = (uint8_t *)packet + sizeof(udp_header_t);

    for (int i = 0; i < registration_count; i++) {
        if (registrations[i].port == dest_port) {
            registrations[i].callback(src_ip, src_port, data, data_len);
            return;
        }
    }

    serial_print("[udp] packet for unregistered port: ");
    char num[16];
    itoa(dest_port, num, 10);
    serial_print(num);
    serial_print("\n");
}
