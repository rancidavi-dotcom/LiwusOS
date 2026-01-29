#ifndef TLS_SOCKET_H
#define TLS_SOCKET_H

#include <stdint.h>
#include <stddef.h>
#include "tcp.h"
#include "bearssl.h"

/* TLS socket wrapper */
typedef struct {
    tcp_socket_t *tcp_sock;
    br_ssl_client_context *ssl_client;
    br_sslio_context ssio;
    void *ctx;
    unsigned char *io_buf;
    int connected;
} tls_socket_t;

/* Initialize TLS library (call once at startup) */
int tls_init(void);

/* Create TLS context for client */
tls_socket_t *tls_socket_create(const char *hostname);

/* Connect to host:port and perform TLS handshake */
int tls_socket_connect(tls_socket_t *tls, uint32_t ip, uint16_t port, const char *sni);

/* TLS read/write */
int tls_socket_read(tls_socket_t *tls, uint8_t *buf, uint32_t len);
int tls_socket_write(tls_socket_t *tls, const uint8_t *buf, uint32_t len);

/* Close and cleanup */
void tls_socket_close(tls_socket_t *tls);

/* Check if TLS socket is connected */
int tls_socket_is_connected(tls_socket_t *tls);

#endif