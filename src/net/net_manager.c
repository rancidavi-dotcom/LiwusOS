#include "net_manager.h"
#include "net_e1000.h"
#include "net_rtl8168.h"
#include "ntp_client.h"
#include "telnet_server.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "vga.h"
#include "task.h"
#include "spinlock.h"

extern void int_to_str(uint32_t n, char *s);

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
#include "lwip/dns.h"
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

static spinlock_t g_net_lock = {0};

uint64_t net_lock_acquire(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    spinlock_acquire(&g_net_lock);
    return flags;
}

void net_lock_release(uint64_t flags) {
    spinlock_release(&g_net_lock);
    if (flags & (1ULL << 9)) {
        __asm__ volatile("sti" ::: "memory");
    }
}

/* Polling de rede: processa pacotes recebidos e timers */
static volatile bool in_poll = false;
void net_poll(void) {
    if (!net_initialized) return;

    uint64_t flags = net_lock_acquire();
    if (in_poll) {
        net_lock_release(flags);
        return;
    }
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

    /* Monitora mudanca dinamica do link fisico e reativa DHCP */
    static int last_link_state = -1;
    bool cur_link = net_get_link_status();
    if (last_link_state < 0) {
        last_link_state = cur_link ? 1 : 0;
        if (!cur_link) {
            netif_set_link_down(&main_netif);
        }
    } else if ((cur_link ? 1 : 0) != last_link_state) {
        last_link_state = cur_link ? 1 : 0;
        if (cur_link) {
            serial_printf("[NET] Link fisico detectado UP! Solicitando DHCP...\n");
            netif_set_link_up(&main_netif);
            dhcp_start(&main_netif);
        } else {
            serial_printf("[NET] Link fisico DOWN!\n");
            netif_set_link_down(&main_netif);
        }
    }

    in_poll = false;
    net_lock_release(flags);
}

bool net_is_active(void) {
    return net_initialized;
}

bool net_has_ip(void) {
    if (!net_initialized) return false;
    return !ip4_addr_isany_val(*netif_ip4_addr(&main_netif));
}

int net_set_static_ip(const char *ip_str, const char *mask_str, const char *gw_str) {
    if (!net_initialized || !ip_str) return -1;
    ip4_addr_t ip, mask, gw;
    if (!ip4addr_aton(ip_str, &ip)) return -2;
    if (mask_str && mask_str[0] && !ip4addr_aton(mask_str, &mask)) {
        IP4_ADDR(&mask, 255, 255, 255, 0);
    } else if (!mask_str || !mask_str[0]) {
        IP4_ADDR(&mask, 255, 255, 255, 0);
    }
    if (gw_str && gw_str[0] && !ip4addr_aton(gw_str, &gw)) {
        ip4_addr_set_zero(&gw);
    } else if (!gw_str || !gw_str[0]) {
        ip4_addr_set_zero(&gw);
    }

    uint64_t flags = net_lock_acquire();
    dhcp_stop(&main_netif);
    netif_set_addr(&main_netif, &ip, &mask, &gw);
    netif_set_up(&main_netif);
    netif_set_link_up(&main_netif);
    net_lock_release(flags);
    serial_printf("[NET] IP Estatico atribuido: %s\n", ip_str);
    return 0;
}

int net_dhcp_renew(void) {
    if (!net_initialized) return -1;
    uint64_t flags = net_lock_acquire();
    dhcp_stop(&main_netif);
    ip4_addr_t zero;
    ip4_addr_set_zero(&zero);
    netif_set_addr(&main_netif, &zero, &zero, &zero);
    netif_set_link_up(&main_netif);
    dhcp_start(&main_netif);
    net_lock_release(flags);
    serial_printf("[NET] Solicitando renovacao DHCP...\n");
    return 0;
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
 * Implementacao de Resolucao DNS
 * ========================================================== */
typedef struct {
    volatile bool done;
    volatile bool success;
    ip_addr_t addr;
} dns_query_ctx_t;

static void dns_resolve_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    (void)name;
    dns_query_ctx_t *ctx = (dns_query_ctx_t *)callback_arg;
    if (ctx) {
        if (ipaddr) {
            ctx->addr = *ipaddr;
            ctx->success = true;
        } else {
            ctx->success = false;
        }
        ctx->done = true;
    }
}

void net_get_dns_server(char *dns_ip_out, size_t max_len) {
    if (!dns_ip_out || max_len < 16) return;
    const ip_addr_t *dns = dns_getserver(0);
    if (dns && !ip_addr_isany(dns)) {
        ip4addr_ntoa_r(dns, dns_ip_out, max_len);
    } else {
        strcpy(dns_ip_out, "8.8.8.8");
    }
}

int net_resolve_host(const char *hostname, char *ip_str_out, size_t max_len) {
    if (!hostname || !ip_str_out || max_len < 16) return -1;

    /* Se ja for um endereco IPv4 numerico, retorna diretamente */
    ip4_addr_t direct_ip;
    if (ip4addr_aton(hostname, &direct_ip)) {
        strncpy(ip_str_out, hostname, max_len - 1);
        ip_str_out[max_len - 1] = '\0';
        return 0;
    }

    if (!net_initialized) return -2;

    /* Garante que temos um servidor DNS valido configurado */
    uint64_t lflags = net_lock_acquire();
    const ip_addr_t *dns = dns_getserver(0);
    if (!dns || ip_addr_isany(dns)) {
        ip_addr_t def_dns;
        if (active_nic == NIC_E1000) {
            /* No QEMU, o servidor DNS virtual do slirp fica em 10.0.2.3 */
            IP4_ADDR(&def_dns, 10, 0, 2, 3);
        } else {
            const ip4_addr_t *gw = netif_ip4_gw(&main_netif);
            if (gw && !ip4_addr_isany_val(*gw)) {
                ip_addr_copy(def_dns, *gw);
            } else {
                IP4_ADDR(&def_dns, 8, 8, 8, 8);
            }
        }
        dns_setserver(0, &def_dns);
        ip_addr_t sec_dns;
        IP4_ADDR(&sec_dns, 8, 8, 8, 8);
        dns_setserver(1, &sec_dns);
    }

    static dns_query_ctx_t s_dns_ctx;
    s_dns_ctx.done = false;
    s_dns_ctx.success = false;

    ip_addr_t resolved;
    err_t err = dns_gethostbyname(hostname, &resolved, dns_resolve_callback, &s_dns_ctx);
    net_lock_release(lflags);

    if (err == ERR_OK) {
        ip4addr_ntoa_r(&resolved, ip_str_out, max_len);
        return 0;
    }

    if (err != ERR_INPROGRESS) {
        return -3;
    }

    /* Aguarda resolucao de DNS com timeout de 3 segundos */
    uint32_t timeout = timer_ticks + 300;
    while (!s_dns_ctx.done && timer_ticks < timeout) {
        net_poll();
        task_sleep_ms(2);
    }

    if (s_dns_ctx.done && s_dns_ctx.success) {
        ip4addr_ntoa_r(&s_dns_ctx.addr, ip_str_out, max_len);
        return 0;
    }

    return -4;
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

    /* Resolve nome de host (DNS) se nao for IP direto */
    char resolved_ip_str[32];
    if (net_resolve_host(target_ip_str, resolved_ip_str, sizeof(resolved_ip_str)) != 0) {
        serial_printf("ping: nao foi possivel resolver o host: %s\n", target_ip_str);
        vga_puts("ping: nao foi possivel resolver o host: ");
        vga_puts(target_ip_str);
        vga_puts("\n");
        if (output_cb) {
            char err[64];
            strcpy(err, "ping: nao foi possivel resolver: ");
            strcat(err, target_ip_str);
            output_cb(err, user_data);
        }
        return -2;
    }

    ip4_addr_t target_ip;
    if (!ip4addr_aton(resolved_ip_str, &target_ip)) {
        serial_printf("ping: IP invalido: %s\n", resolved_ip_str);
        vga_puts("ping: IP invalido: ");
        vga_puts(resolved_ip_str);
        vga_puts("\n");
        if (output_cb) {
            char err[64];
            strcpy(err, "ping: IP invalido: ");
            strcat(err, resolved_ip_str);
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

    char title[96];
    title[0] = '\0';
    strcat(title, "PING ");
    strcat(title, target_ip_str);
    if (strcmp(target_ip_str, resolved_ip_str) != 0) {
        strcat(title, " (");
        strcat(title, resolved_ip_str);
        strcat(title, ")");
    }
    strcat(title, " (ICMP Echo Request):");

    serial_printf("%s\n", title);
    vga_puts(title);
    vga_puts("\n");
    if (output_cb) output_cb(title, user_data);

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

        /* Aguarda resposta com timeout de 2 segundos */
        uint32_t timeout_ticks = timer_ticks + 200; /* 200 ticks = 2s */
        uint32_t loop_guard = 0;
        while (!ping_received && timer_ticks < timeout_ticks && loop_guard < 5000000) {
            net_poll();
            loop_guard++;
            task_sleep_ms(1);
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
            task_sleep_ms(5);
        }
    }

    raw_remove(ping_pcb);
    return 0;
}
