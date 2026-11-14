#include "tls_client.h"
#include "net_manager.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "kheap.h"
#include "vga.h"
#include "task.h"
#include "trust/ca_trust_anchors.h"

/* lwIP Headers */
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"

/* BearSSL Headers */
#include "bearssl.h"

#define TLS_RX_RING_SIZE (256 * 1024)
#define TLS_SESSION_CACHE_SLOTS 8
#define TLS_SESSION_HOST_MAX 256

static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & (1ULL << 9)) {
        __asm__ volatile("sti" ::: "memory");
    }
}

struct tls_connection {
    struct tcp_pcb *pcb;
    char server_name[TLS_SESSION_HOST_MAX];
    uint16_t port;
    volatile bool tcp_connected;
    volatile bool tcp_closed;
    volatile bool tcp_failed;
    volatile uint32_t read_diagnostics;

    uint8_t *rx_ring;
    size_t rx_head;
    size_t rx_tail;
    size_t rx_count;

    br_ssl_client_context sc;
    br_x509_minimal_context xc;
    br_sslio_context ioc;
    unsigned char iobuf[BR_SSL_BUFSIZE_BIDI];

    tls_log_fn log_cb;
    void *user_data;
};

typedef struct {
    bool used;
    char host[TLS_SESSION_HOST_MAX];
    uint16_t port;
    uint32_t last_used;
    br_ssl_session_parameters params;
} tls_session_cache_entry_t;

/* TLS 1.2 session resumption avoids repeating certificate validation and
 * public-key handshake work for each asset fetched from the same origin. */
static tls_session_cache_entry_t g_tls_session_cache[TLS_SESSION_CACHE_SLOTS];
static uint32_t g_tls_session_cache_clock;

static void tls_secure_zero(void *ptr, size_t len) {
    volatile unsigned char *p = (volatile unsigned char *)ptr;
    while (len--) *p++ = 0;
}

static bool tls_session_cache_load(const char *host, uint16_t port,
                                   br_ssl_session_parameters *out) {
    if (!host || !out || strlen(host) >= TLS_SESSION_HOST_MAX) return false;

    uint64_t flags = task_scheduler_lock();
    for (size_t i = 0; i < TLS_SESSION_CACHE_SLOTS; ++i) {
        tls_session_cache_entry_t *entry = &g_tls_session_cache[i];
        if (entry->used && entry->port == port && strcmp(entry->host, host) == 0) {
            *out = entry->params;
            entry->last_used = ++g_tls_session_cache_clock;
            task_scheduler_unlock(flags);
            return out->session_id_len != 0 && out->version != 0;
        }
    }
    task_scheduler_unlock(flags);
    return false;
}

static void tls_session_cache_store(const char *host, uint16_t port,
                                    const br_ssl_session_parameters *params) {
    if (!host || !params || strlen(host) >= TLS_SESSION_HOST_MAX ||
        params->session_id_len == 0 || params->session_id_len > sizeof(params->session_id) ||
        params->version == 0) return;

    uint64_t flags = task_scheduler_lock();
    size_t slot = TLS_SESSION_CACHE_SLOTS;
    uint32_t oldest = UINT32_MAX;
    for (size_t i = 0; i < TLS_SESSION_CACHE_SLOTS; ++i) {
        tls_session_cache_entry_t *entry = &g_tls_session_cache[i];
        if (entry->used && entry->port == port && strcmp(entry->host, host) == 0) {
            slot = i;
            break;
        }
        if (!entry->used) {
            slot = i;
            oldest = 0;
        } else if (oldest != 0 && entry->last_used < oldest) {
            oldest = entry->last_used;
            slot = i;
        }
    }
    if (slot < TLS_SESSION_CACHE_SLOTS) {
        tls_session_cache_entry_t *entry = &g_tls_session_cache[slot];
        tls_secure_zero(&entry->params, sizeof(entry->params));
        memset(entry->host, 0, sizeof(entry->host));
        strncpy(entry->host, host, sizeof(entry->host) - 1);
        entry->port = port;
        entry->params = *params;
        entry->last_used = ++g_tls_session_cache_clock;
        entry->used = true;
    }
    task_scheduler_unlock(flags);
}

static void tls_log(tls_connection_t *conn, const char *msg) {
    serial_printf("[TLS] %s\n", msg);
    if (conn && conn->log_cb) {
        conn->log_cb(msg, conn->user_data);
    }
}

/* Callbacks lwIP TCP */
static err_t tls_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)pcb;
    (void)err;
    tls_connection_t *conn = (tls_connection_t *)arg;
    if (!conn) return ERR_VAL;

    if (!p) {
        conn->tcp_closed = true;
        return ERR_OK;
    }

    if (conn->rx_count + p->tot_len > TLS_RX_RING_SIZE) {
        conn->read_diagnostics |= TLS_READ_DIAG_RX_OVERFLOW;
        return ERR_MEM;
    }

    uint64_t flags = irq_save();
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        const uint8_t *src = (const uint8_t *)q->payload;
        size_t rem = q->len;
        while (rem > 0) {
            size_t space_to_end = TLS_RX_RING_SIZE - conn->rx_head;
            size_t copy_n = (rem < space_to_end) ? rem : space_to_end;
            memcpy(conn->rx_ring + conn->rx_head, src, copy_n);
            conn->rx_head = (conn->rx_head + copy_n) % TLS_RX_RING_SIZE;
            conn->rx_count += copy_n;
            src += copy_n;
            rem -= copy_n;
        }
    }
    irq_restore(flags);

    pbuf_free(p);
    return ERR_OK;
}

static err_t tls_connect_cb(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)pcb;
    (void)err;
    tls_connection_t *conn = (tls_connection_t *)arg;
    if (conn) conn->tcp_connected = true;
    return ERR_OK;
}

static void tls_err_cb(void *arg, err_t err) {
    (void)err;
    tls_connection_t *conn = (tls_connection_t *)arg;
    if (conn) {
        conn->pcb = NULL;
        conn->tcp_failed = true;
        conn->read_diagnostics |= TLS_READ_DIAG_TCP_ERROR;
    }
}

/* Callbacks de I/O em baixo nivel para o BearSSL */
static int tls_sock_read(void *ctx, unsigned char *buf, size_t len) {
    tls_connection_t *conn = (tls_connection_t *)ctx;
    if (!conn || !conn->rx_ring) return -1;

    uint32_t start = timer_ticks;
    while (conn->rx_count == 0) {
        if (conn->tcp_closed) {
            conn->read_diagnostics |= TLS_READ_DIAG_PEER_CLOSED;
            return -1;
        }
        if (conn->tcp_failed) {
            conn->read_diagnostics |= TLS_READ_DIAG_TCP_ERROR;
            return -1;
        }
        if (timer_ticks - start > 800) { /* 8s timeout */
            conn->read_diagnostics |= TLS_READ_DIAG_TIMEOUT;
            return -1;
        }
        net_poll();
        task_sleep_ms(2);
    }

    uint64_t flags = irq_save();
    size_t to_read = (len < conn->rx_count) ? len : conn->rx_count;
    for (size_t i = 0; i < to_read; i++) {
        buf[i] = conn->rx_ring[conn->rx_tail];
        conn->rx_tail = (conn->rx_tail + 1) % TLS_RX_RING_SIZE;
    }
    conn->rx_count -= to_read;
    irq_restore(flags);

    /* Advertise receive-window space only after BearSSL has taken bytes out
     * of our bounded ring. Acknowledging them when merely copied into the
     * ring lets a fast peer overrun the consumer and stall on ERR_MEM. */
    if (to_read && conn->pcb && !conn->tcp_failed) {
        uint64_t net_flags = net_lock_acquire();
        if (conn->pcb && !conn->tcp_failed)
            tcp_recved(conn->pcb, (u16_t)to_read);
        net_lock_release(net_flags);
    }

    return (int)to_read;
}

static int tls_sock_write(void *ctx, const unsigned char *buf, size_t len) {
    tls_connection_t *conn = (tls_connection_t *)ctx;
    if (!conn || !conn->pcb || conn->tcp_closed || conn->tcp_failed) {
        return -1;
    }

    size_t sent = 0;
    while (sent < len) {
        if (conn->tcp_closed || conn->tcp_failed) {
            return -1;
        }

        uint64_t flags = net_lock_acquire();
        size_t chunk = len - sent;
        u16_t snd_buf = tcp_sndbuf(conn->pcb);
        if (chunk > snd_buf) chunk = snd_buf;
        if (chunk == 0) {
            tcp_output(conn->pcb);
            net_lock_release(flags);
            net_poll();
            task_sleep_ms(2);
            continue;
        }

        err_t err = tcp_write(conn->pcb, buf + sent, (u16_t)chunk, TCP_WRITE_FLAG_COPY);
        if (err != ERR_OK) {
            net_lock_release(flags);
            return -1;
        }
        sent += chunk;
        tcp_output(conn->pcb);
        net_lock_release(flags);

        net_poll();
    }

    return (int)len;
}

/* Injeta entropia de hardware (TSC + RDRAND + ticks) */
static void tls_seed_entropy(br_ssl_engine_context *eng) {
    uint64_t seed[8];
    for (int i = 0; i < 8; i++) {
        uint32_t lo, hi;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
        uint64_t val = ((uint64_t)hi << 32) | lo;

        uint32_t eax, ebx, ecx, edx;
        __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        if (ecx & (1 << 30)) { /* RDRAND suportado pela CPU */
            uint64_t rnd;
            unsigned char ok;
            __asm__ volatile("rdrand %0; setc %1" : "=r"(rnd), "=qm"(ok));
            if (ok) val ^= rnd;
        }
        val ^= ((uint64_t)timer_ticks << 16) ^ ((uint64_t)i * 0x9E3779B97F4A7C15ULL);
        seed[i] = val;
    }
    br_ssl_engine_inject_entropy(eng, seed, sizeof(seed));
}

tls_connection_t *tls_connect(const char *host, uint16_t port,
                              uint32_t timeout_ms, tls_log_fn log_cb, void *user_data) {
    if (!host || strlen(host) >= TLS_SESSION_HOST_MAX || !net_is_active()) return NULL;

    tls_connection_t *conn = kmalloc(sizeof(tls_connection_t));
    if (!conn) return NULL;
    memset(conn, 0, sizeof(tls_connection_t));

    conn->log_cb = log_cb;
    conn->user_data = user_data;
    strncpy(conn->server_name, host, sizeof(conn->server_name) - 1);
    conn->port = port;

    conn->rx_ring = kmalloc(TLS_RX_RING_SIZE);
    if (!conn->rx_ring) {
        kfree(conn);
        return NULL;
    }

    char resolved_ip[32];
    if (net_resolve_host(host, resolved_ip, sizeof(resolved_ip)) != 0) {
        tls_log(conn, "Falha na resolucao de DNS");
        kfree(conn->rx_ring);
        kfree(conn);
        return NULL;
    }

    ip4_addr_t target_ip;
    if (!ip4addr_aton(resolved_ip, &target_ip)) {
        tls_log(conn, "Endereco IP invalido");
        kfree(conn->rx_ring);
        kfree(conn);
        return NULL;
    }

    uint64_t lflags = net_lock_acquire();
    conn->pcb = tcp_new();
    if (!conn->pcb) {
        net_lock_release(lflags);
        tls_log(conn, "Falha ao alocar PCB TCP");
        kfree(conn->rx_ring);
        kfree(conn);
        return NULL;
    }

    tcp_arg(conn->pcb, conn);
    tcp_recv(conn->pcb, tls_recv_cb);
    tcp_err(conn->pcb, tls_err_cb);

    err_t connect_err = tcp_connect(conn->pcb, (ip_addr_t *)&target_ip, port, tls_connect_cb);
    net_lock_release(lflags);

    if (connect_err != ERR_OK) {
        tls_log(conn, "Falha ao iniciar conexao TCP");
        tls_close(conn);
        return NULL;
    }

    uint32_t ticks_timeout = (timeout_ms / 10);
    if (ticks_timeout < 100) ticks_timeout = 100;
    uint32_t end_tick = timer_ticks + ticks_timeout;

    while (!conn->tcp_connected && !conn->tcp_failed && timer_ticks < end_tick) {
        net_poll();
        task_sleep_ms(2);
    }

    if (!conn->tcp_connected || conn->tcp_failed) {
        tls_log(conn, "Timeout ou erro ao conectar TCP");
        tls_close(conn);
        return NULL;
    }

    tls_log(conn, "TCP estabelecido. Negociando TLS Handshake...");

    /* Chain, validity period, and hostname verification are mandatory. */
    br_ssl_client_init_full(&conn->sc, &conn->xc,
                            liwus_trust_anchors, liwus_trust_anchors_count);

    /* Define buffer de comunicacao bidirecional */
    br_ssl_engine_set_buffer(&conn->sc.eng, conn->iobuf, sizeof(conn->iobuf), 1);

    /* Injeta entropia fisica de chaves */
    tls_seed_entropy(&conn->sc.eng);

    br_ssl_session_parameters cached_session;
    memset(&cached_session, 0, sizeof(cached_session));
    bool resume_session = tls_session_cache_load(host, port, &cached_session);
    if (resume_session) {
        br_ssl_engine_set_session_parameters(&conn->sc.eng, &cached_session);
    }
    tls_secure_zero(&cached_session, sizeof(cached_session));

    /* Reinicia cliente com o hostname alvo para inclusao de extensao SNI */
    if (!br_ssl_client_reset(&conn->sc, host, resume_session ? 1 : 0)) {
        tls_log(conn, "Falha ao preparar handshake TLS");
        tls_close(conn);
        return NULL;
    }

    /* Inicializa I/O BearSSL sobre os callbacks lwIP */
    br_sslio_init(&conn->ioc, &conn->sc.eng, tls_sock_read, conn, tls_sock_write, conn);

    return conn;
}

int tls_write(tls_connection_t *conn, const void *data, size_t len) {
    if (!conn || !data || len == 0) return -1;
    if (br_sslio_write_all(&conn->ioc, data, len) < 0) {
        return -1;
    }
    if (br_sslio_flush(&conn->ioc) < 0) {
        return -1;
    }
    return (int)len;
}

int tls_read(tls_connection_t *conn, void *buf, size_t len, uint32_t timeout_ms) {
    (void)timeout_ms;
    if (!conn || !buf || len == 0) return -1;
    return br_sslio_read(&conn->ioc, buf, len);
}

int tls_last_error(tls_connection_t *conn) {
    if (!conn) return -1;
    return br_ssl_engine_last_error(&conn->sc.eng);
}

uint32_t tls_read_diagnostics(tls_connection_t *conn) {
    return conn ? conn->read_diagnostics : 0;
}

const char *tls_strerror(int err) {
    switch (err) {
        case BR_ERR_OK: return "OK";
        case BR_ERR_BAD_PARAM: return "Parametro invalido";
        case BR_ERR_BAD_STATE: return "Estado invalido";
        case BR_ERR_UNSUPPORTED_VERSION: return "Versao TLS nao suportada";
        case BR_ERR_BAD_VERSION: return "Versao de protocolo invalida";
        case BR_ERR_BAD_LENGTH: return "Tamanho de registro invalido";
        case BR_ERR_TOO_LARGE: return "Registro TLS muito grande";
        case BR_ERR_BAD_MAC: return "Falha de integridade MAC";
        case BR_ERR_NO_RANDOM: return "Gerador de numeros aleatorios nao inicializado";
        case BR_ERR_UNKNOWN_TYPE: return "Tipo de registro desconhecido";
        case BR_ERR_UNEXPECTED: return "Mensagem TLS inesperada";
        case BR_ERR_BAD_CCS: return "ChangeCipherSpec invalido";
        case BR_ERR_BAD_ALERT: return "Alerta TLS corrompido";
        case BR_ERR_BAD_HANDSHAKE: return "Handshake TLS invalido";
        case BR_ERR_OVERSIZED_ID: return "Session ID muito longo";
        case BR_ERR_BAD_CIPHER_SUITE: return "Cipher suite invalida";
        case BR_ERR_BAD_COMPRESSION: return "Metodo de compressao invalido";
        case BR_ERR_BAD_FRAGLEN: return "Tamanho de fragmento invalido";
        case BR_ERR_BAD_SECRENEG: return "Renegociacao segura invalida";
        case BR_ERR_EXTRA_EXTENSION: return "Extensao TLS duplicada";
        case BR_ERR_BAD_SNI: return "SNI invalido";
        case BR_ERR_BAD_HELLO_DONE: return "ServerHelloDone invalido";
        case BR_ERR_LIMIT_EXCEEDED: return "Limite de chave ou conexao excedido";
        case BR_ERR_BAD_FINISHED: return "Mensagem Finished com hash invalido";
        case BR_ERR_RESUME_MISMATCH: return "Falha na recuperacao de sessao";
        case BR_ERR_INVALID_ALGORITHM: return "Algoritmo criptografico nao suportado";
        case BR_ERR_BAD_SIGNATURE: return "Assinatura digital invalida";
        case BR_ERR_WRONG_KEY_USAGE: return "Uso de chave nao permitido";
        case BR_ERR_NO_CLIENT_AUTH: return "Autenticacao de cliente requerida";
        case BR_ERR_IO: return "Erro de I/O na conexao de rede";
        case BR_ERR_X509_INVALID_VALUE: return "X509: Valor ASN.1 invalido";
        case BR_ERR_X509_TRUNCATED: return "X509: Certificado truncado";
        case BR_ERR_X509_EMPTY_CHAIN: return "X509: Cadeia de certificados vazia";
        case BR_ERR_X509_NOT_TRUSTED: return "X509: Certificado nao confiavel / sem root CA";
        case BR_ERR_X509_TIME_UNKNOWN: return "X509: Data/hora desconhecida";
        case BR_ERR_X509_EXPIRED: return "X509: Certificado expirado";
        case BR_ERR_X509_BAD_SERVER_NAME: return "X509: Nome de servidor incompativel";
        default:
            if (err >= BR_ERR_RECV_FATAL_ALERT && err < BR_ERR_RECV_FATAL_ALERT + 256) {
                return "Servidor enviou Alerta Fatal TLS";
            }
            if (err >= BR_ERR_SEND_FATAL_ALERT && err < BR_ERR_SEND_FATAL_ALERT + 256) {
                return "Cliente enviou Alerta Fatal TLS";
            }
            return "Erro TLS desconhecido";
    }
}

void tls_close(tls_connection_t *conn) {
    if (!conn) return;

    br_ssl_session_parameters session;
    br_ssl_engine_get_session_parameters(&conn->sc.eng, &session);
    if (session.session_id_len != 0 && session.version != 0) {
        /* The session is keyed by the authenticated SNI hostname and port. */
        tls_session_cache_store(conn->server_name, conn->port, &session);
    }
    tls_secure_zero(&session, sizeof(session));

    uint64_t flags = net_lock_acquire();
    if (conn->pcb) {
        tcp_arg(conn->pcb, NULL);
        tcp_recv(conn->pcb, NULL);
        tcp_err(conn->pcb, NULL);
        tcp_sent(conn->pcb, NULL);
        tcp_poll(conn->pcb, NULL, 0);
        if (tcp_close(conn->pcb) != ERR_OK) {
            tcp_abort(conn->pcb);
        }
        conn->pcb = NULL;
    }
    net_lock_release(flags);

    if (conn->rx_ring) {
        kfree(conn->rx_ring);
        conn->rx_ring = NULL;
    }
    kfree(conn);
}
