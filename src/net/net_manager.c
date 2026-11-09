#include "net_manager.h"
#include "net_e1000.h"
#include "net_rtl8168.h"
#include "ntp_client.h"
#include "telnet_server.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "vga.h"

/* lwIP Headers */
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/ip_addr.h"
#include "lwip/timeouts.h"
#include "lwip/raw.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "netif/ethernet.h"

typedef enum {
    NIC_NONE = 0,
    NIC_E1000,
    NIC_RTL8168
} nic_type_t;

static struct netif main_netif;
static bool net_initialized = false;
static nic_type_t active_nic = NIC_NONE;
static uint8_t rx_packet_buffer[2048];

/* Callback de envio de pacotes brutos Ethernet */
static err_t netif_linkoutput(struct netif *netif, struct pbuf *p) {
    (void)netif;
    if (p->tot_len > sizeof(rx_packet_buffer)) {
        return ERR_BUF;
    }

    uint8_t buffer[2048];
    pbuf_copy_partial(p, buffer, p->tot_len, 0);

    int rc = -1;
    if (active_nic == NIC_RTL8168) {
        rc = rtl8168_send_packet(buffer, p->tot_len);
    } else if (active_nic == NIC_E1000) {
        rc = e1000_send_packet(buffer, p->tot_len);
    }

    return (rc == 0) ? ERR_OK : ERR_IF;
}

/* Callback de inicializacao da interface no lwIP */
static err_t netif_init_callback(struct netif *netif) {
    netif->name[0] = 'e';
    netif->name[1] = 't';

    netif->output = etharp_output;
    netif->linkoutput = netif_linkoutput;
    netif->mtu = 1500;

    netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP |
                   NETIF_FLAG_LINK_UP | NETIF_FLAG_UP | NETIF_FLAG_ETHERNET;

    const uint8_t *mac = net_get_mac();
    memcpy(netif->hwaddr, mac, 6);
    netif->hwaddr_len = 6;

    return ERR_OK;
}

/* Callback chamado quando o DHCP ou usuario altera o IP */
static void netif_status_callback(struct netif *netif) {
    if (netif_is_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif))) {
        char ip[16], mask[16], gw[16];
        ip4addr_ntoa_r(netif_ip4_addr(netif), ip, sizeof(ip));
        ip4addr_ntoa_r(netif_ip4_netmask(netif), mask, sizeof(mask));
        ip4addr_ntoa_r(netif_ip4_gw(netif), gw, sizeof(gw));

        serial_printf("[NET] IP Atribuido com Sucesso!\n");
        serial_printf("[NET] IP: %s | Mascara: %s | Gateway: %s\n", ip, mask, gw);
    }
}

/* Inicializacao do subsistema de rede */
int net_init(void) {
    serial_printf("[NET] Inicializando subsistema de rede...\n");

    /* 1. Detecta e inicializa a controladora de rede presente */
    /* Primeiro tenta a Realtek RTL8168 / RTL8111 (hardware fisico H61) */
    if (rtl8168_init() == 0) {
        active_nic = NIC_RTL8168;
        serial_printf("[NET] Driver ativo: Realtek RTL8168/8111 Gigabit Ethernet\n");
    }
    /* Se nao estiver presente, tenta a Intel e1000 (QEMU / placas Intel) */
    else if (e1000_init() == 0) {
        active_nic = NIC_E1000;
        serial_printf("[NET] Driver ativo: Intel e1000 Gigabit Ethernet\n");
    }
    else {
        serial_printf("[NET] Nenhuma placa de rede Ethernet suportada encontrada.\n");
        return -1;
    }

    /* 2. Inicializa a pilha lwIP */
    lwip_init();

    /* 3. Registra a interface no lwIP com IP inicial 0.0.0.0 */
    ip4_addr_t ip, mask, gw;
    ip4_addr_set_zero(&ip);
    ip4_addr_set_zero(&mask);
    ip4_addr_set_zero(&gw);

    if (!netif_add(&main_netif, &ip, &mask, &gw, NULL, netif_init_callback, ethernet_input)) {
        serial_printf("[NET] Falha ao registrar netif no lwIP.\n");
        return -2;
    }

    netif_set_default(&main_netif);
    netif_set_status_callback(&main_netif, netif_status_callback);
    netif_set_up(&main_netif);
    netif_set_link_up(&main_netif);

    /* 4. Inicia cliente DHCP para pegar IP automaticamente do roteador */
    serial_printf("[NET] Solicitando configuracao via DHCP...\n");
    dhcp_start(&main_netif);

    net_initialized = true;

    /* 5. Inicia servidor Telnet na porta TCP 23 */
    telnet_server_init(23);

    return 0;
}

/* Polling de rede: processa pacotes recebidos e timers */
static volatile bool in_poll = false;
void net_poll(void) {
    if (!net_initialized || in_poll) return;
    in_poll = true;

    /* Le pacotes recebidos pela placa ativa */
    int len = 0;
    do {
        if (active_nic == NIC_RTL8168) {
            len = rtl8168_poll_packet(rx_packet_buffer, sizeof(rx_packet_buffer));
        } else if (active_nic == NIC_E1000) {
            len = e1000_poll_packet(rx_packet_buffer, sizeof(rx_packet_buffer));
        } else {
            len = 0;
        }

        if (len > 0) {
            struct pbuf *p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
            if (p) {
                pbuf_take(p, rx_packet_buffer, len);
                if (main_netif.input(p, &main_netif) != ERR_OK) {
                    pbuf_free(p);
                }
            }
        }
    } while (len > 0);

    /* Processa timers do lwIP (DHCP, ARP cache, TCP retransmit) */
    sys_check_timeouts();
    in_poll = false;
}

bool net_is_active(void) {
    return net_initialized;
}

bool net_has_ip(void) {
    if (!net_initialized) return false;
    return !ip4_addr_isany_val(*netif_ip4_addr(&main_netif));
}

void net_get_ip_strings(char *ip, char *netmask, char *gateway) {
    if (!net_initialized) {
        if (ip) strcpy(ip, "0.0.0.0");
        if (netmask) strcpy(netmask, "0.0.0.0");
        if (gateway) strcpy(gateway, "0.0.0.0");
        return;
    }

    if (ip) ip4addr_ntoa_r(netif_ip4_addr(&main_netif), ip, 16);
    if (netmask) ip4addr_ntoa_r(netif_ip4_netmask(&main_netif), netmask, 16);
    if (gateway) ip4addr_ntoa_r(netif_ip4_gw(&main_netif), gateway, 16);
}

const char *net_get_driver_name(void) {
    if (active_nic == NIC_RTL8168) return "Realtek RTL8168/8111";
    if (active_nic == NIC_E1000) return "Intel e1000";
    return "Nenhum";
}

const uint8_t *net_get_mac(void) {
    static const uint8_t zero_mac[6] = {0};
    if (active_nic == NIC_RTL8168) return rtl8168_get_mac();
    if (active_nic == NIC_E1000) return e1000_get_mac();
    return zero_mac;
}

bool net_get_link_status(void) {
    if (active_nic == NIC_RTL8168) return rtl8168_is_link_up();
    if (active_nic == NIC_E1000) {
        e1000_stats_t s;
        e1000_get_stats(&s);
        return s.link_up;
    }
    return false;
}

void net_get_packet_counts(uint32_t *rx, uint32_t *tx) {
    if (active_nic == NIC_RTL8168) {
        rtl8168_stats_t s;
        rtl8168_get_stats(&s);
        if (rx) *rx = s.packets_rx;
        if (tx) *tx = s.packets_tx;
    } else if (active_nic == NIC_E1000) {
        e1000_stats_t s;
        e1000_get_stats(&s);
        if (rx) *rx = s.packets_rx;
        if (tx) *tx = s.packets_tx;
    } else {
        if (rx) *rx = 0;
        if (tx) *tx = 0;
    }
}

/* ==========================================================
 * Implementacao do comando PING ICMP
 * ========================================================== */
static volatile bool ping_received = false;
static uint16_t ping_seq = 0;

static u8_t ping_raw_recv(void *arg, struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr) {
    (void)arg; (void)pcb; (void)addr;
    if (p->tot_len >= (sizeof(struct ip_hdr) + sizeof(struct icmp_echo_hdr))) {
        struct ip_hdr *iphdr = (struct ip_hdr *)p->payload;
        u16_t iphdr_len = IPH_HL(iphdr) * 4;
        if (p->tot_len >= (iphdr_len + sizeof(struct icmp_echo_hdr))) {
            struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)((u8_t *)p->payload + iphdr_len);
            if (iecho->type == ICMP_ER && iecho->id == 0x1234) {
                ping_received = true;
                pbuf_free(p);
                return 1; /* Consumido */
            }
        }
    }
    return 0;
}

int net_ping(const char *target_ip_str, uint32_t count) {
    return net_ping_custom(target_ip_str, count, NULL, NULL);
}

int net_ping_custom(const char *target_ip_str, uint32_t count,
                    net_ping_output_fn output_cb, void *user_data) {
    if (!net_initialized) {
        serial_printf("ping: rede nao inicializada\n");
        vga_puts("ping: rede nao inicializada\n");
        if (output_cb) output_cb("ping: rede nao inicializada", user_data);
        return -1;
    }

    ip4_addr_t target_ip;
    if (!ip4addr_aton(target_ip_str, &target_ip)) {
        serial_printf("ping: IP invalido: %s\n", target_ip_str);
        vga_puts("ping: IP invalido: ");
        vga_puts(target_ip_str);
        vga_puts("\n");
        if (output_cb) {
            char err[64];
            strcpy(err, "ping: IP invalido: ");
            strcat(err, target_ip_str);
            output_cb(err, user_data);
        }
        return -2;
    }

    struct raw_pcb *ping_pcb = raw_new(IP_PROTO_ICMP);
    if (!ping_pcb) {
        serial_printf("ping: erro ao alocar socket ICMP\n");
        vga_puts("ping: erro ao alocar socket ICMP\n");
        if (output_cb) output_cb("ping: erro ao alocar socket ICMP", user_data);
        return -3;
    }

    raw_recv(ping_pcb, ping_raw_recv, NULL);
    raw_bind(ping_pcb, IP_ADDR_ANY);

    serial_printf("PING %s (ICMP Echo Request):\n", target_ip_str);
    vga_puts("PING ");
    vga_puts(target_ip_str);
    vga_puts(" (ICMP Echo Request):\n");
    if (output_cb) {
        char hdr[64];
        strcpy(hdr, "PING ");
        strcat(hdr, target_ip_str);
        strcat(hdr, " (ICMP Echo Request):");
        output_cb(hdr, user_data);
    }

    for (uint32_t i = 0; i < count; i++) {
        ping_seq++;
        size_t ping_size = sizeof(struct icmp_echo_hdr) + 32;
        struct pbuf *p = pbuf_alloc(PBUF_IP, ping_size, PBUF_RAM);
        if (!p) continue;

        struct icmp_echo_hdr *iecho = (struct icmp_echo_hdr *)p->payload;
        ICMPH_TYPE_SET(iecho, ICMP_ECHO);
        ICMPH_CODE_SET(iecho, 0);
        iecho->chksum = 0;
        iecho->id = 0x1234;
        iecho->seqno = lwip_htons(ping_seq);

        /* Preenche payload do ping */
        char *data = (char *)iecho + sizeof(struct icmp_echo_hdr);
        for (int b = 0; b < 32; b++) data[b] = (char)('a' + (b % 26));

        iecho->chksum = inet_chksum(iecho, ping_size);

        ping_received = false;
        uint32_t start_tick = timer_ticks;

        raw_sendto(ping_pcb, p, (ip_addr_t *)&target_ip);
        pbuf_free(p);

        /* Habilita interrupcoes para permitir que o timer avance */
        asm volatile("sti");

        /* Aguarda resposta com timeout de 2 segundos */
        uint32_t timeout_ticks = timer_ticks + 200; /* 200 ticks = 2s */
        uint32_t loop_guard = 0;
        while (!ping_received && timer_ticks < timeout_ticks && loop_guard < 5000000) {
            net_poll();
            loop_guard++;
            asm volatile("pause");
        }

        char num_buf[16];
        if (ping_received) {
            uint32_t elapsed_ms = (timer_ticks - start_tick) * 10;
            serial_printf("64 bytes de %s: icmp_seq=%u tempo=%u ms\n",
                          target_ip_str, ping_seq, elapsed_ms);
            vga_puts("64 bytes de ");
            vga_puts(target_ip_str);
            vga_puts(": icmp_seq=");
            int_to_str(ping_seq, num_buf);
            vga_puts(num_buf);
            vga_puts(" tempo=");
            int_to_str(elapsed_ms, num_buf);
            vga_puts(num_buf);
            vga_puts(" ms\n");

            if (output_cb) {
                char msg[96];
                strcpy(msg, "64 bytes de ");
                strcat(msg, target_ip_str);
                strcat(msg, ": icmp_seq=");
                strcat(msg, num_buf);
                strcat(msg, " tempo=");
                int_to_str(elapsed_ms, num_buf);
                strcat(msg, num_buf);
                strcat(msg, " ms");
                output_cb(msg, user_data);
            }
        } else {
            serial_printf("Requisicao para %s expirou (Timeout).\n", target_ip_str);
            vga_puts("Requisicao para ");
            vga_puts(target_ip_str);
            vga_puts(" expirou (Timeout).\n");
            if (output_cb) {
                char msg[96];
                strcpy(msg, "Requisicao para ");
                strcat(msg, target_ip_str);
                strcat(msg, " expirou (Timeout).");
                output_cb(msg, user_data);
            }
        }

        /* Pequena pausa entre pings */
        uint32_t pause_until = timer_ticks + 30; /* 300 ms */
        while (timer_ticks < pause_until) {
            net_poll();
            asm volatile("pause");
        }
    }

    raw_remove(ping_pcb);
    return 0;
}
