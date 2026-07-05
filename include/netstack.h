#ifndef NETSTACK_H
#define NETSTACK_H

#include <stdint.h>

#define htons(n)                                                               \
  (((((unsigned short)(n) & 0xFF)) << 8) |                                     \
   (((unsigned short)(n) & 0xFF00) >> 8))
#define htonl(n)                                                               \
  (((((unsigned long)(n) & 0xFF)) << 24) |                                     \
   ((((unsigned long)(n) & 0xFF00)) << 8) |                                    \
   ((((unsigned long)(n) & 0xFF0000)) >> 8) |                                  \
   ((((unsigned long)(n) & 0xFF000000)) >> 24))
#define ntohs(n) htons(n)
#define ntohl(n) htonl(n)

typedef struct {
  uint8_t version_ihl;
  uint8_t tos;
  uint16_t len;
  uint16_t id;
  uint16_t flags_offset;
  uint8_t ttl;
  uint8_t proto;
  uint16_t checksum;
  uint32_t src_ip;
  uint32_t dest_ip;
} __attribute__((packed)) ipv4_packet_t;

typedef struct {
  uint8_t dest[6];
  uint8_t src[6];
  uint16_t type;
} __attribute__((packed)) ethernet_frame_t;

uint16_t net_checksum(void *data, int len);
void netstack_send_ipv4(uint32_t dest_ip, uint8_t proto, void *data,
                        uint16_t len);
uint32_t net_resolve_host(const char *host);
void netstack_send_ping(uint32_t dest_ip);
int netstack_ping(uint32_t dest_ip, uint32_t timeout_ticks);
uint32_t netstack_get_my_ip(void);

#endif
