#include "tls.h"
#include "kheap.h"
#include "string.h"
#include "video.h"

// This is a EXTREMELY minimal TLS 1.2 Client Hello implementation.
// In a real OS, you'd want MbedTLS or WolfSSL.
// Since we are building from scratch, we implement the bare minimum to "tickle"
// the server into starting a session.

tls_socket_t *tls_connect(uint32_t dest_ip, uint16_t dest_port) {
  tcp_socket_t *tcp = tcp_connect(dest_ip, dest_port);
  if (!tcp)
    return NULL;

  tls_socket_t *tls = (tls_socket_t *)kmalloc(sizeof(tls_socket_t));
  tls->tcp_sock = tcp;
  tls->established = false;

  // Send TLS Client Hello
  // Hardcoded for TLS 1.2
  uint8_t client_hello[] = {
      0x16,                                           // Handshake
      0x03, 0x03,                                     // Version TLS 1.2
      0x00, 0x2f,                                     // Length
      0x01,                                           // Client Hello
      0x00, 0x00, 0x2b,                               // Handshake length
      0x03, 0x03,                                     // Version TLS 1.2
      0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, // Random (Partial)
      0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13,
      0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
      0x00,       // Session ID length
      0x00, 0x02, // Cipher suites length
      0x00, 0x2f, // TLS_RSA_WITH_AES_128_CBC_SHA
      0x01,       // Compression methods length
      0x00        // NULL compression
  };

  tcp_send(tcp, client_hello, sizeof(client_hello));

  // For a real implementation, we would now wait for Server Hello,
  // handle Key Exchange, etc.
  // For this demonstration, we assume "established" to allow the HTTP layer to
  // work. WARNING: This is INSECURE and only for architectural demonstration.
  tls->established = true;

  return tls;
}

void tls_send(tls_socket_t *sock, const uint8_t *data, uint32_t len) {
  if (!sock || !sock->tcp_sock)
    return;

  // Wrap data in TLS Application Data record (0x17)
  uint8_t header[5];
  header[0] = 0x17; // Application Data
  header[1] = 0x03;
  header[2] = 0x03;
  header[3] = (len >> 8) & 0xFF;
  header[4] = len & 0xFF;

  tcp_send(sock->tcp_sock, header, 5);
  tcp_send(sock->tcp_sock, data, len);
}

int tls_receive(tls_socket_t *sock, uint8_t *buffer, uint32_t len) {
  if (!sock || !sock->tcp_sock)
    return -1;

  // Receive TLS record header
  uint8_t header[5];
  if (tcp_receive(sock->tcp_sock, header, 5) <= 0)
    return -1;

  uint32_t record_len = (header[3] << 8) | header[4];
  if (record_len > len)
    record_len = len;

  return tcp_receive(sock->tcp_sock, buffer, record_len);
}

void tls_close(tls_socket_t *sock) {
  if (!sock)
    return;
  tcp_close(sock->tcp_sock);
  kfree(sock);
}
