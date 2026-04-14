#include "net.h"
#include "serial.h"
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
static uint32_t gateway_ip = 0x0202000A; // 10.0.2.2
volatile int ping_received = 0;
volatile uint32_t last_ping_ip = 0;
static volatile int arp_received = 0;
static volatile uint32_t last_arp_ip = 0;
static uint8_t last_arp_mac[6];

#define IPV4_U32(a, b, c, d)                                                  \
  ((uint32_t)((a) & 0xFF) | ((uint32_t)((b) & 0xFF) << 8) |                   \
   ((uint32_t)((c) & 0xFF) << 16) | ((uint32_t)((d) & 0xFF) << 24))

static void netstack_send_arp_request(uint32_t dest_ip);
static int netstack_resolve_mac(uint32_t dest_ip, uint8_t out_mac[6]);
static uint32_t netstack_next_hop(uint32_t dest_ip);

static void net_log(const char *msg) { (void)msg; }

static void net_log_u32(uint32_t value) { (void)value; }

static void net_log_ip(uint32_t ip) { (void)ip; }

static void net_log_mac(const uint8_t mac[6]) { (void)mac; }

uint16_t net_checksum(void *data, int len) {
  uint32_t sum = 0;
  uint8_t *ptr = (uint8_t *)data;
  while (len > 1) {
    sum += (uint16_t)(((uint16_t)ptr[0] << 8) | ptr[1]);
    ptr += 2;
    len -= 2;
  }
  if (len > 0)
    sum += (uint16_t)((uint16_t)ptr[0] << 8);
  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return ~((uint16_t)sum);
}

uint32_t net_resolve_host(const char *host) {
  net_log("[net] resolve host ");
  net_log(host);
  net_log("\n");
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
    net_log("[net] numeric ip -> ");
    net_log_ip(ip);
    net_log("\n");
    return ip;
  }

  // Call real DNS resolver
  extern uint32_t dns_resolve(const char *hostname);
  uint32_t dns_ip = dns_resolve(host);
  if (dns_ip != 0) {
    return dns_ip;
  }

  net_log("[net] dns resolution failed\n");
  return 0;
}

void netstack_send_ping(uint32_t dest_ip) {
  uint8_t packet[sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t) +
                 sizeof(icmp_packet_t)];
  uint8_t dest_mac[6];
  memset(packet, 0, sizeof(packet));

  ethernet_frame_t *eth = (ethernet_frame_t *)packet;
  ipv4_packet_t *ip = (ipv4_packet_t *)(packet + sizeof(ethernet_frame_t));
  icmp_packet_t *icmp = (icmp_packet_t *)(packet + sizeof(ethernet_frame_t) +
                                          sizeof(ipv4_packet_t));

  net_interface_t *netif = net_get_list();
  if (!netif || !netif->send_packet) {
    return;
  }
  if (netstack_resolve_mac(dest_ip, dest_mac) != 0) {
    return;
  }
  memcpy(eth->dest, dest_mac, 6);
  memcpy(eth->src, netif->mac, 6);
  eth->type = htons(0x0800);

  ip->version_ihl = 0x45;
  ip->len = htons(sizeof(ipv4_packet_t) + sizeof(icmp_packet_t));
  ip->ttl = 64;
  ip->proto = 1;
  ip->src_ip = my_ip;
  ip->dest_ip = dest_ip;
  ip->checksum = htons(net_checksum(ip, 20));

  icmp->type = 8;
  icmp->checksum = htons(net_checksum(icmp, sizeof(icmp_packet_t)));

  netif->send_packet(netif, packet, sizeof(packet));
}

int netstack_ping(uint32_t dest_ip, uint32_t timeout_ticks) {
  extern volatile uint32_t timer_ticks;
  uint32_t start = timer_ticks;

  ping_received = 0;
  last_ping_ip = 0;
  netstack_send_ping(dest_ip);

  while ((timer_ticks - start) < timeout_ticks) {
    if (ping_received && last_ping_ip == dest_ip) {
      ping_received = 0;
      return (int)(timer_ticks - start);
    }
    asm volatile("hlt");
  }

  return -1;
}

uint32_t netstack_get_my_ip(void) { return my_ip; }

static uint32_t netstack_next_hop(uint32_t dest_ip) {
  if ((dest_ip & 0x00FFFFFFU) == (my_ip & 0x00FFFFFFU)) {
    return dest_ip;
  }
  return gateway_ip;
}

// Exported helper
void netstack_send_ipv4(uint32_t dest_ip, uint8_t proto, void *data,
                        uint16_t len) {
  uint8_t packet[sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t) +
                 1500]; // Max buffer
  uint8_t dest_mac[6];
  if (len > 1400)
    return; // Fragment not supported

  ethernet_frame_t *eth = (ethernet_frame_t *)packet;
  ipv4_packet_t *ip = (ipv4_packet_t *)(packet + sizeof(ethernet_frame_t));

  net_interface_t *netif = net_get_list();
  if (!netif || !netif->send_packet) {
    net_log("[net] ipv4 send failed: no interface\n");
    return;
  }
  if (netstack_resolve_mac(dest_ip, dest_mac) != 0) {
    net_log("[net] ipv4 send failed: no mac\n");
    return;
  }
  memcpy(eth->dest, dest_mac, 6);

  memcpy(eth->src, netif->mac, 6);
  eth->type = htons(0x0800);

  ip->version_ihl = 0x45;
  ip->len = htons(sizeof(ipv4_packet_t) + len);
  ip->ttl = 64;
  ip->proto = proto;
  ip->src_ip = my_ip;
  ip->dest_ip = dest_ip;

  ip->checksum = 0;
  ip->checksum = htons(net_checksum(ip, 20));

  net_log("[net] ipv4 send proto=");
  net_log_u32(proto);
  net_log(" len=");
  net_log_u32(len);
  net_log(" dst=");
  net_log_ip(dest_ip);
  net_log("\n");

  memcpy(packet + sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t), data, len);

  netif->send_packet(netif, packet,
                     sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t) + len);
}

static void netstack_send_arp_request(uint32_t dest_ip) {
  uint8_t packet[sizeof(ethernet_frame_t) + sizeof(arp_packet_t)];
  ethernet_frame_t *eth = (ethernet_frame_t *)packet;
  arp_packet_t *arp = (arp_packet_t *)(packet + sizeof(ethernet_frame_t));
  net_interface_t *netif = net_get_list();

  if (!netif || !netif->send_packet) {
    return;
  }

  memset(packet, 0, sizeof(packet));
  memset(eth->dest, 0xFF, 6);
  memcpy(eth->src, netif->mac, 6);
  eth->type = htons(0x0806);

  arp->hw_type = htons(0x0001);
  arp->proto_type = htons(0x0800);
  arp->hw_len = 6;
  arp->proto_len = 4;
  arp->opcode = htons(0x0001);
  memcpy(arp->src_mac, netif->mac, 6);
  arp->src_ip = my_ip;
  memset(arp->dest_mac, 0x00, 6);
  arp->dest_ip = dest_ip;

  net_log("[net] arp request for ");
  net_log_ip(dest_ip);
  net_log("\n");

  netif->send_packet(netif, packet, sizeof(packet));
}

static int netstack_resolve_mac(uint32_t dest_ip, uint8_t out_mac[6]) {
  extern volatile uint32_t timer_ticks;
  uint32_t start = timer_ticks;
  uint32_t target_ip = netstack_next_hop(dest_ip);

  if (last_arp_ip == target_ip) {
    memcpy(out_mac, last_arp_mac, 6);
    return 0;
  }

  net_log("[net] resolve mac for ");
  net_log_ip(dest_ip);
  net_log(" via ");
  net_log_ip(target_ip);
  net_log("\n");

  arp_received = 0;
  last_arp_ip = 0;
  netstack_send_arp_request(target_ip);

  while ((timer_ticks - start) < 100) {
    if (arp_received && last_arp_ip == target_ip) {
      memcpy(out_mac, last_arp_mac, 6);
      arp_received = 0;
      net_log("[net] arp resolved ");
      net_log_mac(out_mac);
      net_log("\n");
      return 0;
    }
    asm volatile("hlt");
  }

  net_log("[net] arp timeout for ");
  net_log_ip(target_ip);
  net_log("\n");
  return -1;
}

extern void tcp_handle_packet(void *packet, uint16_t len, uint32_t src_ip,
                              uint32_t dest_ip);

void netstack_handle_packet(void *data, uint16_t len) {
  ethernet_frame_t *eth = (ethernet_frame_t *)data;
  uint16_t type = htons(eth->type);

  if (type == 0x0806) { // ARP
    arp_packet_t *arp =
        (arp_packet_t *)((uintptr_t)data + sizeof(ethernet_frame_t));
    last_arp_ip = arp->src_ip;
    memcpy(last_arp_mac, arp->src_mac, 6);
    arp_received = 1;
    net_log("[net] arp rx opcode=");
    net_log_u32(htons(arp->opcode));
    net_log(" src_ip=");
    net_log_ip(arp->src_ip);
    net_log(" src_mac=");
    net_log_mac(arp->src_mac);
    net_log("\n");
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
        icmp->checksum = htons(
            net_checksum(icmp, len - sizeof(ethernet_frame_t) - 20));

        uint32_t tmp_ip = ip->src_ip;
        ip->src_ip = ip->dest_ip;
        ip->dest_ip = tmp_ip;
        ip->checksum = 0;
        ip->checksum = htons(net_checksum(ip, 20));

        net_interface_t *netif = net_get_list();
        memcpy(eth->dest, eth->src, 6);
        memcpy(eth->src, netif->mac, 6);

        netif->send_packet(netif, data, len);
      } else if (icmp->type == 0) { // Echo Reply
        last_ping_ip = ip->src_ip;
        ping_received = 1;
      }
    } else if (ip->proto == 6) { // TCP
      net_log("[net] ipv4 rx tcp from ");
      net_log_ip(ip->src_ip);
      net_log(" len=");
      net_log_u32(ip_len);
      net_log("\n");
      void *tcp_segment = (void *)((uintptr_t)data + sizeof(ethernet_frame_t) +
                                   sizeof(ipv4_packet_t));
      uint16_t tcp_len = ip_len - sizeof(ipv4_packet_t);
      tcp_handle_packet(tcp_segment, tcp_len, ip->src_ip, ip->dest_ip);
    } else if (ip->proto == 17) { // UDP
      void *udp_segment = (void *)((uintptr_t)data + sizeof(ethernet_frame_t) +
                                   sizeof(ipv4_packet_t));
      uint16_t udp_len = ip_len - sizeof(ipv4_packet_t);
      udp_handle_packet(udp_segment, udp_len, ip->src_ip, ip->dest_ip);
    }
  }
}
