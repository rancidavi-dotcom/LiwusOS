#include "http_client.h"
#include "tls_client.h"
#include "net_manager.h"
#include "serial.h"
#include "string.h"
#include "timer.h"
#include "kheap.h"
#include "sdfs.h"
#include "vga.h"
#include "task.h"

extern void int_to_str(uint32_t n, char *s);

/* lwIP Headers */
#include "lwip/tcp.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"

#define HTTP_SCRATCH_SIZE 16384
#define HTTP_INIT_BODY_CAPACITY (64 * 1024)
#define HTTP_TLS_CHUNK_SIZE 4096
#define HTTP_URL_MAX 1024U
#define HTTP_PATH_MAX (HTTP_URL_MAX * 3U + 1U)

typedef struct {
    struct tcp_pcb * volatile pcb;
    volatile bool connected;
    volatile bool finished;
    volatile bool failed;
    int status_code;
    bool headers_done;
    bool is_chunked;
    bool body_too_large;
    int content_length;
    size_t body_limit;
    uint32_t hdr_window;
    char location[HTTP_URL_MAX];
    uint8_t *body_buf;
    size_t body_size;
    size_t body_capacity;
    char *scratch;
    size_t scratch_len;
    http_progress_fn progress_cb;
    void *progress_data;
} http_download_ctx_t;

static char ascii_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static const char *str_casestr_local(const char *hay, const char *needle) {
    if (!hay || !needle || !*needle) return hay;
    for (const char *h = hay; *h; h++) {
        size_t i = 0;
        while (needle[i] && h[i] && ascii_tolower(h[i]) == ascii_tolower(needle[i])) {
            i++;
        }
        if (!needle[i]) return h;
    }
    return NULL;
}

static bool extract_header_value(const char *headers, const char *name, char *out, size_t out_sz) {
    if (!headers || !name || !out || out_sz == 0) return false;
    out[0] = '\0';
    size_t nlen = strlen(name);
    const char *p = headers;
    while (*p) {
        const char *line_end = strstr(p, "\n");
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);
        if (line_len > nlen + 1) {
            bool match = true;
            for (size_t i = 0; i < nlen; i++) {
                if (ascii_tolower(p[i]) != ascii_tolower(name[i])) {
                    match = false;
                    break;
                }
            }
            if (match && p[nlen] == ':') {
                const char *val = p + nlen + 1;
                while (*val == ' ' || *val == '\t') val++;
                size_t copy = 0;
                while (val + copy < p + line_len && val[copy] != '\r' && val[copy] != '\n' && copy < out_sz - 1) {
                    out[copy] = val[copy];
                    copy++;
                }
                while (copy > 0 && (out[copy - 1] == ' ' || out[copy - 1] == '\t')) copy--;
                out[copy] = '\0';
                return true;
            }
        }
        if (!line_end) break;
        p = line_end + 1;
    }
    return false;
}

static void parse_http_headers(const char *scratch, size_t scratch_len,
                               int *out_status, int *out_content_len,
                               char *out_location, size_t loc_sz, bool *out_chunked) {
    if (out_status) {
        *out_status = 0;
        const char *status_pos = str_casestr_local(scratch, "HTTP/1.");
        if (status_pos && (size_t)((status_pos + 9) - scratch) < scratch_len) {
            *out_status = atoi(status_pos + 9);
        }
    }
    if (out_content_len) {
        char cl_buf[32];
        if (extract_header_value(scratch, "Content-Length", cl_buf, sizeof(cl_buf))) {
            *out_content_len = atoi(cl_buf);
        }
    }
    if (out_location && loc_sz > 0) {
        extract_header_value(scratch, "Location", out_location, loc_sz);
    }
    if (out_chunked) {
        char te_buf[64];
        *out_chunked = false;
        if (extract_header_value(scratch, "Transfer-Encoding", te_buf, sizeof(te_buf)) &&
            str_casestr_local(te_buf, "chunked") != NULL) {
            *out_chunked = true;
        }
    }
}

static void resolve_redirect_url(bool is_tls, const char *host, uint16_t port,
                                 const char *loc, char *out, size_t out_sz) {
    if (!loc || !out || out_sz == 0) return;
    out[0] = '\0';
    if (strstr(loc, "://") != NULL) {
        strncpy(out, loc, out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }
    const char *scheme = is_tls ? "https://" : "http://";
    if (loc[0] == '/' && loc[1] == '/') {
        strncpy(out, is_tls ? "https:" : "http:", out_sz - 1);
        out[out_sz - 1] = '\0';
        size_t cur = strlen(out);
        strncpy(out + cur, loc, out_sz - 1 - cur);
        out[out_sz - 1] = '\0';
        return;
    }
    strncpy(out, scheme, out_sz - 1);
    out[out_sz - 1] = '\0';
    size_t pos = strlen(out);
    if (pos < out_sz - 1) {
        strncpy(out + pos, host, out_sz - 1 - pos);
        out[out_sz - 1] = '\0';
        pos = strlen(out);
    }
    if ((!is_tls && port != 80) || (is_tls && port != 443)) {
        char pbuf[16];
        pbuf[0] = ':';
        int_to_str(port, pbuf + 1);
        if (pos < out_sz - 1) {
            strncpy(out + pos, pbuf, out_sz - 1 - pos);
            out[out_sz - 1] = '\0';
            pos = strlen(out);
        }
    }
    if (loc[0] != '/' && pos < out_sz - 1) {
        out[pos++] = '/';
        out[pos] = '\0';
    }
    if (pos < out_sz - 1) {
        strncpy(out + pos, loc, out_sz - 1 - pos);
        out[out_sz - 1] = '\0';
    }
}

static void parse_http_headers(const char *scratch, size_t scratch_len,
                               int *out_status, int *out_content_len,
                               char *out_location, size_t loc_sz, bool *out_chunked);

/* Acumula cabecalhos HTTP e detecta o delimitador \r\n\r\n mesmo que exceda HTTP_SCRATCH_SIZE */
static bool feed_header_byte(uint8_t b, char *scratch, size_t *scratch_len,
                             uint32_t *hdr_window, int *status_code,
                             int *content_length, char *location, size_t loc_sz,
                             bool *is_chunked) {
    if (*scratch_len < (HTTP_SCRATCH_SIZE - 1)) {
        scratch[(*scratch_len)++] = (char)b;
        scratch[*scratch_len] = '\0';
    }
    *hdr_window = (*hdr_window << 8) | b;
    if (*hdr_window == 0x0D0A0D0AUL) {
        parse_http_headers(scratch, *scratch_len,
                           status_code, content_length,
                           location, loc_sz, is_chunked);
        return true;
    }
    return false;
}

static void http_log(http_output_fn output_cb, void *user_data, const char *msg) {
    serial_printf("[HTTP] %s\n", msg);
    if (output_cb) {
        output_cb(msg, user_data);
    }
}

static err_t http_recv_cb(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err) {
    (void)err;
    http_download_ctx_t *ctx = (http_download_ctx_t *)arg;
    if (!ctx) return ERR_VAL;

    if (!p) {
        /* Conexao finalizada pelo servidor remoto */
        ctx->finished = true;
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);

    u16_t offset = 0;

    /* 1. Enquanto nao encontramos o fim dos headers HTTP (\r\n\r\n) */
    if (!ctx->headers_done) {
        while (offset < p->tot_len && !ctx->headers_done) {
            uint8_t b = pbuf_get_at(p, offset++);
            if (feed_header_byte(b, ctx->scratch, &ctx->scratch_len,
                                 &ctx->hdr_window, &ctx->status_code,
                                 &ctx->content_length, ctx->location,
                                 sizeof(ctx->location), &ctx->is_chunked)) {
                ctx->headers_done = true;
                break;
            }
        }
    }

    /* 2. Processa o restante dos bytes como conteudo do corpo (body) */
    if (ctx->headers_done && ctx->body_limit && ctx->content_length > 0 &&
        (size_t)ctx->content_length > ctx->body_limit) {
        ctx->body_too_large = true;
        ctx->failed = true;
        pbuf_free(p);
        return ERR_MEM;
    }
    if (ctx->headers_done && offset < p->tot_len) {
        size_t remaining = p->tot_len - offset;

        if (ctx->body_limit &&
            (ctx->body_size > ctx->body_limit ||
             remaining > ctx->body_limit - ctx->body_size)) {
            ctx->body_too_large = true;
            ctx->failed = true;
            pbuf_free(p);
            return ERR_MEM;
        }

        while (ctx->body_size + remaining + 1 > ctx->body_capacity) {
            size_t new_cap = ctx->body_capacity * 2;
            if (new_cap < 32 * 1024) new_cap = 32 * 1024;
            uint8_t *new_buf = krealloc(ctx->body_buf, new_cap);
            if (!new_buf) {
                ctx->failed = true;
                pbuf_free(p);
                return ERR_MEM;
            }
            ctx->body_buf = new_buf;
            ctx->body_capacity = new_cap;
        }

        pbuf_copy_partial(p, ctx->body_buf + ctx->body_size, (u16_t)remaining, offset);
        ctx->body_size += remaining;
        ctx->body_buf[ctx->body_size] = '\0';
        if (ctx->progress_cb) {
            size_t total = ctx->content_length > 0 ? (size_t)ctx->content_length : 0;
            ctx->progress_cb(ctx->body_size, total, ctx->progress_data);
        }
    }

    pbuf_free(p);
    return ERR_OK;
}

static err_t http_connect_cb(void *arg, struct tcp_pcb *pcb, err_t err) {
    (void)pcb;
    (void)err;
    http_download_ctx_t *ctx = (http_download_ctx_t *)arg;
    if (ctx) {
        ctx->connected = true;
    }
    return ERR_OK;
}

static void http_err_cb(void *arg, err_t err) {
    (void)err;
    http_download_ctx_t *ctx = (http_download_ctx_t *)arg;
    if (ctx) {
        ctx->pcb = NULL;
        ctx->failed = true;
    }
}

static void http_close_pcb_locked(http_download_ctx_t *ctx) {
    if (!ctx || !ctx->pcb) return;
    struct tcp_pcb *pcb = ctx->pcb;
    ctx->pcb = NULL;
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    if (tcp_close(pcb) != ERR_OK) {
        tcp_abort(pcb);
    }
}

static void tls_log_bridge(const char *msg, void *user_data) {
    (void)user_data;
    serial_printf("[TLS] %s\n", msg);
}

/* Decodifica Transfer-Encoding: chunked caso a resposta HTTP utilize chunks */
static size_t http_unchunk(uint8_t *buf, size_t size) {
    if (!buf || size == 0) return 0;

    uint8_t *src = buf;
    uint8_t *end = buf + size;

    /* Pula quebras de linha iniciais se houver */
    while (src < end && (*src == '\r' || *src == '\n')) src++;
    if (src >= end) return size;

    /* Se comeca direto com '<', ja e markup e nao esta empacotado em chunk */
    if (*src == '<') return size;

    /* Valida se os primeiros caracteres sao hex seguidos de \r, \n ou ; */
    uint8_t *probe = src;
    size_t probe_len = 0;
    bool looks_like_chunk = false;
    while (probe < end && ((*probe >= '0' && *probe <= '9') ||
                           (*probe >= 'a' && *probe <= 'f') ||
                           (*probe >= 'A' && *probe <= 'F'))) {
        probe++;
        probe_len++;
    }
    if (probe_len > 0 && probe < end && (*probe == '\r' || *probe == '\n' || *probe == ';')) {
        looks_like_chunk = true;
    }

    if (!looks_like_chunk) return size;

    uint8_t *dst = buf;
    while (src < end) {
        while (src < end && (*src == '\r' || *src == '\n')) src++;
        if (src >= end) break;

        size_t chunk_len = 0;
        bool has_hex = false;
        while (src < end) {
            char c = *src;
            int digit = -1;
            if (c >= '0' && c <= '9') digit = c - '0';
            else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
            else break;
            chunk_len = (chunk_len << 4) | digit;
            has_hex = true;
            src++;
        }

        if (!has_hex) {
            while (src < end) *dst++ = *src++;
            break;
        }

        while (src < end && *src != '\n') src++;
        if (src < end && *src == '\n') src++;

        if (chunk_len == 0) {
            break;
        }

        size_t to_copy = chunk_len;
        if (src + to_copy > end) to_copy = (size_t)(end - src);
        memmove(dst, src, to_copy);
        dst += to_copy;
        src += to_copy;

        if (src < end && *src == '\r') src++;
        if (src < end && *src == '\n') src++;
    }

    if (dst < buf + size) *dst = '\0';
    return (size_t)(dst - buf);
}

/*
 * Executa uma requisicao HTTP ou HTTPS GET genérica.
 * Se dest_path != NULL: grava no SDFS e nao retorna buffer em memoria.
 * Se out_body != NULL: retorna ponteiro alocado com kmalloc (chamador deve liberar com kfree).
 */
static int http_perform_request(const char *url, uint8_t **out_body, size_t *out_size,
                                int *out_status, const char *dest_path,
                                http_output_fn output_cb, void *user_data,
                                http_progress_fn progress_cb, void *progress_data,
                                size_t body_limit) {
    if (!url || !net_is_active()) {
        http_log(output_cb, user_data, "Rede inativa ou URL vazia");
        return -1;
    }

    if (strlen(url) >= HTTP_URL_MAX) {
        http_log(output_cb, user_data, "URL excede o limite de 1023 caracteres");
        return -1;
    }
    char active_url[HTTP_URL_MAX];
    strncpy(active_url, url, sizeof(active_url) - 1);
    active_url[sizeof(active_url) - 1] = '\0';

    char log_buf[256];
    char num[16];
    char final_dest[256];
    final_dest[0] = '\0';

    uint8_t *result_body = NULL;
    size_t result_size = 0;
    int status_code = 0;

    for (int redirect_hop = 0; redirect_hop < 4; redirect_hop++) {
        /* 1. Parse robusto da URL (suporta https://, http://, htts://, htps://, esquemas tolerantes e urls sem esquema) */
        bool is_tls = false;
        uint16_t port = 80;
        const char *p = active_url;

        /* Pula espacos iniciais */
        while (*p == ' ') p++;

    /* Verifica se ha indicador de esquema '://' */
    const char *scheme_sep = strstr(p, "://");
    if (scheme_sep) {
        size_t slen = (size_t)(scheme_sep - p);
        char scheme[16];
        if (slen >= sizeof(scheme)) slen = sizeof(scheme) - 1;
        strncpy(scheme, p, slen);
        scheme[slen] = '\0';

        /* Se tiver 's' ou 'S' no esquema (ex: https, htts, htps), assume TLS */
        if (strchr(scheme, 's') != NULL || strchr(scheme, 'S') != NULL) {
            is_tls = true;
            port = 443;
        } else {
            is_tls = false;
            port = 80;
        }
        p = scheme_sep + 3;
    } else if (strncmp(p, "//", 2) == 0) {
        p += 2;
    }

    /* Pula eventuais barras extras (ex: https:///host) */
    while (*p == '/') p++;

    char host[128];
    char port_str[8];
    char path[HTTP_PATH_MAX];

    size_t hi = 0;
    while (*p && *p != ':' && *p != '/' && *p != '?' && *p != ' ' && hi < sizeof(host) - 1) {
        host[hi++] = *p++;
    }
    host[hi] = '\0';

    if (hi == 0) {
        http_log(output_cb, user_data, "Nome de host invalido na URL");
        return -1;
    }

    if (*p == ':') {
        p++;
        size_t pi = 0;
        while (*p >= '0' && *p <= '9' && pi < sizeof(port_str) - 1) {
            port_str[pi++] = *p++;
        }
        port_str[pi] = '\0';
        if (pi > 0) {
            int pt = atoi(port_str);
            if (pt > 0 && pt <= 65535) {
                port = (uint16_t)pt;
                if (port == 443) is_tls = true;
            }
        }
    }

    /* Pula espacos antes do path */
    while (*p == ' ') p++;

    /* Calcula fim util sem espacos a direita para nao vazar %20 no final */
    size_t raw_len = strlen(p);
    while (raw_len > 0 && (p[raw_len - 1] == ' ' || p[raw_len - 1] == '\t' ||
                           p[raw_len - 1] == '\r' || p[raw_len - 1] == '\n')) {
        raw_len--;
    }

    if (raw_len > 0 && (*p == '/' || *p == '?')) {
        size_t pai = 0;
        if (*p == '?') {
            path[pai++] = '/';
        }
        for (size_t idx = 0; idx < raw_len; idx++) {
            char ch = p[idx];
            if (ch == ' ') {
                if (pai + 3 >= sizeof(path)) break;
                path[pai++] = '%';
                path[pai++] = '2';
                path[pai++] = '0';
            } else {
                if (pai + 1 >= sizeof(path)) break;
                path[pai++] = ch;
            }
        }
        path[pai] = '\0';
    } else {
        strcpy(path, "/");
    }

        /* Determina caminho de destino apenas na primeira tentativa */
        if (redirect_hop == 0 && dest_path) {
            if (dest_path[0] != '\0') {
                strncpy(final_dest, dest_path, sizeof(final_dest) - 1);
                final_dest[sizeof(final_dest) - 1] = '\0';
            } else {
                const char *last_slash = strrchr(path, '/');
                if (last_slash && *(last_slash + 1) != '\0') {
                    final_dest[0] = '/';
                    strncpy(final_dest + 1, last_slash + 1, sizeof(final_dest) - 2);
                    final_dest[sizeof(final_dest) - 1] = '\0';
                } else {
                    strcpy(final_dest, "/download.bin");
                }
            }
        }

        result_body = NULL;
        result_size = 0;
        status_code = 0;
        bool is_chunked = false;
        char location[HTTP_URL_MAX];
        location[0] = '\0';

        /* -------------------------------------------------------------
         * FLUXO HTTPS (TLS sobre BearSSL)
         * ------------------------------------------------------------- */
        if (is_tls) {
            strcpy(log_buf, "Conectando TLS em ");
            strcat(log_buf, host);
            strcat(log_buf, ":");
            int_to_str(port, num);
            strcat(log_buf, num);
            strcat(log_buf, "...");
            http_log(output_cb, user_data, log_buf);

            tls_connection_t *tls = tls_connect(host, port, 10000,
                                                tls_log_bridge, NULL);
            if (!tls) {
                http_log(output_cb, user_data, "Falha na conexao segura TLS");
                return -6;
            }

            http_log(output_cb, user_data, "Conexao TLS ativa! Enviando requisicao HTTP...");
            serial_printf("[HTTP] Iniciando tls_write da requisicao GET %s...\n", path);

            char req[HTTP_PATH_MAX + 256U];
            if (strlen(path) + strlen(host) + 128 >= sizeof(req)) {
                tls_close(tls);
                http_log(output_cb, user_data, "URL muito longa para requisicao HTTP");
                return -1;
            }
            req[0] = '\0';
            strcat(req, "GET ");
            strcat(req, path);
            strcat(req, " HTTP/1.1\r\nHost: ");
            strcat(req, host);
            strcat(req, "\r\nUser-Agent: LiwusOS/2.0 Web (freestanding)\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");

            if (tls_write(tls, req, strlen(req)) < 0) {
                int err = tls_last_error(tls);
                const char *err_str = tls_strerror(err);
                char emsg[160];
                strcpy(emsg, "Falha TLS (erro ");
                char nbuf[16];
                int_to_str(err, nbuf);
                strcat(emsg, nbuf);
                strcat(emsg, ": ");
                strcat(emsg, err_str);
                strcat(emsg, ")");
                http_log(output_cb, user_data, emsg);
                tls_close(tls);
                return -7;
            }

            serial_printf("[HTTP] tls_write OK! Aguardando resposta HTTP...\n");

            size_t body_cap = HTTP_INIT_BODY_CAPACITY;
            result_body = kmalloc(body_cap);
            if (!result_body) {
                tls_close(tls);
                return -4;
            }
            result_body[0] = '\0';

            char *scratch = kmalloc(HTTP_SCRATCH_SIZE);
            uint8_t *chunk = kmalloc(HTTP_TLS_CHUNK_SIZE);
            if (!scratch || !chunk) {
                if (scratch) kfree(scratch);
                if (chunk) kfree(chunk);
                kfree(result_body);
                tls_close(tls);
                return -4;
            }

            size_t scratch_len = 0;
            uint32_t hdr_window = 0;
            bool headers_done = false;
            int content_length = -1;

            while (1) {
                int rlen = tls_read(tls, chunk, HTTP_TLS_CHUNK_SIZE, 8000);
                if (rlen <= 0) break;

                size_t offset = 0;
                if (!headers_done) {
                    while (offset < (size_t)rlen && !headers_done) {
                        uint8_t b = chunk[offset++];
                        if (feed_header_byte(b, scratch, &scratch_len,
                                             &hdr_window, &status_code,
                                             &content_length, location,
                                             sizeof(location), &is_chunked)) {
                            headers_done = true;
                            break;
                        }
                    }
                }

                if (headers_done && offset < (size_t)rlen) {
                    size_t remaining = (size_t)rlen - offset;
                    if (body_limit &&
                        (result_size > body_limit ||
                         remaining > body_limit - result_size)) {
                        kfree(scratch);
                        kfree(chunk);
                        kfree(result_body);
                        tls_close(tls);
                        if (out_status) *out_status = status_code;
                        return -9;
                    }
                    while (result_size + remaining + 1 > body_cap) {
                        body_cap *= 2;
                        uint8_t *new_buf = krealloc(result_body, body_cap);
                        if (!new_buf) {
                            kfree(scratch);
                            kfree(chunk);
                            kfree(result_body);
                            tls_close(tls);
                            return -4;
                        }
                        result_body = new_buf;
                    }
                    memcpy(result_body + result_size, chunk + offset, remaining);
                    result_size += remaining;
                    result_body[result_size] = '\0';
                    if (progress_cb) {
                        size_t total = content_length > 0 ? (size_t)content_length : 0;
                        progress_cb(result_size, total, progress_data);
                    }
                }

                if (headers_done && content_length >= 0 && (int)result_size >= content_length) {
                    break;
                }
            }

            if (result_body && result_size > 0 && is_chunked) {
                result_size = http_unchunk(result_body, result_size);
                result_body[result_size] = '\0';
            }

            /* TLS can report EOF after only part of the response. Do not pass
             * a truncated minified bundle to HTML/JavaScript as if it were
             * complete; that turns transport failures into misleading parse
             * errors. Chunked responses use their own framing. */
            bool incomplete_body = !is_chunked && content_length >= 0 &&
                result_size != (size_t)content_length;
            uint32_t tls_read_diag = tls_read_diagnostics(tls);

            int tls_err = tls_last_error(tls);
            serial_printf("[HTTP] Leitura concluida: status=%d hdr=%u size=%u chunked=%d tls_err=%d\n",
                          status_code, (uint32_t)scratch_len, (uint32_t)result_size,
                          is_chunked ? 1 : 0, tls_err);

            kfree(scratch);
            kfree(chunk);
            tls_close(tls);

            if (incomplete_body) {
                serial_printf("[HTTP] Resposta incompleta: recebido=%u esperado=%u read_diag=0x%x\n",
                              (uint32_t)result_size,
                              (uint32_t)content_length,
                              tls_read_diag);
                kfree(result_body);
                result_body = NULL;
                result_size = 0;
                return -8;
            }

        } else {
            /* -------------------------------------------------------------
             * FLUXO HTTP PLANO (lwIP TCP direto)
             * ------------------------------------------------------------- */
            strcpy(log_buf, "Resolvendo host: ");
            strcat(log_buf, host);
            strcat(log_buf, "...");
            http_log(output_cb, user_data, log_buf);

            char resolved_ip[32];
            if (net_resolve_host(host, resolved_ip, sizeof(resolved_ip)) != 0) {
                http_log(output_cb, user_data, "Falha ao resolver host (DNS)");
                return -2;
            }

            ip4_addr_t target_ip;
            if (!ip4addr_aton(resolved_ip, &target_ip)) {
                http_log(output_cb, user_data, "Endereco IP invalido");
                return -3;
            }

            strcpy(log_buf, "Conectando a ");
            strcat(log_buf, resolved_ip);
            strcat(log_buf, ":");
            int_to_str(port, num);
            strcat(log_buf, num);
            strcat(log_buf, "...");
            http_log(output_cb, user_data, log_buf);

            http_download_ctx_t ctx;
            memset(&ctx, 0, sizeof(ctx));
            ctx.content_length = -1;
            ctx.progress_cb = progress_cb;
            ctx.progress_data = progress_data;
            ctx.body_limit = body_limit;
            ctx.body_capacity = HTTP_INIT_BODY_CAPACITY;
            ctx.body_buf = kmalloc(ctx.body_capacity);
            ctx.scratch = kmalloc(HTTP_SCRATCH_SIZE);
            if (!ctx.body_buf || !ctx.scratch) {
                if (ctx.body_buf) kfree(ctx.body_buf);
                if (ctx.scratch) kfree(ctx.scratch);
                http_log(output_cb, user_data, "Memoria insuficiente");
                return -4;
            }
            ctx.body_buf[0] = '\0';
            ctx.scratch[0] = '\0';

            uint64_t lflags = net_lock_acquire();
            struct tcp_pcb *pcb = tcp_new();
            if (!pcb) {
                net_lock_release(lflags);
                kfree(ctx.scratch);
                kfree(ctx.body_buf);
                http_log(output_cb, user_data, "Erro ao alocar socket TCP");
                return -5;
            }
            ctx.pcb = pcb;

            tcp_arg(pcb, &ctx);
            tcp_recv(pcb, http_recv_cb);
            tcp_err(pcb, http_err_cb);

            err_t connect_err = tcp_connect(pcb, (ip_addr_t *)&target_ip, port, http_connect_cb);
            if (connect_err != ERR_OK) {
                http_close_pcb_locked(&ctx);
                net_lock_release(lflags);
                kfree(ctx.scratch);
                kfree(ctx.body_buf);
                http_log(output_cb, user_data, "Falha na conexao TCP");
                return -6;
            }
            net_lock_release(lflags);

            uint64_t conn_timeout = timer_ticks + 700;
            while (!ctx.connected && !ctx.failed && timer_ticks < conn_timeout) {
                net_poll();
                task_sleep_ms(2);
            }

            if (!ctx.connected || ctx.failed) {
                uint64_t cflags = net_lock_acquire();
                http_close_pcb_locked(&ctx);
                net_lock_release(cflags);
                kfree(ctx.scratch);
                kfree(ctx.body_buf);
                http_log(output_cb, user_data, "Timeout ou erro ao conectar");
                return -7;
            }

            http_log(output_cb, user_data, "Conectado! Enviando requisicao HTTP GET...");

            char req[HTTP_PATH_MAX + 256U];
            if (strlen(path) + strlen(host) + 128 >= sizeof(req)) {
                uint64_t cflags = net_lock_acquire();
                http_close_pcb_locked(&ctx);
                net_lock_release(cflags);
                kfree(ctx.scratch);
                kfree(ctx.body_buf);
                http_log(output_cb, user_data, "URL muito longa para requisicao HTTP");
                return -1;
            }
            req[0] = '\0';
            strcat(req, "GET ");
            strcat(req, path);
            strcat(req, " HTTP/1.0\r\nHost: ");
            strcat(req, host);
            strcat(req, "\r\nUser-Agent: LiwusOS-wget/0.2\r\nAccept: */*\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");

            uint64_t wflags = net_lock_acquire();
            if (ctx.pcb && !ctx.failed) {
                tcp_write(ctx.pcb, req, strlen(req), TCP_WRITE_FLAG_COPY);
                tcp_output(ctx.pcb);
            }
            net_lock_release(wflags);

            uint32_t download_timeout = timer_ticks + 1500;
            while (!ctx.finished && !ctx.failed && timer_ticks < download_timeout) {
                if (ctx.headers_done && ctx.content_length >= 0 &&
                    (int)ctx.body_size >= ctx.content_length) {
                    break;
                }
                net_poll();
                task_sleep_ms(2);
            }

            uint64_t cflags = net_lock_acquire();
            http_close_pcb_locked(&ctx);
            net_lock_release(cflags);

            status_code = ctx.status_code;
            result_body = ctx.body_buf;
            result_size = ctx.body_size;
            is_chunked = ctx.is_chunked;
            strncpy(location, ctx.location, sizeof(location) - 1);
            location[sizeof(location) - 1] = '\0';
            kfree(ctx.scratch);

            if (result_body && result_size > 0 && is_chunked) {
                result_size = http_unchunk(result_body, result_size);
            }
            if (result_body) {
                result_body[result_size] = '\0';
            }

            if (ctx.failed) {
                kfree(ctx.body_buf);
                if (ctx.body_too_large) {
                    if (out_status) *out_status = status_code;
                    return -9;
                }
                return -8;
            }
        }

        if ((status_code == 301 || status_code == 302 || status_code == 303 ||
             status_code == 307 || status_code == 308) &&
            location[0] != '\0' && redirect_hop < 3) {
            char next_url[HTTP_URL_MAX];
            resolve_redirect_url(is_tls, host, port, location, next_url, sizeof(next_url));
            serial_printf("[HTTP] Redirecionando (%d) -> %s\n", status_code, next_url);
            if (result_body) {
                kfree(result_body);
                result_body = NULL;
            }
            strncpy(active_url, next_url, sizeof(active_url) - 1);
            active_url[sizeof(active_url) - 1] = '\0';
            continue;
        }
        break;
    }

    if (out_status) {
        *out_status = status_code;
    }

    if (status_code != 200 && status_code != 0) {
        strcpy(log_buf, "Resposta recebida com Status HTTP: ");
        int_to_str(status_code, num);
        strcat(log_buf, num);
        http_log(output_cb, user_data, log_buf);
    }

    /* Se gravacao em disco SDFS solicitada */
    if (dest_path) {
        if (!result_body || result_size == 0) {
            if (result_body) kfree(result_body);
            http_log(output_cb, user_data, "Nenhum dado recebido do servidor");
            return -8;
        }

        strcpy(log_buf, "Gravando ");
        int_to_str((int)result_size, num);
        strcat(log_buf, num);
        strcat(log_buf, " bytes em ");
        strcat(log_buf, final_dest);
        strcat(log_buf, "...");
        http_log(output_cb, user_data, log_buf);

        /* Garante que diretorios pais existam no SDFS */
        for (char *dp = final_dest + 1; *dp; dp++) {
            if (*dp == '/') {
                *dp = '\0';
                sdfs_create_dir(final_dest);
                *dp = '/';
            }
        }

        bool is_elf = (result_size >= 4 &&
                       result_body[0] == 0x7F &&
                       result_body[1] == 'E' &&
                       result_body[2] == 'L' &&
                       result_body[3] == 'F');
        size_t dlen = strlen(final_dest);
        bool is_lxe = (dlen > 4 && strcmp(final_dest + dlen - 4, ".lxe") == 0);

        uint32_t written = sdfs_write_file(final_dest, result_body, (uint32_t)result_size);
        kfree(result_body);

        if (written != result_size) {
            http_log(output_cb, user_data, "Erro ao gravar arquivo no disco SDFS");
            return -9;
        }

        if (is_elf || is_lxe) {
            sdfs_set_permissions(final_dest, 0755);
        }
        sdfs_flush_bitmap();

        http_log(output_cb, user_data, "Arquivo salvo com sucesso no SDFS!");
        return (int)written;
    }

    /* Retorno em memoria para navegadores e aplicacoes */
    if (out_body) {
        *out_body = result_body;
    }
    if (out_size) {
        *out_size = result_size;
    }

    return (int)result_size;
}

int http_download_file(const char *url, const char *dest_path,
                       http_output_fn output_cb, void *user_data) {
    return http_perform_request(url, NULL, NULL, NULL, dest_path ? dest_path : "", output_cb, user_data, NULL, NULL, 0);
}

int http_get_mem(const char *url, uint8_t **out_body, size_t *out_size, int *out_status,
                 http_output_fn output_cb, void *user_data) {
    return http_perform_request(url, out_body, out_size, out_status, NULL, output_cb, user_data, NULL, NULL, 0);
}

int http_get_mem_progress(const char *url, uint8_t **out_body, size_t *out_size,
                          int *out_status, http_output_fn output_cb,
                          void *user_data, http_progress_fn progress_cb,
                          void *progress_data) {
    return http_perform_request(url, out_body, out_size, out_status, NULL,
                                output_cb, user_data, progress_cb, progress_data, 0);
}

int http_get_mem_progress_limit(const char *url, uint8_t **out_body,
                                size_t *out_size, int *out_status,
                                http_output_fn output_cb, void *user_data,
                                http_progress_fn progress_cb,
                                void *progress_data, size_t max_body_size) {
    if (!max_body_size) return -1;
    return http_perform_request(url, out_body, out_size, out_status, NULL,
                                output_cb, user_data, progress_cb, progress_data,
                                max_body_size);
}
