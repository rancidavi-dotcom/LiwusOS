#include "dns.h"
#include "kheap.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "udp.h"
#include "task.h"

#define DNS_SERVER_IP 0x0302000A // 10.0.2.3 (QEMU DNS)
#define DNS_PORT 53
#define DNS_LOCAL_PORT 49153

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

static uint32_t dns_server_ip = DNS_SERVER_IP;

void dns_set_server(uint32_t ip) {
    dns_server_ip = ip;
}

static void dns_callback(uint32_t src_ip, uint16_t src_port, void *data, uint16_t len) {
    (void)src_ip; (void)src_port; (void)len;
    dns_header_t *hdr = (dns_header_t *)data;
    
    if (ntohs(hdr->id) != last_dns_id) return;
    if (ntohs(hdr->ans_count) == 0) return;

    // Skip Header
    uint8_t *ptr = (uint8_t *)data + sizeof(dns_header_t);

    // Skip Question Section (Name + Type + Class)
    // Name is encoded as labels (len byte followed by data)
    while (*ptr) {
        ptr += (*ptr + 1);
    }
    ptr++; // Null terminator
    ptr += 4; // Skip Type and Class

    // Answer Section
    // Skip Name (often a pointer 0xC00C)
    if ((*ptr & 0xC0) == 0xC0) {
        ptr += 2;
    } else {
        while (*ptr) ptr += (*ptr + 1);
        ptr++;
    }

    uint16_t type = ntohs(*(uint16_t *)ptr); ptr += 2;
    ptr += 2; // Class
    ptr += 4; // TTL
    uint16_t rd_len = ntohs(*(uint16_t *)ptr); ptr += 2;

    if (type == 1 && rd_len == 4) { // A Record (IPv4)
        memcpy(&resolved_ip, ptr, 4);
        dns_ready = true;
    }
}

void dns_init() {
    udp_register_callback(DNS_LOCAL_PORT, dns_callback);
}

static void dns_format_name(uint8_t *dest, const char *name) {
    int lock = 0;
    
    for (int i = 0; i <= (int)strlen(name); i++) {
        if (name[i] == '.' || name[i] == '\0') {
            *dest++ = i - lock;
            for (; lock < i; lock++) {
                *dest++ = name[lock];
            }
            lock++;
        }
    }
    *dest++ = '\0';
}

uint32_t dns_resolve(const char *hostname) {
    dns_ready = false;
    resolved_ip = 0;
    last_dns_id++;

    uint8_t packet[512];
    memset(packet, 0, sizeof(packet));

    dns_header_t *hdr = (dns_header_t *)packet;
    hdr->id = htons(last_dns_id);
    hdr->flags = htons(0x0100); // Recursive Query
    hdr->q_count = htons(1);

    uint8_t *q_name = packet + sizeof(dns_header_t);
    dns_format_name(q_name, hostname);

    uint8_t *q_info = q_name + strlen((char *)q_name) + 1;
    *(uint16_t *)q_info = htons(1); // Type A
    q_info += 2;
    *(uint16_t *)q_info = htons(1); // Class IN
    q_info += 2;

    uint16_t total_len = q_info - packet;

    serial_print("[dns] resolving ");
    serial_print(hostname);
    serial_print("\n");

    udp_send(dns_server_ip, DNS_LOCAL_PORT, DNS_PORT, packet, total_len);

    uint32_t start = timer_ticks;
    while (!dns_ready && (timer_ticks - start) < 500) {
        switch_task();
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
        return resolved_ip;
    }

    serial_print("[dns] resolution failed/timeout\n");
    return 0;
}
