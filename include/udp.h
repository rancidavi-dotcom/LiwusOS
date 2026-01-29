#ifndef UDP_H
#define UDP_H

#include <stdint.h>

typedef struct {
    uint16_t src_port;
    uint16_t dest_port;
    uint16_t len;
    uint16_t checksum;
} __attribute__((packed)) udp_header_t;

typedef void (*udp_callback_t)(uint32_t src_ip, uint16_t src_port, void *data, uint16_t len);

void udp_init();
void udp_send(uint32_t dest_ip, uint16_t src_port, uint16_t dest_port, void *data, uint16_t len);
void udp_handle_packet(void *packet, uint16_t len, uint32_t src_ip, uint32_t dest_ip);
void udp_register_callback(uint16_t port, udp_callback_t callback);

#endif
