#include "http.h"
#include "kheap.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"
#include "tcp.h"
#include "task.h"
#include "timer.h"
#include "net/tls_socket.h"

static void http_log(const char *msg) { serial_print(msg); }

static void http_log_u32(uint32_t value) {
    char num[16];
    itoa((int)value, num, 10);
    serial_print(num);
}

static void http_log_ip(uint32_t ip) {
    char num[16];

    itoa((int)(ip & 0xFF), num, 10);
    serial_print(num);
    serial_print(".");
    itoa((int)((ip >> 8) & 0xFF), num, 10);
    serial_print(num);
    serial_print(".");
    itoa((int)((ip >> 16) & 0xFF), num, 10);
    serial_print(num);
    serial_print(".");
    itoa((int)((ip >> 24) & 0xFF), num, 10);
    serial_print(num);
}

/* Connection abstraction - can be TCP or TLS */
typedef enum {
    HTTP_CONN_TCP,
    HTTP_CONN_TLS
} http_conn_type_t;

typedef struct {
    http_conn_type_t type;
    union {
        tcp_socket_t *tcp;
        tls_socket_t *tls;
    } sock;
} http_conn_t;

static int http_conn_send(http_conn_t *conn, const uint8_t *buf, uint32_t len) {
    if (conn->type == HTTP_CONN_TCP) {
        return tcp_send(conn->sock.tcp, buf, len);
    } else {
        return tls_socket_write(conn->sock.tls, buf, len);
    }
}

static int http_conn_receive(http_conn_t *conn, uint8_t *buf, uint32_t len) {
    if (conn->type == HTTP_CONN_TCP) {
        return tcp_receive(conn->sock.tcp, buf, len);
    } else {
        int r = tls_socket_read(conn->sock.tls, buf, len);
        if (r > 0) {
            static int first_recv = 1;
            if (first_recv) {
                first_recv = 0;
                serial_print("[http] first tls record: ");
                for (int i = 0; i < r && i < 24; i++) {
                    char num[8];
                    itoa(buf[i], num, 16);
                    if (buf[i] < 16) serial_print("0");
                    serial_print(num);
                    serial_print(" ");
                }
                serial_print("\n");
            }
        }
        return r;
    }
}

static void http_conn_close(http_conn_t *conn) {
    if (conn->type == HTTP_CONN_TCP) {
        tcp_close(conn->sock.tcp);
    } else {
        tls_socket_close(conn->sock.tls);
    }
}

static int http_parse_url_internal(const char *url, char *host, char *path, uint16_t *port, int *use_tls) {
    http_log("[http] parse_url: ");
    http_log(url);
    http_log("\n");
    *port = 80; /* Default HTTP port */
    *use_tls = 0;

    /* Skip "http://" or "https://" */
    if (strstr(url, "http://") == url) {
        url += 7;
        *use_tls = 0;
        *port = 80;
    } else if (strstr(url, "https://") == url) {
        url += 8;
        *use_tls = 1;
        *port = 443;
    } else {
        /* Assume HTTP */
        *use_tls = 0;
        *port = 80;
    }

    /* Find end of host (either ':', '/', or end of string) */
    const char *ptr = url;
    int host_len = 0;

    while (*ptr && *ptr != ':' && *ptr != '/') {
        if (host_len < 127) host[host_len++] = *ptr++;
    }
    host[host_len] = '\0';

    /* Check for port */
    if (*ptr == ':') {
        ptr++;
        *port = 0;
        while (*ptr >= '0' && *ptr <= '9') {
            *port = (uint16_t)((*port * 10) + (uint16_t)(*ptr - '0'));
            ptr++;
        }
    }

    /* Copy path (or default to "/") */
    if (*ptr == '/') {
        strcpy(path, ptr);
    } else {
        strcpy(path, "/");
    }

    http_log("[http] host=");
    http_log(host);
    http_log(" port=");
    http_log_u32(*port);
    http_log(" tls=");
    http_log_u32(*use_tls);
    http_log(" path=");
    http_log(path);
    http_log("\n");

    return 0;
}

/* Find a header value (case-insensitive key) in a raw HTTP response.
 * Returns 1 and copies the value into 'out' if found, 0 otherwise. */
static int http_header_value(const char *hp, const char *key, char *out, int outsz) {
    const char *p = strstr(hp, "\r\n");
    if (!p) return 0;
    p += 2;

    while (*p) {
        if (*p == '\r') break; /* blank line: end of headers */

        const char *eol = strstr(p, "\r\n");
        int line_len = eol ? (int)(eol - p) : (int)strlen(p);

        const char *colon = NULL;
        for (int i = 0; i < line_len; i++) {
            if (p[i] == ':') { colon = p + i; break; }
        }

        if (colon) {
            int klen = (int)strlen(key);
            if (colon - p >= klen) {
                int match = 1;
                for (int i = 0; i < klen; i++) {
                    char c = p[i];
                    if (c >= 'A' && c <= 'Z') c = (char)(c + 32);
                    if (c != key[i]) { match = 0; break; }
                }
                if (match) {
                    const char *v = colon + 1;
                    while (*v == ' ' || *v == '\t') v++;
                    int n = 0;
                    while (*v && *v != '\r' && *v != '\n' && n < outsz - 1) out[n++] = *v++;
                    out[n] = '\0';
                    return 1;
                }
            }
        }

        if (!eol) break;
        p = eol + 2;
    }
    return 0;
}

/* Resolve 'loc' (possibly relative) against absolute 'base'. Returns 0 on
 * success, -1 if it cannot/should not be followed (e.g. same-page anchor). */
static int http_resolve_url(const char *base, const char *loc, char *out, int outsz) {
    if (strstr(loc, "http://") == loc || strstr(loc, "https://") == loc) {
        int n = (int)strlen(loc);
        if (n >= outsz) n = outsz - 1;
        memcpy(out, loc, n);
        out[n] = '\0';
        return 0;
    }
    if (loc[0] == '#') return -1; /* same-page anchor: ignore */

    char scheme[8];
    const char *p = strstr(base, "://");
    int slen = p ? (int)(p - base) : 4;
    if (slen > 7) slen = 7;
    memcpy(scheme, base, slen);
    scheme[slen] = '\0';

    /* prefix = scheme://host (everything in base up to first '/') */
    const char *h = base;
    const char *hend = h;
    while (*hend && *hend != '/') hend++;
    int plen = (int)(hend - h);
    if (plen >= outsz) plen = outsz - 1;
    memcpy(out, h, plen);
    out[plen] = '\0';
    char *prefix = out;

    if (loc[0] == '/' && loc[1] == '/') {
        /* scheme-relative: //host/path */
        if ((int)strlen(scheme) + 1 + (int)strlen(loc) >= outsz) return -1;
        strcat(out, ":");
        strcat(out, loc);
        return 0;
    }
    if (loc[0] == '/') {
        if ((int)(strlen(prefix) + strlen(loc)) >= outsz) return -1;
        strcat(prefix, loc);
        return 0;
    }

    /* Relative to the directory of the base path */
    char dir[256];
    dir[0] = '\0';
    const char *path = hend; /* starts with '/' */
    const char *last_slash = path + strlen(path);
    for (const char *q = path; *q; q++) {
        if (*q == '/') last_slash = q;
    }
    if (last_slash > path) {
        int dlen = (int)(last_slash - path) + 1;
        if (dlen >= (int)sizeof(dir)) dlen = (int)sizeof(dir) - 1;
        memcpy(dir, path, dlen);
        dir[dlen] = '\0';
    }
    if ((int)(strlen(prefix) + strlen(dir) + strlen(loc)) >= outsz) return -1;
    strcat(prefix, dir);
    strcat(prefix, loc);
    return 0;
}

/* Low-level fetch: connect, send GET, drain the response. 'response' keeps
 * the raw HTTP response (headers + body). Returns total bytes, or -1. */
static int http_fetch_raw(const char *host, uint16_t port, const char *path,
                          int use_tls, char *response, uint32_t max_len) {
    /* Resolve host to IP */
    extern uint32_t net_resolve_host(const char *host);
    uint32_t ip = net_resolve_host(host);

    http_log("[http] resolve ");
    http_log(host);
    http_log(" -> ");
    http_log_ip(ip);
    http_log("\n");

    if (ip == 0) {
        http_log("[http] resolve failed\n");
        return -1;
    }

    http_conn_t conn;
    conn.type = use_tls ? HTTP_CONN_TLS : HTTP_CONN_TCP;
    conn.sock.tcp = NULL;
    conn.sock.tls = NULL;

    if (use_tls) {
        http_log("[http] TLS connect begin\n");
        conn.sock.tls = tls_socket_create(host);
        if (!conn.sock.tls) {
            http_log("[http] TLS create failed\n");
            return -1;
        }
        if (tls_socket_connect(conn.sock.tls, ip, port, host) < 0) {
            http_log("[http] TLS connect failed\n");
            tls_socket_close(conn.sock.tls);
            return -1;
        }
        http_log("[http] TLS connect ok\n");
    } else {
        http_log("[http] TCP connect begin\n");
        conn.sock.tcp = tcp_connect(ip, port);
        if (!conn.sock.tcp) {
            http_log("[http] TCP connect failed\n");
            return -1;
        }
        http_log("[http] TCP connect ok\n");
    }

    char request[512];
    strcpy(request, "GET ");
    strcat(request, path);
    strcat(request, " HTTP/1.1\r\nHost: ");
    strcat(request, host);
    strcat(request, "\r\nUser-Agent: LiwusOS/2.0\r\nAccept: text/html,*/*;q=0.1\r\nConnection: close\r\n\r\n");

    http_log("[http] request bytes=");
    http_log_u32((uint32_t)strlen(request));
    http_log("\n");

    int sent = http_conn_send(&conn, (const uint8_t *)request, strlen(request));
    if (sent < 0) {
        http_log("[http] send failed\n");
        http_conn_close(&conn);
        return -1;
    }
    http_log("[http] request sent\n");

    uint32_t total = 0;
    uint8_t *chunk = (uint8_t *)kmalloc(1024);
    if (!chunk) {
        http_conn_close(&conn);
        return -1;
    }

    uint32_t start = timer_ticks;
    uint32_t last_rx = timer_ticks;
    while (total < max_len - 1) {
        if ((timer_ticks - start) > 15000) break; /* hard cap ~150s */
        int received = http_conn_receive(&conn, chunk, 1023);
        if (received > 0) {
            if (total + (uint32_t)received >= max_len - 1) {
                received = (int)(max_len - 1 - total);
            }
            memcpy(response + total, chunk, received);
            total += (uint32_t)received;
            last_rx = timer_ticks;
        } else if (received == 0) {
            if ((timer_ticks - last_rx) > 1500) break; /* 15s silent: done */
            task_sleep_ms(5);
        } else {
            break;
        }
    }
    response[total] = '\0';
    kfree(chunk);
    http_conn_close(&conn);

    http_log("[http] total bytes=");
    http_log_u32(total);
    http_log("\n");

    return (int)total;
}

int http_get_tls(const char *host, uint16_t port, const char *path, char *response,
                 uint32_t max_len, int use_tls) {
    int got = http_fetch_raw(host, port, path, use_tls, response, max_len);
    if (got <= 0) return got;

    /* Skip HTTP headers */
    char *body = strstr(response, "\r\n\r\n");
    if (body) {
        body += 4;
        int body_len = strlen(body);
        memmove(response, body, body_len + 1);
        http_log("[http] body bytes=");
        http_log_u32((uint32_t)body_len);
        http_log("\n");
        return body_len;
    }

    http_log("[http] header separator not found\n");
    return got;
}

int http_get_url(const char *url, char *response, uint32_t max_len) {
    char cur_url[320];
    int n = (int)strlen(url);
    if (n >= (int)sizeof(cur_url)) n = (int)sizeof(cur_url) - 1;
    memcpy(cur_url, url, n);
    cur_url[n] = '\0';

    for (int hops = 0; hops < 4; hops++) {
        char host[128];
        char path[256];
        uint16_t port;
        int use_tls = 0;

        if (http_parse_url_internal(cur_url, host, path, &port, &use_tls) < 0) {
            http_log("[http] parse_url failed\n");
            return -1;
        }

        int got = http_fetch_raw(host, port, path, use_tls, response, max_len);
        if (got <= 0) return got;

        /* Check for redirect (3xx + Location) */
        int code = 0;
        if (got >= 12 && response[0] == 'H') {
            code = (response[9] - '0') * 100 +
                   (response[10] - '0') * 10 +
                   (response[11] - '0');
        }
        if (code >= 300 && code < 400) {
            char loc[300];
            if (http_header_value(response, "location", loc, sizeof(loc)) > 0) {
                char next[340];
                if (http_resolve_url(cur_url, loc, next, sizeof(next)) == 0) {
                    http_log("[http] redirect -> ");
                    http_log(next);
                    http_log("\n");
                    n = (int)strlen(next);
                    if (n >= (int)sizeof(cur_url)) n = (int)sizeof(cur_url) - 1;
                    memcpy(cur_url, next, n);
                    cur_url[n] = '\0';
                    continue;
                }
            }
        }

        /* Not a redirect: strip headers and return the body */
        char *body = strstr(response, "\r\n\r\n");
        if (body) {
            body += 4;
            int body_len = strlen(body);
            memmove(response, body, body_len + 1);
            http_log("[http] body bytes=");
            http_log_u32((uint32_t)body_len);
            http_log("\n");
            return body_len;
        }
        return got;
    }

    /* Redirect loop exhausted */
    http_log("[http] too many redirects\n");
    return -1;
}

/* Legacy function for backward compatibility */
int http_parse_url(const char *url, char *host, char *path, uint16_t *port) {
    int use_tls;
    return http_parse_url_internal(url, host, path, port, &use_tls);
}

int http_get(const char *host, uint16_t port, const char *path, char *response,
             uint32_t max_len) {
    return http_get_tls(host, port, path, response, max_len, 0);
}