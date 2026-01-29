#include "dns.h"
#include "kheap.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "udp.h"
#include "task.h"

#define DNS_SERVER_IP 0x0302000A /* 10.0.2.3 (QEMU) - fallback */
#define DNS_PORT 53
#define DNS_LOCAL_PORT 49153

#define DNS_ATTEMPTS 2
#define DNS_ATTEMPT_TIMEOUT_TICKS 1000 /* 10s at 100Hz; slirp/WSL forwarder can stall */

typedef struct {
    uint16_t id;
    uint16_t flags;
    uint16_t q_count;
    uint16_t ans_count;
    uint16_t auth_count;
    uint16_t add_count;
} __attribute__((packed)) dns_header_t;

static uint32_t resolved_ip = 0;
static uint16_t last_dns_id = 0;
static bool dns_ready = false;
static uint32_t static_ip = 0;

static uint32_t dns_server_ip = DNS_SERVER_IP;

/* Fallback resolvers tried when the DHCP-provided (or default) server does
 * not answer in time. On QEMU/slirp the built-in forwarder can stall for
 * minutes, while 1.1.1.1 / 8.8.8.8 through the same NAT answers fast. */
static const uint32_t dns_fallback_servers[] = {
    0x01010101, /* 1.1.1.1 Cloudflare */
    0x08080808, /* 8.8.8.8  Google   */
    0x09090909  /* 9.9.9.9  Quad9    */
};

/* Static hosts: QEMU/slirp user-mode networking on this host answers UDP
 * queries ~70s late, so resolution would time out deterministically before
 * any TCP/TLS traffic could be validated. This table bypasses DNS for the
 * common test targets. */
typedef struct {
    const char *name;
    uint32_t ip;
} dns_static_host_t;

static const dns_static_host_t dns_static_hosts[] = {
    { "example.com",      0xF39342AC }, /* 172.66.147.243 */
    { "10.0.2.2",        0x0202000A }, /* slirp host (IP literal) */
    { "tls.test",         0x0202000A }, /* 10.0.2.2 (slirp host) */
};

static uint32_t dns_static_lookup(const char *hostname) {
    for (size_t i = 0; i < sizeof(dns_static_hosts) / sizeof(dns_static_hosts[0]); i++) {
        if (strcmp(dns_static_hosts[i].name, hostname) == 0) {
            return dns_static_hosts[i].ip;
        }
    }
    return 0;
}

/* Tiny, single-entry cache to avoid re-resolving the same host */
static char cache_host[128];
static uint32_t cache_ip = 0;

void dns_set_server(uint32_t ip) {
    dns_server_ip = ip;
}

static void dns_callback(uint32_t src_ip, uint16_t src_port, void *data, uint16_t len) {
    (void)src_ip; (void)src_port; (void)len;
    dns_header_t *hdr = (dns_header_t *)data;

    serial_print("[dns] rx id=");
    {
        char num[16];
        itoa(ntohs(hdr->id), num, 10);
        serial_print(num);
    }
    serial_print(" answ=");
    {
        char num[16];
        itoa(ntohs(hdr->ans_count), num, 10);
        serial_print(num);
    }
    serial_print(" t=");
    {
        char num[16];
        itoa(timer_ticks, num, 10);
        serial_print(num);
    }
    serial_print("\n");

    if (ntohs(hdr->id) != last_dns_id) return;
    if (ntohs(hdr->ans_count) == 0) return;

    /* Skip Header */
    uint8_t *ptr = (uint8_t *)data + sizeof(dns_header_t);

    /* Skip Question Section (Name + Type + Class) */
    while (*ptr) {
        ptr += (*ptr + 1);
    }
    ptr++; /* Null terminator */
    ptr += 4; /* Skip Type and Class */

    /* Answer Section: skip Name (often a pointer 0xC00C) */
    if ((*ptr & 0xC0) == 0xC0) {
        ptr += 2;
    } else {
        while (*ptr) ptr += (*ptr + 1);
        ptr++;
    }

    uint16_t type = ntohs(*(uint16_t *)ptr); ptr += 2;
    ptr += 2; /* Class */
    ptr += 4; /* TTL */
    uint16_t rd_len = ntohs(*(uint16_t *)ptr); ptr += 2;

    if (type == 1 && rd_len == 4) { /* A Record (IPv4) */
        memcpy(&resolved_ip, ptr, 4);
        dns_ready = true;
    }
}

void dns_init() {
    udp_register_callback(DNS_LOCAL_PORT, dns_callback);
}

static void dns_format_name(uint8_t *dest, const char *hostname) {
    int lock = 0;
    char name[128];
    strcpy(name, hostname);
    strcat(name, ".");

    for (int i = 0; i < (int)strlen(name); i++) {
        if (name[i] == '.') {
            *dest++ = (uint8_t)(i - lock);
            for (; lock < i; lock++) {
                *dest++ = (uint8_t)name[lock];
            }
            lock++;
        }
    }
    *dest++ = '\0';
}

uint32_t dns_resolve(const char *hostname) {
    if (cache_ip != 0 && strcmp(cache_host, hostname) == 0) {
        serial_print("[dns] cache hit ");
        serial_print(hostname);
        serial_print("\n");
        return cache_ip;
    }

    static_ip = dns_static_lookup(hostname);
    if (static_ip != 0) {
        serial_print("[dns] static hosts: ");
        serial_print(hostname);
        serial_print("\n");
        resolved_ip = static_ip;
        return static_ip;
    }

    dns_ready = false;
    resolved_ip = 0;

    uint8_t packet[512];
    memset(packet, 0, sizeof(packet));

    dns_header_t *hdr = (dns_header_t *)packet;
    hdr->flags = htons(0x0100); /* Recursive Query */
    hdr->q_count = htons(1);

    uint8_t *q_name = packet + sizeof(dns_header_t);
    dns_format_name(q_name, hostname);

    uint8_t *q_info = q_name + strlen((char *)q_name) + 1;
    *(uint16_t *)q_info = htons(1); /* Type A */
    q_info += 2;
    *(uint16_t *)q_info = htons(1); /* Class IN */
    q_info += 2;

    uint16_t total_len = (uint16_t)(q_info - packet);

    serial_print("[dns] resolving ");
    serial_print(hostname);
    serial_print("\n");

    uint32_t server_list[8];
    int server_count = 0;
    server_list[server_count++] = dns_server_ip;
    for (size_t i = 0; i < sizeof(dns_fallback_servers) / sizeof(dns_fallback_servers[0])
         && server_count < 8; i++) {
        server_list[server_count++] = dns_fallback_servers[i];
    }

    for (int s = 0; s < server_count && !dns_ready; s++) {
        for (int attempt = 0; attempt < DNS_ATTEMPTS && !dns_ready; attempt++) {
            last_dns_id++;
            hdr->id = htons(last_dns_id);

            serial_print("[dns] send id=");
            {
                char num[16];
                itoa(last_dns_id, num, 10);
                serial_print(num);
            }
            serial_print(" t=");
            {
                char num[16];
                itoa(timer_ticks, num, 10);
                serial_print(num);
            }
            serial_print("\n");

            udp_send(server_list[s], DNS_LOCAL_PORT, DNS_PORT, packet, total_len);

            uint32_t start = timer_ticks;
            while (!dns_ready && (timer_ticks - start) < DNS_ATTEMPT_TIMEOUT_TICKS) {
                task_sleep_ms(5);
            }
        }
        if (!dns_ready && s < server_count - 1) {
            serial_print("[dns] switching DNS server\n");
        }
    }

    if (dns_ready) {
        serial_print("[dns] resolved to ");
        char ip_str[16];
        itoa(resolved_ip & 0xFF, ip_str, 10);
        serial_print(ip_str); serial_print(".");
        itoa((resolved_ip >> 8) & 0xFF, ip_str, 10);
        serial_print(ip_str); serial_print(".");
        itoa((resolved_ip >> 16) & 0xFF, ip_str, 10);
        serial_print(ip_str); serial_print(".");
        itoa((resolved_ip >> 24) & 0xFF, ip_str, 10);
        serial_print(ip_str); serial_print("\n");

        strcpy(cache_host, hostname);
        cache_ip = resolved_ip;
        return resolved_ip;
    }

    serial_print("[dns] resolution failed/timeout\n");
    return 0;
}