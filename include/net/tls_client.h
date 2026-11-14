#ifndef NET_TLS_CLIENT_H
#define NET_TLS_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct tls_connection tls_connection_t;

enum {
    TLS_READ_DIAG_TIMEOUT = 1u << 0,
    TLS_READ_DIAG_PEER_CLOSED = 1u << 1,
    TLS_READ_DIAG_TCP_ERROR = 1u << 2,
    TLS_READ_DIAG_RX_OVERFLOW = 1u << 3,
};

/* Callback de log */
typedef void (*tls_log_fn)(const char *msg, void *user_data);

/* Inicia uma conexao TLS/HTTPS com o host na porta especificada.
 * host: FQDN ou IP (ex: "example.com")
 * port: porta TCP (padrao 443 para HTTPS)
 * timeout_ms: timeout maximo em ms para handshake
 * Retorna ponteiro para conexao TLS ou NULL em caso de erro */
tls_connection_t *tls_connect(const char *host, uint16_t port,
                              uint32_t timeout_ms, tls_log_fn log_cb, void *user_data);

/* Envia dados cifrados pela conexao TLS. Retorna quantidade de bytes enviados ou negativo em erro. */
int tls_write(tls_connection_t *conn, const void *data, size_t len);

/* Le dados decifrados da conexao TLS.
 * Retorna quantidade de bytes lidos (>0), 0 em fim de conexao (EOF), ou negativo em erro. */
int tls_read(tls_connection_t *conn, void *buf, size_t len, uint32_t timeout_ms);

/* Obtem o ultimo codigo de erro retornado pela engine TLS */
int tls_last_error(tls_connection_t *conn);

/* Diagnostic bits observed by the TLS socket read callback. */
uint32_t tls_read_diagnostics(tls_connection_t *conn);

/* Retorna uma descricao textual amigavel do codigo de erro TLS */
const char *tls_strerror(int err);

/* Fecha e desaloca a conexao TLS */
void tls_close(tls_connection_t *conn);

#endif /* NET_TLS_CLIENT_H */
