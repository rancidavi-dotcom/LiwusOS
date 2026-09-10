#ifdef KERNEL_TEST
#include "framework.h"
#include "http.h"
#include "net.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"
#include "timer.h"

static void net_print_ip(uint32_t ip) {
    char num[16];
    itoa((int)(ip & 0xFF), num, 10); serial_print(num); serial_print(".");
    itoa((int)((ip >> 8) & 0xFF), num, 10); serial_print(num); serial_print(".");
    itoa((int)((ip >> 16) & 0xFF), num, 10); serial_print(num); serial_print(".");
    itoa((int)((ip >> 24) & 0xFF), num, 10); serial_print(num);
}

int test_net_interface_ready(void) {
    TEST_BEGIN("net_interface_ready");

    net_interface_t *netif = net_get_list();
    if (!netif || !netif->send_packet) {
        FAIL("net_interface_ready", "no interface or send_packet");
    }

    serial_print("  [net] iface=");
    serial_print(netif->name);
    serial_print(" my_ip=");
    net_print_ip(netstack_get_my_ip());
    serial_print("\n");

    PASS("net_interface_ready");
}

/* Diagnoses ARP + ICMP round trip to the SLIRP gateway. */
int test_net_gateway_ping(void) {
    TEST_BEGIN("net_gateway_ping");

    uint32_t gw = 0x0202000A; /* 10.0.2.2 */
    serial_print("  [net] ping 10.0.2.2...\n");
    int r = netstack_ping(gw, 300);
    serial_print("  [net] ping result ticks=");
    char num[16];
    itoa(r, num, 10);
    serial_print(num);
    serial_print("\n");
    if (r < 0) {
        FAIL("net_gateway_ping", "ping timeout (ARP or ICMP broken)");
    }
    PASS("net_gateway_ping");
}

/* Diagnoses the DNS/UDP path for our real browser request. */
int test_net_dns_resolve(void) {
    TEST_BEGIN("net_dns_resolve");

    uint32_t ip = net_resolve_host("example.com");
    serial_print("  [net] example.com -> ");
    if (ip == 0) {
        serial_print("0.0.0.0\n");
        FAIL("net_dns_resolve", "dns resolution failed/timeout");
    }
    net_print_ip(ip);
    serial_print("\n");

    PASS("net_dns_resolve");
}

/* Full end-to-end: DNS + TCP + HTTP GET of example.com. */
int test_net_http_fetch(void) {
    TEST_BEGIN("net_http_fetch");

    static char buf[8192];
    int got = http_get_url("http://example.com/", buf, sizeof(buf));
    serial_print("  [net] http_get_url bytes=");
    char num[16];
    itoa(got, num, 10);
    serial_print(num);
    serial_print("\n");
    if (got <= 0) {
        FAIL("net_http_fetch", "http fetch failed (TCP connection?)");
    }

    serial_print("  [net] body: ");
    for (int i = 0; i < 200 && buf[i]; i++) {
        char c = buf[i];
        if (c < 32 || c > 126) c = ' ';
        write_serial(c);
    }
    serial_print("\n");

    PASS("net_http_fetch");
}
#endif /* KERNEL_TEST */