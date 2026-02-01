#include "net.h"
#include "string.h"
#include <stdint.h>

#define htons(n)                                                               \
  (((((unsigned short)(n) & 0xFF)) << 8) |                                     \
   (((unsigned short)(n) & 0xFF00) >> 8))
#define htonl(n)                                                               \
  (((((unsigned long)(n) & 0xFF)) << 24) |                                     \
   ((((unsigned long)(n) & 0xFF00)) << 8) |                                    \
   ((((unsigned long)(n) & 0xFF0000)) >> 8) |                                  \
   ((((unsigned long)(n) & 0xFF000000)) >> 24))

typedef struct {
  uint8_t dest[6];
  uint8_t src[6];
  uint16_t type;
} __attribute__((packed)) ethernet_frame_t;

typedef struct {
  uint16_t hw_type;
  uint16_t proto_type;
  uint8_t hw_len;
  uint8_t proto_len;
  uint16_t opcode;
  uint8_t src_mac[6];
  uint32_t src_ip;
  uint8_t dest_mac[6];
  uint32_t dest_ip;
} __attribute__((packed)) arp_packet_t;

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
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint16_t id;
  uint16_t seq;
} __attribute__((packed)) icmp_packet_t;

static uint32_t my_ip = 0x0F02000A; // 10.0.2.15
volatile int ping_received = 0;
volatile uint32_t last_ping_ip = 0;

uint16_t net_checksum(void *data, int len) {
  uint32_t sum = 0;
  uint16_t *ptr = (uint16_t *)data;
  for (; len > 1; len -= 2)
    sum += *ptr++;
  if (len > 0)
    sum += *(uint8_t *)ptr;
  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return ~((uint16_t)sum);
}

uint32_t net_resolve_host(const char *host) {
  // Check if it's a numeric IP (e.g., 10.0.2.2)
  if (host[0] >= '0' && host[0] <= '9') {
    uint32_t ip = 0;
    int shift = 0;
    int octet = 0;
    for (int i = 0; host[i]; i++) {
      if (host[i] == '.' || host[i] == ':') {
        ip |= (octet << (shift * 8));
        shift++;
        octet = 0;
        if (host[i] == ':')
          break; // Port separator
      } else if (host[i] >= '0' && host[i] <= '9') {
        octet = octet * 10 + (host[i] - '0');
      }
    }
    if (shift < 4)
      ip |= (octet << (shift * 8));
    return ip;
  }

  // Static DNS table
  if (strstr(host, "google.com"))
    return 0x08080808; // 8.8.8.8
  if (strstr(host, "youtube.com"))
    return 0xEE9941D0; // 208.65.153.238
  if (strstr(host, "liwus.com"))
    return 0x01010101; // 1.1.1.1
  if (strstr(host, "httpbin.org"))
    return 0x223C8954; // 34.201.60.147
  if (strstr(host, "example.com"))
    return 0x5DB8D822; // 93.184.216.34
  if (strstr(host, "neverssl.com"))
    return 0x22C63C8E; // 34.198.60.142

  // Fallback hash
  uint32_t hash = 5381;
  while (*host)
    hash = ((hash << 5) + hash) + *host++;
  return htonl(hash);
}

void netstack_send_ping(uint32_t dest_ip) {
  uint8_t packet[sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t) +
                 sizeof(icmp_packet_t)];
  memset(packet, 0, sizeof(packet));

  ethernet_frame_t *eth = (ethernet_frame_t *)packet;
  ipv4_packet_t *ip = (ipv4_packet_t *)(packet + sizeof(ethernet_frame_t));
  icmp_packet_t *icmp = (icmp_packet_t *)(packet + sizeof(ethernet_frame_t) +
                                          sizeof(ipv4_packet_t));

  net_interface_t *netif = net_get_list();
  memset(eth->dest, 0xFF, 6);
  memcpy(eth->src, netif->mac, 6);
  eth->type = htons(0x0800);

  ip->version_ihl = 0x45;
  ip->len = htons(sizeof(ipv4_packet_t) + sizeof(icmp_packet_t));
  ip->ttl = 64;
  ip->proto = 1;
  ip->src_ip = my_ip;
  ip->dest_ip = dest_ip;
  ip->checksum = net_checksum(ip, 20);

  icmp->type = 8;
  icmp->checksum = net_checksum(icmp, sizeof(icmp_packet_t));

  netif->send_packet(netif, packet, sizeof(packet));
}

// Exported helper
void netstack_send_ipv4(uint32_t dest_ip, uint8_t proto, void *data,
                        uint16_t len) {
  uint8_t packet[sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t) +
                 1500]; // Max buffer
  if (len > 1400)
    return; // Fragment not supported

  ethernet_frame_t *eth = (ethernet_frame_t *)packet;
  ipv4_packet_t *ip = (ipv4_packet_t *)(packet + sizeof(ethernet_frame_t));

  net_interface_t *netif = net_get_list();
  memset(eth->dest, 0xFF, 6); // Broadcast mac? No, need ARP resolution.
  // For simplicity, using Broadcast MAC or cached/hardcoded Gateway MAC if
  // possible. Assuming gateway on QEMU 10.0.2.2 usually is 52:54:00:12:35:02
  // But sending to FF is safer if switch handles it, though routers drop
  // broadcast IP.
  // TODO: ARP Table lookup needed for real internet.
  // Hardcoding Gateway MAC for QEMU (52:54:00:12:35:02)
  eth->dest[0] = 0x52;
  eth->dest[1] = 0x54;
  eth->dest[2] = 0x00;
  eth->dest[3] = 0x12;
  eth->dest[4] = 0x35;
  eth->dest[5] = 0x02;

  memcpy(eth->src, netif->mac, 6);
  eth->type = htons(0x0800);

  ip->version_ihl = 0x45;
  ip->len = htons(sizeof(ipv4_packet_t) + len);
  ip->ttl = 64;
  ip->proto = proto;
  ip->src_ip = my_ip;
  ip->dest_ip = dest_ip;

  ip->checksum = 0;
  ip->checksum = net_checksum(ip, 20);

  memcpy(packet + sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t), data, len);

  netif->send_packet(netif, packet,
                     sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t) + len);
}

extern void tcp_handle_packet(void *packet, uint16_t len, uint32_t src_ip,
                              uint32_t dest_ip);

void netstack_handle_packet(void *data, uint16_t len) {
  ethernet_frame_t *eth = (ethernet_frame_t *)data;
  uint16_t type = htons(eth->type);

  if (type == 0x0806) { // ARP
    arp_packet_t *arp =
        (arp_packet_t *)((uintptr_t)data + sizeof(ethernet_frame_t));
    if (htons(arp->opcode) == 0x0001 && arp->dest_ip == my_ip) { // ARP Request
      arp->opcode = htons(0x0002);                               // Reply
      memcpy(arp->dest_mac, arp->src_mac, 6);
      arp->dest_ip = arp->src_ip;

      net_interface_t *netif = net_get_list();
      memcpy(arp->src_mac, netif->mac, 6);
      arp->src_ip = my_ip;

      memcpy(eth->dest, eth->src, 6);
      memcpy(eth->src, netif->mac, 6);

      netif->send_packet(netif, data, len);
    }
  } else if (type == 0x0800) { // IPv4
    ipv4_packet_t *ip =
        (ipv4_packet_t *)((uintptr_t)data + sizeof(ethernet_frame_t));
    uint16_t ip_len = htons(ip->len);

    if (ip->proto == 1) { // ICMP
      icmp_packet_t *icmp =
          (icmp_packet_t *)((uintptr_t)data + sizeof(ethernet_frame_t) + 20);
      if (icmp->type == 8) { // Echo Request
        icmp->type = 0;      // Echo Reply
        icmp->checksum = 0;
        icmp->checksum =
            net_checksum(icmp, len - sizeof(ethernet_frame_t) - 20);

        uint32_t tmp_ip = ip->src_ip;
        ip->src_ip = ip->dest_ip;
        ip->dest_ip = tmp_ip;
        ip->checksum = 0;
        ip->checksum = net_checksum(ip, 20);

        net_interface_t *netif = net_get_list();
        memcpy(eth->dest, eth->src, 6);
        memcpy(eth->src, netif->mac, 6);

        netif->send_packet(netif, data, len);
      } else if (icmp->type == 0) { // Echo Reply
        last_ping_ip = ip->src_ip;
        ping_received = 1;
      }
    } else if (ip->proto == 6) { // TCP
      void *tcp_segment = (void *)((uintptr_t)data + sizeof(ethernet_frame_t) +
                                   sizeof(ipv4_packet_t));
      uint16_t tcp_len = ip_len - sizeof(ipv4_packet_t);
      tcp_handle_packet(tcp_segment, tcp_len, ip->src_ip, ip->dest_ip);
    }
  }
}
