#include "tcp.h"
#include "kheap.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "task.h"

extern char *itoa(int value, char *str, int base);

static tcp_socket_t *sockets = NULL;
static uint16_t ephemeral_port = 49152;

void tcp_init() { sockets = NULL; }

static void tcp_log(const char *msg) { serial_print(msg); }

static void tcp_log_u32(uint32_t value) {
  char buf[16];
  itoa(value, buf, 10);
  serial_print(buf);
}

static void tcp_log_ip(uint32_t ip) {
  char buf[16];
  itoa(ip & 0xFF, buf, 10); serial_print(buf); serial_print(".");
  itoa((ip >> 8) & 0xFF, buf, 10); serial_print(buf); serial_print(".");
  itoa((ip >> 16) & 0xFF, buf, 10); serial_print(buf); serial_print(".");
  itoa((ip >> 24) & 0xFF, buf, 10); serial_print(buf);
}

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
  uint8_t ph[12];

  // Pseudo-header
  memcpy(&ph[0], &src_ip, 4);
  memcpy(&ph[4], &dest_ip, 4);
  ph[8] = 0;
  ph[9] = 6; // TCP
  ph[10] = (len >> 8) & 0xFF;
  ph[11] = (len >> 0) & 0xFF;

  // Soma Pseudo-header
  for (int i = 0; i < 12; i += 2) {
    sum += (uint16_t)((ph[i] << 8) | ph[i + 1]);
  }

  // Soma Payload
  uint8_t *ptr = (uint8_t *)packet;
  int left = len;
  while (left > 1) {
    sum += (uint16_t)((ptr[0] << 8) | ptr[1]);
    ptr += 2;
    left -= 2;
  }
  if (left > 0) {
    sum += (uint16_t)(ptr[0] << 8);
  }

  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }

  return htons(~((uint16_t)sum));
}

static void send_tcp_packet(tcp_socket_t *socket, uint8_t flags,
                            const uint8_t *payload, uint32_t payload_len,
                            uint32_t seq) {
  uint32_t packet_len = sizeof(tcp_header_t) + payload_len;
  uint8_t *packet = (uint8_t *)kmalloc(packet_len);
  memset(packet, 0, packet_len);

  tcp_header_t *hdr = (tcp_header_t *)packet;
  hdr->src_port = htons(socket->local_port);
  hdr->dest_port = htons(socket->remote_port);
  hdr->seq_num = htonl(seq);
  hdr->ack_num = htonl(socket->ack_num);
  hdr->data_offset_reserved = (sizeof(tcp_header_t) / 4) << 4;
  hdr->flags = flags;
  hdr->window_size = htons(8192);

  if (payload && payload_len > 0) {
    memcpy(packet + sizeof(tcp_header_t), payload, payload_len);
  }

  hdr->checksum = 0;
  hdr->checksum = tcp_checksum(packet, packet_len, socket->local_ip,
                                socket->remote_ip);

  tcp_log("[tcp] send flags=");
  tcp_log_u32(flags);
  tcp_log(" seq=");
  tcp_log_u32(socket->seq_num);
  tcp_log(" ack=");
  tcp_log_u32(socket->ack_num);
  tcp_log(" len=");
  tcp_log_u32(payload_len);
  tcp_log(" dst=");
  tcp_log_ip(socket->remote_ip);
  tcp_log(":");
  tcp_log_u32(socket->remote_port);
  tcp_log("\n");

  netstack_send_ipv4(socket->remote_ip, 6, packet, packet_len);
  kfree(packet);
}

tcp_socket_t *tcp_connect(uint32_t dest_ip, uint16_t dest_port) {
  tcp_socket_t *sock = (tcp_socket_t *)kmalloc(sizeof(tcp_socket_t));
  memset(sock, 0, sizeof(tcp_socket_t));

  sock->local_port = ephemeral_port++;
  sock->remote_port = dest_port;
  sock->remote_ip = dest_ip;
  sock->local_ip = netstack_get_my_ip();

  // Usa ticks do timer para um número de sequência menos previsível
  sock->seq_num = timer_ticks * 1000 + (ephemeral_port & 0xFFF);
  sock->ack_num = 0;
  sock->state = TCP_CLOSED;

  sock->recv_buffer_size = 32768; // 32KB
  sock->recv_buffer = (uint8_t *)kmalloc(sock->recv_buffer_size);

  tcp_log("[tcp] connect local_port=");
  tcp_log_u32(sock->local_port);
  tcp_log(" remote=");
  tcp_log_ip(dest_ip);
  tcp_log(":");
  tcp_log_u32(dest_port);
  tcp_log("\n");

  // Add to list
  sock->next = sockets;
  sockets = sock;

  // Handshake: Send SYN
  uint32_t syn_seq = sock->seq_num;
  sock->seq_num++; // Consume sequence number for SYN
  send_tcp_packet(sock, TCP_SYN, NULL, 0, syn_seq);
  sock->state = TCP_SYN_SENT;

  // Wait for Established (Async using scheduler). The QEMU/slirp path on
  // this host can delay SYN/SYN-ACK by tens of seconds, so the SYN is
  // retransmitted periodically while we wait.
  {
    uint32_t start = timer_ticks;
    int last_retrans = -99999;
    while (sock->state != TCP_ESTABLISHED && (timer_ticks - start) < 9000) {
      if (sock->state == TCP_CLOSED) break; // RST or other error
      if (sock->state == TCP_SYN_SENT &&
          (int)(timer_ticks - start) - last_retrans >= 500) {
        last_retrans = (int)(timer_ticks - start);
        send_tcp_packet(sock, TCP_SYN, NULL, 0, sock->seq_num - 1);
      }
      switch_task();
    }
    if (sock->state != TCP_ESTABLISHED) {
      tcp_log("[tcp] connect timeout state=");
      tcp_log_u32((uint32_t)sock->state);
      tcp_log(" waited_ticks=");
      tcp_log_u32((uint32_t)(timer_ticks - start));
      tcp_log("\n");
    }
  }

  if (sock->state != TCP_ESTABLISHED) {
    /* Remove the socket from the list so a late SYN-ACK cannot revive it. */
    tcp_socket_t **pp = &sockets;
    while (*pp && *pp != sock) pp = &(*pp)->next;
    if (*pp) *pp = sock->next;
    if (sock->recv_buffer) kfree(sock->recv_buffer);
    kfree(sock);
    return NULL; // Failed
  }

  tcp_log("[tcp] connect established\n");

  return sock;
}

uint32_t tcp_send(tcp_socket_t *socket, const uint8_t *data, uint32_t len) {
  if (socket->state != TCP_ESTABLISHED) {
    tcp_log("[tcp] send ignored, socket not established\n");
    return 0;
  }

  // Reserve the sequence range BEFORE it can reach the wire: o ACK do peer
  // que chegar depois (ack == seq+len) nao deve reajustar seq_num para frente.
  uint32_t seq = socket->seq_num;
  socket->seq_num += len;

  // Send PSH+ACK
  send_tcp_packet(socket, TCP_PSH | TCP_ACK, data, len, seq);
  return seq;
}

void tcp_resend(tcp_socket_t *socket, uint32_t seq, const uint8_t *data,
                uint32_t len) {
  if (socket->state != TCP_ESTABLISHED && socket->state != TCP_FIN_WAIT_1) {
    tcp_log("[tcp] resend ignored, socket not established\n");
    return;
  }
  send_tcp_packet(socket, TCP_PSH | TCP_ACK, data, len, seq);
}

void tcp_close(tcp_socket_t *socket) {
  if (!socket)
    return;
  // Send FIN
  uint32_t fin_seq = socket->seq_num;
  socket->seq_num++; // FIN consome 1 byte de sequencia
  send_tcp_packet(socket, TCP_FIN | TCP_ACK, NULL, 0, fin_seq);
  socket->state = TCP_FIN_WAIT_1;
}

// Non-blocking receive
int tcp_receive(tcp_socket_t *socket, uint8_t *buffer, uint32_t len) {
  if (!socket || (socket->state == TCP_CLOSED && socket->recv_read_ptr == socket->recv_write_ptr))
    return -1;

  // DEBUG
  serial_print("[TCP_RX] sock=");
  {
      char num[16];
      itoa((int)(intptr_t)socket, num, 16);
      serial_print(num);
  }
  serial_print(" rptr=");
  {
      char num[16];
      itoa(socket->recv_read_ptr, num, 10);
      serial_print(num);
  }
  serial_print(" wptr=");
  {
      char num[16];
      itoa(socket->recv_write_ptr, num, 10);
      serial_print(num);
  }
  serial_print(" state=");
  {
      char num[16];
      itoa(socket->state, num, 10);
      serial_print(num);
  }
  serial_print(" len=");
  {
      char num[16];
      itoa(len, num, 10);
      serial_print(num);
  }
  serial_print("\n");

  if (socket->recv_read_ptr == socket->recv_write_ptr) {
    serial_print("[TCP_RX] empty, ret 0\n");
    return 0;
  }

  uint32_t available = socket->recv_write_ptr - socket->recv_read_ptr;
  uint32_t to_read = (len < available) ? len : available;

  memcpy(buffer, socket->recv_buffer + socket->recv_read_ptr, to_read);
  socket->recv_read_ptr += to_read;

  if (socket->recv_read_ptr == socket->recv_write_ptr) {
    socket->recv_read_ptr = 0;
    socket->recv_write_ptr = 0;
  }

  return (int)to_read;
}

tcp_socket_t *tcp_listen(uint16_t port) {
  tcp_socket_t *sock = (tcp_socket_t *)kmalloc(sizeof(tcp_socket_t));
  memset(sock, 0, sizeof(tcp_socket_t));

  sock->local_port = port;
  sock->state = TCP_LISTEN;
  sock->recv_buffer_size = 32768;
  sock->recv_buffer = (uint8_t *)kmalloc(sock->recv_buffer_size);

  sock->next = sockets;
  sockets = sock;

  tcp_log("[tcp] listening on port ");
  tcp_log_u32(port);
  tcp_log("\n");

  return sock;
}

// Retorna o proximo socket ESTABLISHED aceito na porta (nao bloqueante).
// O listener criado por tcp_listen permanece em LISTEN.
tcp_socket_t *tcp_accept(uint16_t port) {
  tcp_socket_t *sock = sockets;
  while (sock) {
    if (sock->local_port == port && sock->state == TCP_ESTABLISHED &&
        sock->remote_ip != 0) {
      return sock;
    }
    sock = sock->next;
  }
  return NULL;
}

// Handler chamado quando chega pacote IP pro protocolo 6
void tcp_handle_packet(void *packet, uint16_t len, uint32_t src_ip,
                       uint32_t dest_ip) {
  tcp_header_t *hdr = (tcp_header_t *)packet;
  uint16_t data_offset = (hdr->data_offset_reserved >> 4) * 4;
  uint8_t *payload = (uint8_t *)packet + data_offset;
  uint32_t payload_len = len - data_offset;

  uint16_t dest_port = htons(hdr->dest_port);
  uint16_t src_port = htons(hdr->src_port);

  // DEBUG all packets
  serial_print("[TCP_PKT] src=");
  {
      char num[16];
      itoa(src_ip, num, 10); serial_print(num);
  }
  serial_print(":");
  {
      char num[16];
      itoa(src_port, num, 10); serial_print(num);
  }
  serial_print(" dst=");
  {
      char num[16];
      itoa(dest_ip, num, 10); serial_print(num);
  }
  serial_print(":");
  {
      char num[16];
      itoa(dest_port, num, 10); serial_print(num);
  }
  serial_print(" flags=");
  {
      char num[16];
      itoa(hdr->flags, num, 16); serial_print(num);
  }
  serial_print(" payload=");
  {
      char num[16];
      itoa(payload_len, num, 10); serial_print(num);
  }
  serial_print("\n");

  // Find socket
  tcp_socket_t *sock = sockets;
  tcp_socket_t *listen_sock = NULL;

  while (sock) {
    if (sock->local_port == dest_port) {
      if (sock->state == TCP_LISTEN) {
        listen_sock = sock;
      } else if (sock->remote_ip == src_ip && sock->remote_port == src_port) {
        break; // Match exato
      }
    }
    sock = sock->next;
  }

  // Se nao achou match exato mas achou um listener, cria socket filho
  if (!sock && listen_sock && (hdr->flags & TCP_SYN)) {
    tcp_socket_t *child = (tcp_socket_t *)kmalloc(sizeof(tcp_socket_t));
    if (!child) return;
    memset(child, 0, sizeof(tcp_socket_t));
    child->local_port = dest_port;
    child->remote_port = src_port;
    child->remote_ip = src_ip;
    child->local_ip = dest_ip;
    child->seq_num = 1000; // ISN
    child->state = TCP_LISTEN; // a maquina de estados abaixo responde o SYN
    child->recv_buffer_size = 32768;
    child->recv_buffer = (uint8_t *)kmalloc(child->recv_buffer_size);
    child->next = sockets;
    sockets = child;
    sock = child;
  }

  if (!sock) {
    return; // Port closed or not ours
  }

  uint32_t seq = ntohl(hdr->seq_num);
  uint32_t ack = ntohl(hdr->ack_num);
  uint8_t flags = hdr->flags;

  // State machine minimal
  if (sock->state == TCP_LISTEN) {
    if (flags & TCP_SYN) {
      sock->ack_num = seq + 1;
      sock->state = TCP_SYN_RECEIVED;
      uint32_t synack_seq = sock->seq_num;
      sock->seq_num++; // SYN-ACK consome 1 byte de sequencia
      send_tcp_packet(sock, TCP_SYN | TCP_ACK, NULL, 0, synack_seq);
      tcp_log("[tcp] connection request -> SYN_RECEIVED\n");
    }
  } else if (sock->state == TCP_SYN_RECEIVED) {
    if (flags & TCP_ACK) {
      sock->state = TCP_ESTABLISHED;
      sock->seq_num = ack;
      tcp_log("[tcp] connection established (passive)\n");
    }
  } else if (sock->state == TCP_SYN_SENT) {
    if ((flags & TCP_SYN) && (flags & TCP_ACK)) {
      tcp_log("[tcp] rx SYNACK, transitioning\n");
      sock->ack_num = seq + 1;
      sock->seq_num = ack; // Sync sequence with server's ACK
      sock->state = TCP_ESTABLISHED;
      tcp_log("[tcp] state -> ESTABLISHED\n");

      // Send ACK
      send_tcp_packet(sock, TCP_ACK, NULL, 0, sock->seq_num);
    } else if (flags & TCP_RST) {
      sock->state = TCP_CLOSED;
      tcp_log("[tcp] state -> CLOSED (RST received)\n");
    }
  } else if (sock->state == TCP_ESTABLISHED) {
    serial_print("[TCP_EST] sock=");
    {
        char num[16];
        itoa((int)(intptr_t)sock, num, 16);
        serial_print(num);
    }
    serial_print(" flags=");
    {
        char num[16];
        itoa(flags, num, 16);
        serial_print(num);
    }
    serial_print(" payload=");
    {
        char num[16];
        itoa(payload_len, num, 10);
        serial_print(num);
    }
    serial_print("\n");
    if (flags & TCP_ACK) {
       if (ack > sock->seq_num) {
         sock->seq_num = ack;
       }
       if (ack > sock->ack_received) {
         sock->ack_received = ack;
       }
    }
    if (payload_len > 0) {
      // Data received
      uint32_t current_usage = sock->recv_write_ptr - sock->recv_read_ptr;
      uint32_t space = sock->recv_buffer_size - sock->recv_write_ptr;

      serial_print("[TCP_DATA] sock=");
      {
          char num[16];
          itoa((int)(intptr_t)sock, num, 16);
          serial_print(num);
      }
      serial_print(" payload=");
      {
          char num[16];
          itoa(payload_len, num, 10);
          serial_print(num);
      }
      serial_print(" space=");
      {
          char num[16];
          itoa(space, num, 10);
          serial_print(num);
      }
      serial_print(" wptr=");
      {
          char num[16];
          itoa(sock->recv_write_ptr, num, 10);
          serial_print(num);
      }
      serial_print("\n");

      if (space >= payload_len) {
        memcpy(sock->recv_buffer + sock->recv_write_ptr, payload, payload_len);
        sock->recv_write_ptr += payload_len;
        sock->ack_num += payload_len;
        send_tcp_packet(sock, TCP_ACK, NULL, 0, sock->seq_num); // ACK data
      } else {
        // Sem espaco no fim? Se o buffer estiver vazio, resetamos os ponteiros
        if (current_usage == 0) {
            sock->recv_write_ptr = 0;
            sock->recv_read_ptr = 0;
            // Tenta de novo
            memcpy(sock->recv_buffer, payload, payload_len);
            sock->recv_write_ptr = payload_len;
            sock->ack_num += payload_len;
            send_tcp_packet(sock, TCP_ACK, NULL, 0, sock->seq_num);
        } else {
            // Buffer realmente cheio, dropa pacote para salvar o kernel
            // serial_print("[tcp] buffer overflow prevented!\n");
        }
      }
    }
    if (flags & TCP_FIN) {
      sock->ack_num++;
      send_tcp_packet(sock, TCP_ACK, NULL, 0, sock->seq_num);
      sock->state = TCP_CLOSE_WAIT;
      tcp_log("[tcp] state -> CLOSE_WAIT\n");
      // Should verify if user also wants to close.
    }
  } else if (sock->state == TCP_FIN_WAIT_1) {
    if ((flags & TCP_FIN) && (flags & TCP_ACK)) {
      sock->ack_num++;
      send_tcp_packet(sock, TCP_ACK, NULL, 0, sock->seq_num);
      sock->state = TCP_TIME_WAIT;
      tcp_log("[tcp] state -> TIME_WAIT\n");
    } else if (flags & TCP_FIN) {
      sock->ack_num++;
      send_tcp_packet(sock, TCP_ACK, NULL, 0, sock->seq_num);
      sock->state = TCP_TIME_WAIT;
      tcp_log("[tcp] state -> TIME_WAIT (fin-only)\n");
    } else if (flags & TCP_ACK) {
      sock->state = TCP_FIN_WAIT_2;
      tcp_log("[tcp] state -> FIN_WAIT_2\n");
    }
  } else if (sock->state == TCP_FIN_WAIT_2) {
    if (flags & TCP_FIN) {
      sock->ack_num++;
      send_tcp_packet(sock, TCP_ACK, NULL, 0, sock->seq_num);
      sock->state = TCP_TIME_WAIT;
      tcp_log("[tcp] state -> TIME_WAIT (from FIN_WAIT_2)\n");
    }
  } else if (sock->state == TCP_LAST_ACK) {
    if (flags & TCP_ACK) {
      sock->state = TCP_CLOSED;
      tcp_log("[tcp] state -> CLOSED (last ack)\n");
    }
  } else if (sock->state == TCP_TIME_WAIT) {
    if (flags & TCP_SYN) {
      // 4-tupla morta reciclada - sera tratado no ramo acima
    }
  }
}
