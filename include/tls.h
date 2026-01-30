#ifndef TLS_H
#define TLS_H

#include "tcp.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  tcp_socket_t *tcp_sock;
  uint8_t *session_key;
  uint32_t session_key_len;
  bool established;
} tls_socket_t;

tls_socket_t *tls_connect(uint32_t dest_ip, uint16_t dest_port);
void tls_send(tls_socket_t *sock, const uint8_t *data, uint32_t len);
int tls_receive(tls_socket_t *sock, uint8_t *buffer, uint32_t len);
void tls_close(tls_socket_t *sock);

#endif
