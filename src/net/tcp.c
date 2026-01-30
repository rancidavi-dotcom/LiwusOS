#include "tcp.h"
#include "kheap.h"
#include "netstack.h" // For checksum, IP send, etc
#include "string.h"
#include "video.h" // Debug

static tcp_socket_t *sockets = NULL;
static uint16_t ephemeral_port = 49152;

void tcp_init() { sockets = NULL; }

static uint32_t swap_uint32(uint32_t val) {
  return ((val << 24) & 0xFF000000) | ((val << 8) & 0x00FF0000) |
         ((val >> 8) & 0x0000FF00) | ((val >> 24) & 0x000000FF);
}

// Pseudo Header for Checksum
typedef struct {
  uint32_t src_ip;
  uint32_t dest_ip;
  uint8_t zeros;
  uint8_t proto;
  uint16_t tcp_len;
} __attribute__((packed)) tcp_pseudo_header_t;

static uint16_t tcp_checksum(void *packet, uint16_t len, uint32_t src_ip,
                             uint32_t dest_ip) {
  uint32_t sum = 0;

  // Pseudo Header
  tcp_pseudo_header_t ph;
  ph.src_ip = src_ip;
  ph.dest_ip = dest_ip;
  ph.zeros = 0;
  ph.proto = 6; // TCP
  ph.tcp_len = htons(len);

  uint16_t *ptr = (uint16_t *)&ph;
  for (int i = 0; i < sizeof(tcp_pseudo_header_t) / 2; i++) {
    sum += ptr[i];
  }

  // Payload
  ptr = (uint16_t *)packet;
  for (; len > 1; len -= 2)
    sum += *ptr++;
  if (len > 0)
    sum += *(uint8_t *)ptr;

  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return ~((uint16_t)sum);
}

// Helper to send TCP packet
// Precisamos de acesso ao netstack para enviar IP packet.
// Vou assumir que netstack.c exponha `ipv4_send` ou similar.
// Se não, vou precisar adicionar lá. Por enquanto, vou usar stub ou assumir que
// posso construir IP packet aqui. O ideal é modificar `netstack.c` para
// exportar função de envio IP. VOU COLOCAR COMMENTS PARA ISSO. POR ENQUANTO VOU
// DEFINIR UMA FUNCAO STUB AQUI.
extern void netstack_send_ipv4(uint32_t dest_ip, uint8_t proto, void *data,
                               uint16_t len);

static void send_tcp_packet(tcp_socket_t *socket, uint8_t flags,
                            const uint8_t *payload, uint32_t payload_len) {
  uint32_t packet_len = sizeof(tcp_header_t) + payload_len;
  uint8_t *packet = (uint8_t *)kmalloc(packet_len);
  memset(packet, 0, packet_len);

  tcp_header_t *hdr = (tcp_header_t *)packet;
  hdr->src_port = htons(socket->local_port);
  hdr->dest_port = htons(socket->remote_port);
  hdr->seq_num = htonl(socket->seq_num);
  hdr->ack_num = htonl(socket->ack_num);
  hdr->data_offset_reserved = (sizeof(tcp_header_t) / 4) << 4;
  hdr->flags = flags;
  hdr->window_size = htons(8192); // 8KB window

  if (payload && payload_len > 0) {
    memcpy(packet + sizeof(tcp_header_t), payload, payload_len);
  }

  hdr->checksum =
      tcp_checksum(packet, packet_len, socket->local_ip, socket->remote_ip);

  netstack_send_ipv4(socket->remote_ip, 6, packet, packet_len);
  kfree(packet);
}

tcp_socket_t *tcp_connect(uint32_t dest_ip, uint16_t dest_port) {
  tcp_socket_t *sock = (tcp_socket_t *)kmalloc(sizeof(tcp_socket_t));
  memset(sock, 0, sizeof(tcp_socket_t));

  sock->local_port = ephemeral_port++;
  sock->remote_port = dest_port;
  sock->remote_ip = dest_ip;
  // sock->local_ip = net_get_my_ip(); // Need this API
  sock->local_ip = 0x0F02000A; // 10.0.2.15 Hardcoded for now match netstack.c

  sock->seq_num = 12345; // TODO: Random
  sock->ack_num = 0;
  sock->state = TCP_CLOSED;

  sock->recv_buffer_size = 32768; // 32KB
  sock->recv_buffer = (uint8_t *)kmalloc(sock->recv_buffer_size);

  // Add to list
  sock->next = sockets;
  sockets = sock;

  // Handshake: Send SYN
  send_tcp_packet(sock, TCP_SYN, NULL, 0);
  sock->state = TCP_SYN_SENT;
  sock->seq_num++; // Consume sequence for SYN

  // Wait for Established (Blocking simple implementation for now)
  // Em um sistema real, seria async. Aqui vou fazer busy wait com yield.
  int timeout = 100000;
  while (sock->state != TCP_ESTABLISHED && timeout > 0) {
    // yield(); // If implementing multitasking properly
    timeout--;
    // Se receber SYN-ACK na interrupt, o handler muda o estado.
    // check timeout
  }

  if (sock->state != TCP_ESTABLISHED) {
    draw_string(10, 60, "TCP Connect Timeout", 0xFF0000);
    return NULL; // Failed
  }

  return sock;
}

void tcp_send(tcp_socket_t *socket, const uint8_t *data, uint32_t len) {
  if (socket->state != TCP_ESTABLISHED)
    return;

  // Send PSH+ACK
  send_tcp_packet(socket, TCP_PSH | TCP_ACK, data, len);
  socket->seq_num += len;
}

void tcp_close(tcp_socket_t *socket) {
  if (!socket)
    return;
  // Send FIN
  send_tcp_packet(socket, TCP_FIN | TCP_ACK, NULL, 0);
  socket->state = TCP_FIN_WAIT_1;
  socket->seq_num++;
}

// Blocking receive
int tcp_receive(tcp_socket_t *socket, uint8_t *buffer, uint32_t len) {
  if (!socket || socket->state == TCP_CLOSED)
    return -1;

  // Busy wait for data
  // TODO: Timeout
  while (socket->recv_read_ptr == socket->recv_write_ptr) {
    if (socket->state != TCP_ESTABLISHED && socket->state != TCP_CLOSE_WAIT) {
      // Connection lost or closed
      return 0;
    }
    // yield();
  }

  uint32_t available = socket->recv_write_ptr - socket->recv_read_ptr;
  uint32_t to_read = (len < available) ? len : available;

  memcpy(buffer, socket->recv_buffer + socket->recv_read_ptr, to_read);
  socket->recv_read_ptr += to_read;

  // Simplification: Reset buffer if empty to prevent overflow of ptr (assuming
  // linear buffer)
  if (socket->recv_read_ptr == socket->recv_write_ptr) {
    socket->recv_read_ptr = 0;
    socket->recv_write_ptr = 0;
  }

  return to_read;
}

// Handler chamado quando chega pacote IP pro protocolo 6
void tcp_handle_packet(void *packet, uint16_t len, uint32_t src_ip,
                       uint32_t dest_ip) {
  tcp_header_t *hdr = (tcp_header_t *)packet;
  uint16_t data_offset = (hdr->data_offset_reserved >> 4) * 4;
  uint8_t *payload = (uint8_t *)packet + data_offset;
  uint32_t payload_len = len - data_offset;

  uint16_t dest_port = htons(hdr->dest_port);

  // Find socket
  tcp_socket_t *sock = sockets;
  while (sock) {
    if (sock->local_port == dest_port && sock->remote_ip == src_ip)
      break;
    sock = sock->next;
  }

  if (!sock)
    return; // Port closed or not ours

  uint32_t seq = htonl(hdr->seq_num);
  uint32_t ack = htonl(hdr->ack_num);
  uint8_t flags = hdr->flags;

  // State machine minimal
  if (sock->state == TCP_SYN_SENT) {
    if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
      sock->ack_num = seq + 1;
      // sock->seq_num was incremented at send SYN
      sock->state = TCP_ESTABLISHED;

      // Send ACK
      send_tcp_packet(sock, TCP_ACK, NULL, 0);
    }
  } else if (sock->state == TCP_ESTABLISHED) {
    if (flags & TCP_ACK) {
      // Good
    }
    if (payload_len > 0) {
      // Data received
      // Copy to buffer
      uint32_t space =
          sock->recv_buffer_size - (sock->recv_write_ptr - sock->recv_read_ptr);
      if (space >= payload_len) {
        // Circular logic omitted for linear simplicity, assume infinite buffer
        // reset logic elsewhere or just linear
        memcpy(sock->recv_buffer + sock->recv_write_ptr, payload, payload_len);
        sock->recv_write_ptr += payload_len;

        sock->ack_num += payload_len;
        send_tcp_packet(sock, TCP_ACK, NULL, 0); // ACK data
      }
    }
    if (flags & TCP_FIN) {
      sock->ack_num++;
      send_tcp_packet(sock, TCP_ACK, NULL, 0);
      sock->state = TCP_CLOSE_WAIT;
      // Should verify if user also wants to close.
    }
  } else if (sock->state == TCP_FIN_WAIT_1) {
    if ((flags & TCP_FIN) && (flags & TCP_ACK)) {
      sock->ack_num++;
      send_tcp_packet(sock, TCP_ACK, NULL, 0);
      sock->state = TCP_TIME_WAIT;
    } else if (flags & TCP_ACK) {
      sock->state = TCP_FIN_WAIT_2;
    }
  }
}
