#include "net/tls_socket.h"
#include "bearssl.h"
#include "tcp.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "task.h"
#include "timer.h"

/*
 * BearSSL glue for LiwusOS.
 *
 * Trust model for this milestone (text browser / reader mode):
 * certificate chains are ACCEPTED WITHOUT VALIDATION. A null X.509 engine
 * decodes the end-entity certificate (so key exchange works) but skips
 * anchor trust, name and validity checks. Real CA-bundle validation can be
 * swapped in later by replacing the null engine with br_x509_minimal and a
 * trust anchor table.
 */

/* ---- Null X.509 engine (decode-only, no validation) ---- */

typedef struct {
    const br_x509_class *vtable;
    br_x509_decoder_context dec;
} tls_null_x509_ctx_t;

static void tls_null_start_chain(const br_x509_class **ctx,
                                 const char *server_name) {
    (void)server_name;
    tls_null_x509_ctx_t *c = (tls_null_x509_ctx_t *)(void *)ctx;
    c->vtable = *ctx;
    br_x509_decoder_init(&c->dec, NULL, NULL);
}

static void tls_null_start_cert(const br_x509_class **ctx, uint32_t length) {
    (void)length;
    tls_null_x509_ctx_t *c = (tls_null_x509_ctx_t *)(void *)ctx;
    br_x509_decoder_init(&c->dec, NULL, NULL);
}

static void tls_null_append(const br_x509_class **ctx,
                            const unsigned char *buf, size_t len) {
    tls_null_x509_ctx_t *c = (tls_null_x509_ctx_t *)(void *)ctx;
    br_x509_decoder_push(&c->dec, buf, len);
}

static void tls_null_end_cert(const br_x509_class **ctx) {
    (void)ctx;
}

static unsigned tls_null_end_chain(const br_x509_class **ctx) {
    tls_null_x509_ctx_t *c = (tls_null_x509_ctx_t *)(void *)ctx;
    unsigned e = (unsigned)br_x509_decoder_last_error(&c->dec);
    char num[16];
    serial_print("[X9] chain end err=");
    itoa(e, num, 10);
    serial_print(num);
    serial_print("\n");
    return e;
}

static const br_x509_pkey *tls_null_get_pkey(const br_x509_class *const *ctx,
                                             unsigned *usages) {
    tls_null_x509_ctx_t *c = (tls_null_x509_ctx_t *)(void *)ctx;
    if (usages) {
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    }
    const br_x509_pkey *pk = br_x509_decoder_get_pkey(&c->dec);
    char num[16];
    serial_print("[X9] get_pkey ");
    if (pk) {
        serial_print("type=");
        itoa(pk->key_type, num, 10);
        serial_print(num);
    } else {
        serial_print("NULL");
    }
    serial_print("\n");
    return pk;
}

static const br_x509_class tls_null_vtable = {
    sizeof(tls_null_x509_ctx_t),
    tls_null_start_chain,
    tls_null_start_cert,
    tls_null_append,
    tls_null_end_cert,
    tls_null_end_chain,
    tls_null_get_pkey
};

/* ---- Socket glue ---- */

static br_ssl_engine_context *g_eng;
static uint32_t g_eng_saved;
static unsigned char *g_eng_buf;

static void eng_hex(const char *tag, uint8_t *p, int n) {
    serial_print(tag);
    for (int i = 0; i < n; i++) {
        char num[16];
        serial_print(" ");
        itoa(p[i], num, 16);
        if (p[i] < 16) serial_print("0");
        serial_print(num);
    }
    serial_print("\n");
}

static int tls_sock_read(void *ctx, unsigned char *buf, size_t len) {
    tcp_socket_t *sock = (tcp_socket_t *)ctx;
    static int dbg = 500;
    static int stall = 0;
    for (;;) {
        int r = tcp_receive(sock, buf, len);
        if (dbg > 0) {
            dbg--;
            char num[16];
            serial_print("[RK] ask=");
            itoa((int)len, num, 10);
            serial_print(num);
            serial_print(" r=");
            itoa(r, num, 10);
            serial_print(num);
            serial_print("\n");
        }
        if (r > 0) return r;
        if (r < 0) return -1;
        if (stall++ == 200 && g_eng) {
            char num[16];
            serial_print("[ENG] err=");
            itoa(br_ssl_engine_last_error(g_eng), num, 10);
            serial_print(num);
            serial_print(" iomode=");
            itoa(g_eng->iomode, num, 10);
            serial_print(num);
            serial_print(" rtypin=");
            itoa(g_eng->record_type_in, num, 10);
            serial_print(num);
            serial_print(" appdata=");
            itoa(g_eng->application_data, num, 10);
            serial_print(num);
            serial_print(" hlen_in=");
            itoa((int)g_eng->hlen_in, num, 10);
            serial_print(num);
            serial_print(" hlen_out=");
            itoa((int)g_eng->hlen_out, num, 10);
            serial_print(num);
            serial_print(" rng=");
            itoa(g_eng->rng_init_done, num, 10);
            serial_print(", ");
            itoa(g_eng->rng_os_rand_done, num, 10);
            serial_print(num);
            serial_print(" ver_in=");
            itoa(g_eng->version_in, num, 10);
            serial_print(num);
            serial_print(" reneg=");
            itoa(g_eng->reneg, num, 10);
            serial_print(num);
            serial_print(" dp=");
            itoa((int)(g_eng->cpu.dp - g_eng->dp_stack), num, 10);
            serial_print(num);
            serial_print(" rp=");
            itoa((int)(g_eng->cpu.rp - g_eng->rp_stack), num, 10);
            serial_print(num);
            serial_print(" action=");
            itoa(g_eng->action, num, 10);
            serial_print(num);
            serial_print(" alert=");
            itoa(g_eng->alert, num, 10);
            serial_print(num);
            serial_print(" hlen_in=");
            itoa((int)g_eng->hlen_in, num, 10);
            serial_print(num);
            serial_print(" hlen_out=");
            itoa((int)g_eng->hlen_out, num, 10);
            serial_print(num);
            serial_print(" ixa=");
            itoa((int)g_eng->ixa, num, 10);
            serial_print(num);
            serial_print(" ixb=");
            itoa((int)g_eng->ixb, num, 10);
            serial_print(num);
            serial_print(" ixc=");
            itoa((int)g_eng->ixc, num, 10);
            serial_print(num);
            serial_print(" oxa=");
            itoa((int)g_eng->oxa, num, 10);
            serial_print(num);
            serial_print(" oxb=");
            itoa((int)g_eng->oxb, num, 10);
            serial_print(num);
            serial_print(" oxc=");
            itoa((int)g_eng->oxc, num, 10);
            serial_print(num);
            serial_print("\n");
            eng_hex("[IP]", (uint8_t *)g_eng->cpu.ip, 16);
            {
                char num[32];
                serial_print("[IPP] ");
                itoa((uint32_t)(uintptr_t)g_eng->cpu.ip, num, 16);
                serial_print(num);
                serial_print("\n");
            }
            {
                char num[32];
                serial_print("[ADDR] eng=");
                itoa((uint32_t)(uintptr_t)g_eng, num, 16);
                serial_print(num);
                serial_print(" buf=");
                itoa((uint32_t)(uintptr_t)g_eng_buf, num, 16);
                serial_print(num);
                serial_print(" ibuf=");
                itoa((uint32_t)(uintptr_t)g_eng->ibuf, num, 16);
                serial_print(num);
                serial_print(" obuf=");
                itoa((uint32_t)(uintptr_t)g_eng->obuf, num, 16);
                serial_print(num);
                serial_print(" ibuf_len=");
                itoa((int)g_eng->ibuf_len, num, 10);
                serial_print(num);
                serial_print(" obuf_len=");
                itoa((int)g_eng->obuf_len, num, 10);
                serial_print(num);
                serial_print("\n");
            }
            eng_hex("[IB]", (uint8_t *)&g_eng->ibuf, 32);
            eng_hex("[IR]", (uint8_t *)&g_eng->ixa, 56);
            eng_hex("[RP]", (uint8_t *)g_eng->cpu.rp, 24);
            eng_hex("[DP]", (uint8_t *)g_eng->cpu.dp, 8);
        }
        task_sleep_ms(2);
    }
}

static int tls_sock_write(void *ctx, const unsigned char *buf, size_t len) {
    tcp_socket_t *sock = (tcp_socket_t *)ctx;
    return tcp_send(sock, buf, len);
}

static void tls_inject_entropy(br_ssl_engine_context *eng) {
    uint8_t ent[48];
    uint32_t r = 0;

    uint64_t t = (uint64_t)timer_ticks;
    memcpy(ent, &t, 8);
    memcpy(ent + 8, &ent, 8);
    memcpy(ent + 16, &eng, 8);

    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    memcpy(ent + 32, &lo, 4);
    memcpy(ent + 36, &hi, 4);

    /* xorshift to spread across the remaining bytes */
    r = lo ^ hi ^ (uint32_t)t;
    for (int i = 40; i < 48; i++) {
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        ent[i] = (uint8_t)(r & 0xFF);
    }

    br_ssl_engine_inject_entropy(eng, ent, sizeof ent);
}

int tls_init(void) {
    return 0;
}

tls_socket_t *tls_socket_create(const char *hostname) {
    (void)hostname;
    tls_socket_t *tls = (tls_socket_t *)kmalloc(sizeof(tls_socket_t));
    if (!tls) return NULL;

    tls->tcp_sock = NULL;
    tls->ssl_client = (br_ssl_client_context *)kmalloc(sizeof(br_ssl_client_context));
    if (!tls->ssl_client) {
        serial_print("[TLS] SSL client alloc failed\n");
        kfree(tls);
        return NULL;
    }

    /* X.509 engine storage */
    tls->ctx = kmalloc(sizeof(br_x509_minimal_context));
    if (!tls->ctx) {
        serial_print("[TLS] X509 alloc failed\n");
        kfree(tls->ssl_client);
        kfree(tls);
        return NULL;
    }

    /* Allocate I/O buffer from PHYSICAL allocator (kmalloc_ap) to ensure
     * it lives in a completely different memory region from the engine.
     * This prevents the BearSSL output ring from overwriting the engine struct. */
    uint64_t phys_dummy;
    tls->io_buf = (unsigned char *)kmalloc_ap(32768, &phys_dummy);
    if (!tls->io_buf) {
        serial_print("[TLS] I/O buffer alloc failed\n");
        kfree(tls->ctx);
        kfree(tls->ssl_client);
        kfree(tls);
        return NULL;
    }

    tls->connected = 0;

    return tls;
}

int tls_socket_connect(tls_socket_t *tls, uint32_t ip, uint16_t port, const char *sni) {
    if (!tls) return -1;

    tls->tcp_sock = tcp_connect(ip, port);
    if (!tls->tcp_sock) {
        serial_print("[TLS] TCP connect failed\n");
        return -1;
    }

    br_x509_minimal_context *xc = (br_x509_minimal_context *)tls->ctx;
    br_ssl_client_init_full(tls->ssl_client, xc, NULL, 0);
    xc->vtable = &tls_null_vtable;

    if (!tls->io_buf) {
        serial_print("[TLS] I/O buffer not allocated\n");
        tcp_close(tls->tcp_sock);
        tls->tcp_sock = NULL;
        return -1;
    }
    g_eng_buf = tls->io_buf;
    br_ssl_engine_set_buffer(&tls->ssl_client->eng,
        g_eng_buf, 32768, 1);

    tls_inject_entropy(&tls->ssl_client->eng);

    if (!br_ssl_client_reset(tls->ssl_client, sni, 0)) {
        serial_print("[TLS] client reset failed (no RNG?)\n");
        tcp_close(tls->tcp_sock);
        tls->tcp_sock = NULL;
        return -1;
    }

    /* Initialize simplified I/O context */
    g_eng = &tls->ssl_client->eng;
    g_eng_saved = br_ssl_engine_last_error(&tls->ssl_client->eng);
    br_sslio_init(&tls->ssio, &tls->ssl_client->eng,
                  tls_sock_read, tls->tcp_sock,
                  tls_sock_write, tls->tcp_sock);

    tls->connected = 1;
    return 0;
}

int tls_socket_read(tls_socket_t *tls, uint8_t *buf, uint32_t len) {
    if (!tls || !tls->connected) return -1;

    int r = br_sslio_read(&tls->ssio, buf, len);
    if (r < 0) {
        int err = br_ssl_engine_last_error(&tls->ssl_client->eng);
        serial_print("[TLS] read error, engine err=");
        {
            char num[16];
            itoa(err, num, 10);
            serial_print(num);
        }
        serial_print(" (0x");
        {
            char num[16];
            itoa(err, num, 16);
            serial_print(num);
        }
        serial_print(")\n");
    }
    return r;
}

int tls_socket_write(tls_socket_t *tls, const uint8_t *buf, uint32_t len) {
    if (!tls || !tls->connected) return -1;

    int ret = br_sslio_write_all(&tls->ssio, buf, len);
    if (ret < 0) {
        char num[16];
        serial_print("[TLS] write failed, engine err=");
        itoa(br_ssl_engine_last_error(&tls->ssl_client->eng), num, 10);
        serial_print(num);
        serial_print(" iomode=");
        itoa(tls->ssl_client->eng.iomode, num, 10);
        serial_print(num);
        serial_print(" alert=");
        itoa(tls->ssl_client->eng.alert, num, 10);
        serial_print(num);
        serial_print("\n");
    }
    return ret >= 0 ? (int)len : ret;
}

void tls_socket_close(tls_socket_t *tls) {
    if (!tls) return;

    if (tls->ssl_client) {
        /* ssl_client, ctx, and io_buf are a single combined allocation */
        kfree(tls->ssl_client);
    }
    if (tls->tcp_sock) {
        tcp_close(tls->tcp_sock);
    }
    kfree(tls);
}

int tls_socket_is_connected(tls_socket_t *tls) {
    return tls && tls->connected;
}