#ifndef NET_HTTP_CLIENT_H
#define NET_HTTP_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef void (*http_output_fn)(const char *msg, void *user_data);
typedef void (*http_progress_fn)(size_t received, size_t total, void *user_data);

/* Metadados limitados que podem ser expostos ao consumidor de uma resposta.
 * O cliente continua sendo deliberadamente GET-only neste milestone. */
typedef struct {
    char content_type[128];
} http_response_meta_t;

/* Baixa um arquivo via HTTP ou HTTPS (GET) e grava diretamente no disco SDFS.
 * Suporta formatos de URL:
 *   http://192.168.0.21:8000/arquivo.txt
 *   https://example.com/index.html
 *   http://example.com/index.html
 *   192.168.0.21:8000/arquivo.txt
 * Retorna a quantidade de bytes salvos (> 0) em caso de sucesso, ou codigo negativo em erro. */
int http_download_file(const char *url, const char *dest_path,
                       http_output_fn output_cb, void *user_data);

/* Baixa uma pagina ou recurso via HTTP ou HTTPS e armazena em memoria dinamica (kmalloc).
 * Retorna bytes no body (> 0) em caso de sucesso, preenchendo *out_body e *out_size.
 * O chamador e responsavel por liberar *out_body com kfree().
 * Retorna codigo negativo em caso de erro. */
int http_get_mem(const char *url, uint8_t **out_body, size_t *out_size, int *out_status,
                 http_output_fn output_cb, void *user_data);

/* Variant that reports body bytes as they arrive. total is zero when the
 * response does not provide Content-Length. Existing callers keep using the
 * simpler API above. */
int http_get_mem_progress(const char *url, uint8_t **out_body, size_t *out_size,
                          int *out_status, http_output_fn output_cb,
                          void *user_data, http_progress_fn progress_cb,
                          void *progress_data);

/* Memory-returning GET with an enforced body-size ceiling. The response is
 * aborted before its buffer can grow beyond max_body_size. */
int http_get_mem_progress_limit(const char *url, uint8_t **out_body,
                                size_t *out_size, int *out_status,
                                http_output_fn output_cb, void *user_data,
                                http_progress_fn progress_cb,
                                void *progress_data, size_t max_body_size);

/* Variante que retorna os metadados básicos da resposta sem alterar a API
 * existente usada pelo kernel e pelos aplicativos nativos. */
int http_get_mem_progress_limit_meta(const char *url, uint8_t **out_body,
                                     size_t *out_size, int *out_status,
                                     http_output_fn output_cb, void *user_data,
                                     http_progress_fn progress_cb,
                                     void *progress_data,
                                     http_response_meta_t *meta,
                                     size_t max_body_size);

#endif /* NET_HTTP_CLIENT_H */
