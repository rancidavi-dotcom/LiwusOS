#include <stdint.h>
#include "string.h"
#include "net.h"

#define htons(n) (((((unsigned short)(n) & 0xFF)) << 8) | (((unsigned short)(n) & 0xFF00) >> 8))
#define htonl(n) (((((unsigned long)(n) & 0xFF)) << 24) | \
                  ((((unsigned long)(n) & 0xFF00)) << 8) | \
                  ((((unsigned long)(n) & 0xFF0000)) >> 8) | \
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

uint16_t net_checksum(void* data, int len) {
    uint32_t sum = 0;
    uint16_t* ptr = (uint16_t*)data;
    for (; len > 1; len -= 2) sum += *ptr++;
    if (len > 0) sum += *(uint8_t*)ptr;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~((uint16_t)sum);
}

uint32_t net_resolve_host(const char* host) {
    if (strstr(host, "google.com")) return 0x08080808; // 8.8.8.8
    if (strstr(host, "youtube.com")) return 0xEE9941D0; // 208.65.153.238
    if (strstr(host, "liwus.com")) return 0x01010101; // 1.1.1.1
    
    uint32_t hash = 5381;
    while (*host) hash = ((hash << 5) + hash) + *host++;
    return htonl(hash);
}

void netstack_send_ping(uint32_t dest_ip) {
    uint8_t packet[sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t) + sizeof(icmp_packet_t)];
    memset(packet, 0, sizeof(packet));

    ethernet_frame_t* eth = (ethernet_frame_t*)packet;
    ipv4_packet_t* ip = (ipv4_packet_t*)(packet + sizeof(ethernet_frame_t));
    icmp_packet_t* icmp = (icmp_packet_t*)(packet + sizeof(ethernet_frame_t) + sizeof(ipv4_packet_t));

    net_interface_t* netif = net_get_list();
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

void netstack_handle_packet(void* data, uint16_t len) {
    ethernet_frame_t* eth = (ethernet_frame_t*)data;
    uint16_t type = htons(eth->type);

    if (type == 0x0806) { // ARP
        arp_packet_t* arp = (arp_packet_t*)((uintptr_t)data + sizeof(ethernet_frame_t));
        if (htons(arp->opcode) == 0x0001 && arp->dest_ip == my_ip) { // ARP Request
            arp->opcode = htons(0x0002); // Reply
            memcpy(arp->dest_mac, arp->src_mac, 6);
            arp->dest_ip = arp->src_ip;
            
            net_interface_t* netif = net_get_list();
            memcpy(arp->src_mac, netif->mac, 6);
            arp->src_ip = my_ip;
            
            memcpy(eth->dest, eth->src, 6);
            memcpy(eth->src, netif->mac, 6);
            
            netif->send_packet(netif, data, len);
        }
    }
    else if (type == 0x0800) { // IPv4
        ipv4_packet_t* ip = (ipv4_packet_t*)((uintptr_t)data + sizeof(ethernet_frame_t));
        if (ip->proto == 1) { // ICMP
            icmp_packet_t* icmp = (icmp_packet_t*)((uintptr_t)data + sizeof(ethernet_frame_t) + 20);
            if (icmp->type == 8) { // Echo Request (Alguem pingou o Liwus)
                icmp->type = 0; // Echo Reply
                icmp->checksum = 0;
                icmp->checksum = net_checksum(icmp, len - sizeof(ethernet_frame_t) - 20);
                
                uint32_t tmp_ip = ip->src_ip;
                ip->src_ip = ip->dest_ip;
                ip->dest_ip = tmp_ip;
                ip->checksum = 0;
                ip->checksum = net_checksum(ip, 20);
                
                net_interface_t* netif = net_get_list();
                memcpy(eth->dest, eth->src, 6);
                memcpy(eth->src, netif->mac, 6);
                
                netif->send_packet(netif, data, len);
            }
            else if (icmp->type == 0) { // Echo Reply (RESPOSTA REAL DO PING!)
                last_ping_ip = ip->src_ip;
                ping_received = 1;
            }
        }
    }
}
