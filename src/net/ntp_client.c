#include "ntp_client.h"
#include "net_manager.h"
#include "serial.h"
#include "timer.h"
#include "rtc.h"
#include "string.h"
#include "vga.h"

/* lwIP Headers */
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/def.h"

#define NTP_PORT 123
#define NTP_PACKET_SIZE 48
#define NTP_TIMESTAMP_DELTA 2208988800ULL /* Segundos entre 1900 e 1970 */

static volatile bool g_ntp_synced = false;
static volatile uint64_t g_base_unix_time = 0;
static volatile uint32_t g_base_ticks = 0;

typedef struct {
    volatile bool received;
    uint32_t ntp_seconds;
} ntp_context_t;

static void ntp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                              const ip_addr_t *addr, u16_t port) {
    (void)pcb; (void)addr; (void)port;
    ntp_context_t *ctx = (ntp_context_t *)arg;
    if (!ctx || !p) return;

    if (p->tot_len >= NTP_PACKET_SIZE) {
        uint8_t buffer[NTP_PACKET_SIZE];
        pbuf_copy_partial(p, buffer, NTP_PACKET_SIZE, 0);

        /* Transmit Timestamp esta no offset 40 (4 bytes em Big-Endian) */
        uint32_t high_word = ((uint32_t)buffer[40] << 24) |
                             ((uint32_t)buffer[41] << 16) |
                             ((uint32_t)buffer[42] << 8)  |
                             ((uint32_t)buffer[43]);

        if (high_word > 0) {
            ctx->ntp_seconds = high_word;
            ctx->received = true;
        }
    }
    pbuf_free(p);
}

static int try_sync_with_server(const char *server_ip_str) {
    ip4_addr_t server_ip;
    if (!ip4addr_aton(server_ip_str, &server_ip)) {
        return -1;
    }

    struct udp_pcb *pcb = udp_new();
    if (!pcb) {
        return -2;
    }

    ntp_context_t ctx;
    ctx.received = false;
    ctx.ntp_seconds = 0;

    udp_recv(pcb, ntp_recv_callback, &ctx);
    udp_bind(pcb, IP_ADDR_ANY, 0);

    /* Monta requisicao NTP v4 Client (LI=0, VN=4, Mode=3 -> 0x23) */
    uint8_t packet[NTP_PACKET_SIZE];
    memset(packet, 0, sizeof(packet));
    packet[0] = 0x23;

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, NTP_PACKET_SIZE, PBUF_RAM);
    if (!p) {
        udp_remove(pcb);
        return -3;
    }

    pbuf_take(p, packet, NTP_PACKET_SIZE);
    udp_sendto(pcb, p, (ip_addr_t *)&server_ip, NTP_PORT);
    pbuf_free(p);

    asm volatile("sti");

    /* Aguarda resposta com timeout de 2 segundos */
    uint32_t timeout = timer_ticks + 200;
    while (!ctx.received && timer_ticks < timeout) {
        net_poll();
        asm volatile("pause");
    }

    udp_remove(pcb);

    if (ctx.received && ctx.ntp_seconds >= NTP_TIMESTAMP_DELTA) {
        uint64_t unix_time = ctx.ntp_seconds - NTP_TIMESTAMP_DELTA;
        g_base_unix_time = unix_time;
        g_base_ticks = timer_ticks;
        g_ntp_synced = true;

        /* Atualiza tambem o RTC em hardware no CMOS */
        int y, mo, d, h, m, s;
        ntp_datetime_get(-3, &y, &mo, &d, &h, &m, &s);
        rtc_write_time((uint8_t)h, (uint8_t)m, (uint8_t)s);

        char time_str[64];
        ntp_get_formatted_time(time_str, sizeof(time_str), -3);
        serial_printf("[NTP] Sucesso com %s! Horario: %s\n", server_ip_str, time_str);
        vga_puts("[NTP] Sincronizado: ");
        vga_puts(time_str);
        vga_puts("\n");
        return 0;
    }

    return -4;
}

int ntp_sync(const char *server_ip_str) {
    if (!net_is_active()) return -1;

    /* Se o usuario forneceu um servidor especifico, tenta ele */
    if (server_ip_str && server_ip_str[0] != '\0') {
        return try_sync_with_server(server_ip_str);
    }

    /* Lista de servidores padrao */
    char gw_ip[16];
    net_get_ip_strings(NULL, NULL, gw_ip);

    const char *fallback_servers[] = {
        gw_ip,             /* Gateway do usuario (roteador geralmente roda NTP) */
        "216.239.35.0",   /* time.google.com */
        "200.160.7.186",  /* a.st1.ntp.br */
        "162.159.200.1"   /* time.cloudflare.com */
    };

    size_t num_servers = sizeof(fallback_servers) / sizeof(fallback_servers[0]);
    for (size_t i = 0; i < num_servers; i++) {
        if (!fallback_servers[i] || fallback_servers[i][0] == '\0' ||
            strcmp(fallback_servers[i], "0.0.0.0") == 0) {
            continue;
        }
        if (try_sync_with_server(fallback_servers[i]) == 0) {
            return 0;
        }
    }

    return -1;
}

bool ntp_is_synced(void) {
    return g_ntp_synced;
}

uint64_t ntp_get_unix_time(void) {
    if (g_ntp_synced) {
        uint32_t elapsed_ticks = timer_ticks - g_base_ticks;
        return g_base_unix_time + (elapsed_ticks / 100);
    }

    /* Se ainda nao sincronizou via rede, estima a partir do RTC local */
    rtc_time_t rtc = rtc_read_time();
    /* Retorna uma base com hora/min/seg do RTC para 2026-11-09 */
    uint64_t base_day = 1794254400ULL; /* Aprox Nov 9 2026 */
    return base_day + (rtc.hour * 3600) + (rtc.minute * 60) + rtc.second;
}

void ntp_datetime_get(int tz_offset, int *year, int *month, int *day,
                      int *hour, int *min, int *sec) {
    uint64_t ts = ntp_get_unix_time();
    int64_t adjusted = (int64_t)ts + ((int64_t)tz_offset * 3600);
    if (adjusted < 0) adjusted = 0;

    if (sec) *sec = adjusted % 60;
    adjusted /= 60;
    if (min) *min = adjusted % 60;
    adjusted /= 60;
    if (hour) *hour = adjusted % 24;
    int64_t days = adjusted / 24;

    /* Calcula ano */
    int y = 1970;
    while (1) {
        int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
        int days_in_year = leap ? 366 : 365;
        if (days >= days_in_year) {
            days -= days_in_year;
            y++;
        } else {
            break;
        }
    }
    if (year) *year = y;

    static const int days_per_month[2][12] = {
        {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
        {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
    };
    int leap = (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
    int m = 0;
    for (m = 0; m < 12; m++) {
        int dim = days_per_month[leap][m];
        if (days >= dim) {
            days -= dim;
        } else {
            break;
        }
    }
    if (month) *month = m + 1;
    if (day) *day = (int)days + 1;
}

void ntp_get_formatted_time(char *buf, size_t max_len, int tz_offset) {
    if (!buf || max_len < 32) return;

    int y, mo, d, h, m, s;
    ntp_datetime_get(tz_offset, &y, &mo, &d, &h, &m, &s);

    /* Formato: YYYY-MM-DD HH:MM:SS BRT */
    char num[16];
    buf[0] = '\0';

    /* Ano */
    int_to_str(y, num);
    strcat(buf, num);
    strcat(buf, "-");

    /* Mes */
    if (mo < 10) strcat(buf, "0");
    int_to_str(mo, num);
    strcat(buf, num);
    strcat(buf, "-");

    /* Dia */
    if (d < 10) strcat(buf, "0");
    int_to_str(d, num);
    strcat(buf, num);
    strcat(buf, " ");

    /* Hora */
    if (h < 10) strcat(buf, "0");
    int_to_str(h, num);
    strcat(buf, num);
    strcat(buf, ":");

    /* Minuto */
    if (m < 10) strcat(buf, "0");
    int_to_str(m, num);
    strcat(buf, num);
    strcat(buf, ":");

    /* Segundo */
    if (s < 10) strcat(buf, "0");
    int_to_str(s, num);
    strcat(buf, num);

    strcat(buf, (tz_offset == -3) ? " BRT" : " UTC");
}
