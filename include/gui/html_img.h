#ifndef GUI_HTML_IMG_H
#define GUI_HTML_IMG_H

#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>

/* Decodifica PNG ou JPEG (baseline) em memoria para um lv_img_dsc_t pronto para LVGL.
 * Redimensiona proporcionalmente caso exceda `max_w` ou `max_h` (se > 0).
 * O descritor retornado (e seu buffer `data`) deve ser liberado via `html_img_free()`.
 * Retorna NULL em formatos nao suportados (ex: SVG/WebP/JPEG progressivo) ou erro. */
lv_img_dsc_t *html_img_decode(const uint8_t *raw, size_t len, int max_w, int max_h);

/* Decodifica URIs `data:image/(png|jpeg|jpg);base64,...` em buffer bruto (liberar com kfree).
 * Retorna tamanho (> 0) em sucesso ou -1 se nao suportado/invalido. */
int html_img_decode_data_uri(const char *uri, uint8_t **out_raw, size_t *out_len);

/* Libera descritor e pixels alocados por `html_img_decode()`. */
void html_img_free(lv_img_dsc_t *dsc);

#endif /* GUI_HTML_IMG_H */
