#include "dhcp.h"
#include "net.h"
#include "netstack.h"
#include "udp.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "task.h"

#define DHCP_BOOTREQUEST 1
#define DHCP_BOOTREPLY   2
#define DHCP_HTYPE_ETHERNET 1
#define DHCP_HLEN_ETHERNET  6

typedef struct {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
} __attribute__((packed)) dhcp_packet_t;

static uint32_t dhcp_xid = 0x12345678;
static bool dhcp_bound = false;

// Funções externas do netstack para atualizar config
extern void netstack_set_my_ip(uint32_t ip);
extern void netstack_set_gateway(uint32_t ip);
extern void dns_set_server(uint32_t ip);

static void dhcp_callback(uint32_t src_ip, uint16_t src_port, void *data, uint16_t len) {
    (void)src_ip; (void)src_port; (void)len;
    dhcp_packet_t *pkt = (dhcp_packet_t *)data;

    if (pkt->op != DHCP_BOOTREPLY) return;
    if (pkt->xid != htonl(dhcp_xid)) return;

    uint8_t *opt = pkt->options;
    uint8_t msg_type = 0;
    uint32_t server_id = 0;

    // Parse options
    while (opt < pkt->options + 312 && *opt != 0xFF) {
        if (*opt == 0) { opt++; continue; }
        uint8_t code = *opt++;
        uint8_t l = *opt++;
        
        if (code == 53) msg_type = *opt; // Message Type
        if (code == 54) memcpy(&server_id, opt, 4); // Server Identifier
        if (code == 3)  netstack_set_gateway(*(uint32_t*)opt); // Router
        if (code == 6)  dns_set_server(*(uint32_t*)opt); // DNS

        opt += l;
    }

    if (msg_type == 2) { // OFFER
        serial_print("[dhcp] offer received, sending request...\n");
        // Enviar Request (simplificado para este lab)
        netstack_set_my_ip(pkt->yiaddr);
        dhcp_bound = true; 
    } else if (msg_type == 5) { // ACK
        serial_print("[dhcp] ack received, network bound.\n");
        netstack_set_my_ip(pkt->yiaddr);
        dhcp_bound = true;
    }
}

void dhcp_init() {
    udp_register_callback(68, dhcp_callback);
}

void dhcp_discover() {
    dhcp_bound = false;
    dhcp_packet_t *pkt = kmalloc(sizeof(dhcp_packet_t));
    memset(pkt, 0, sizeof(dhcp_packet_t));

    pkt->op = DHCP_BOOTREQUEST;
    pkt->htype = DHCP_HTYPE_ETHERNET;
    pkt->hlen = DHCP_HLEN_ETHERNET;
    pkt->xid = htonl(dhcp_xid);
    pkt->magic = htonl(0x63825363);

    net_interface_t *netif = net_get_list();
    if (netif) memcpy(pkt->chaddr, netif->mac, 6);

    uint8_t *opt = pkt->options;
    *opt++ = 53; *opt++ = 1; *opt++ = 1; // DHCP Discover
    *opt++ = 255; // End

    serial_print("[dhcp] sending discover...\n");
    udp_send(0xFFFFFFFF, 68, 67, pkt, sizeof(dhcp_packet_t));
    kfree(pkt);

    uint32_t start = timer_ticks;
    while (!dhcp_bound && (timer_ticks - start) < 500) {
        switch_task();
    }
}
