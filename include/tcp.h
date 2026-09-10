#ifndef TCP_H
#define TCP_H

#include "net.h"
#include <stdint.h>

// TCP Header
typedef struct {
  uint16_t src_port;
  uint16_t dest_port;
  uint32_t seq_num;
  uint32_t ack_num;
  uint8_t data_offset_reserved; // 4 bits data offset, 4 bits reserved usually 0
  uint8_t flags;
  uint16_t window_size;
  uint16_t checksum;
  uint16_t urgent_pointer;
} __attribute__((packed)) tcp_header_t;

// TCP Flags
#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10
#define TCP_URG 0x20

// TCP State
typedef enum {
  TCP_CLOSED,
  TCP_LISTEN,
  TCP_SYN_SENT,
  TCP_SYN_RECEIVED,
  TCP_ESTABLISHED,
  TCP_FIN_WAIT_1,
  TCP_FIN_WAIT_2,
  TCP_CLOSE_WAIT,
  TCP_CLOSING,
  TCP_LAST_ACK,
  TCP_TIME_WAIT
} tcp_state_t;

// TCP Socket Structure (Minimal)
typedef struct tcp_socket {
  uint16_t local_port;
  uint16_t remote_port;
  uint32_t remote_ip;
  uint32_t local_ip;

  uint32_t seq_num;
  uint32_t ack_num;

  tcp_state_t state;

  // Buffers (Circular buffer would be better, but linear for simplicity now)
  uint8_t *recv_buffer;
  uint32_t recv_buffer_size;
  uint32_t recv_write_ptr;
  uint32_t recv_read_ptr;

  struct tcp_socket *next;
} tcp_socket_t;

void tcp_init();
tcp_socket_t *tcp_connect(uint32_t dest_ip, uint16_t dest_port);
void tcp_send(tcp_socket_t *socket, const uint8_t *data, uint32_t len);
void tcp_close(tcp_socket_t *socket);
int tcp_receive(tcp_socket_t *socket, uint8_t *buffer, uint32_t len);

// Called from netstack
void tcp_handle_packet(void *packet, uint16_t len, uint32_t src_ip,
                       uint32_t dest_ip);

#endif
