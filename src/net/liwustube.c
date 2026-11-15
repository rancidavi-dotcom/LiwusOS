#include "net/liwustube.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#if defined(__KERNEL__) || defined(LIWUS_KERNEL)
#include "kernel.h"
#include "lib/string.h"
#else
#include <string.h>
#include <stdio.h>
#endif

/* ------------------------------------------------------------
 * Helpers de String e Caracteres
 * ------------------------------------------------------------ */

static inline bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

static inline bool is_alnum_dash(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_';
}

static const char *skip_whitespace(const char *p, const char *end) {
    while (p < end && is_space(*p)) {
        p++;
    }
    return p;
}

/* ------------------------------------------------------------
 * Parser Simples de String JSON (com unescape de \/, \", \uXXXX)
 * ------------------------------------------------------------ */

static const char *parse_json_string(const char *p, const char *end, char *out, size_t out_max) {
    p = skip_whitespace(p, end);
    if (p >= end || *p != '"') {
        return NULL;
    }
    p++; /* Pula aspas de abertura */

    size_t written = 0;
    while (p < end && *p != '"') {
        if (*p == '\\') {
            p++;
            if (p >= end) break;
            char esc = *p++;
            char c = esc;
            if (esc == '"') c = '"';
            else if (esc == '\\') c = '\\';
            else if (esc == '/') c = '/';
            else if (esc == 'b') c = '\b';
            else if (esc == 'f') c = '\f';
            else if (esc == 'n') c = ' ';
            else if (esc == 'r') c = ' ';
            else if (esc == 't') c = ' ';
            else if (esc == 'u') {
                /* Ex: \u0026 -> '&' */
                if (p + 4 <= end) {
                    uint32_t val = 0;
                    for (int i = 0; i < 4; i++) {
                        char h = p[i];
                        val <<= 4;
                        if (h >= '0' && h <= '9') val |= (h - '0');
                        else if (h >= 'a' && h <= 'f') val |= (h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') val |= (h - 'A' + 10);
                    }
                    p += 4;
                    if (val < 128) {
                        c = (char)val;
                    } else {
                        c = '?';
                    }
                }
            }
            if (out && written + 1 < out_max) {
                out[written++] = c;
            }
        } else {
            if (out && written + 1 < out_max) {
                out[written++] = *p;
            }
            p++;
        }
    }

    if (p < end && *p == '"') {
        p++; /* Pula aspas de fechamento */
    }

    if (out && out_max > 0) {
        out[written < out_max ? written : out_max - 1] = '\0';
    }
    return p;
}

static const char *parse_json_number(const char *p, const char *end, int64_t *out_val) {
    p = skip_whitespace(p, end);
    if (p >= end) return NULL;

    bool neg = false;
    if (*p == '-') {
        neg = true;
        p++;
    }

    int64_t v = 0;
    bool has_digit = false;
    while (p < end && is_digit(*p)) {
        v = (v * 10) + (*p - '0');
        p++;
        has_digit = true;
    }

    /* Pula casas decimais ou expoente se houver */
    if (p < end && *p == '.') {
        p++;
        while (p < end && is_digit(*p)) p++;
    }
    if (p < end && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < end && (*p == '+' || *p == '-')) p++;
        while (p < end && is_digit(*p)) p++;
    }

    if (has_digit) {
        if (out_val) *out_val = neg ? -v : v;
        return p;
    }
    return NULL;
}

static const char *parse_json_bool(const char *p, const char *end, bool *out_val) {
    p = skip_whitespace(p, end);
    if (p + 4 <= end && memcmp(p, "true", 4) == 0) {
        if (out_val) *out_val = true;
        return p + 4;
    }
    if (p + 5 <= end && memcmp(p, "false", 5) == 0) {
        if (out_val) *out_val = false;
        return p + 5;
    }
    return NULL;
}

/* Pula um valor JSON arbitrário (escalar, objeto aninhado ou array) */
static const char *skip_json_value(const char *p, const char *end) {
    p = skip_whitespace(p, end);
    if (p >= end) return NULL;

    if (*p == '"') {
        return parse_json_string(p, end, NULL, 0);
    }
    if (*p == '{') {
        int depth = 0;
        while (p < end) {
            if (*p == '"') {
                p = parse_json_string(p, end, NULL, 0);
                if (!p) return NULL;
                continue;
            }
            if (*p == '{') depth++;
            else if (*p == '}') {
                depth--;
                if (depth == 0) return p + 1;
            }
            p++;
        }
        return NULL;
    }
    if (*p == '[') {
        int depth = 0;
        while (p < end) {
            if (*p == '"') {
                p = parse_json_string(p, end, NULL, 0);
                if (!p) return NULL;
                continue;
            }
            if (*p == '[') depth++;
            else if (*p == ']') {
                depth--;
                if (depth == 0) return p + 1;
            }
            p++;
        }
        return NULL;
    }
    /* Número, booleano ou null */
    while (p < end && *p != ',' && *p != '}' && *p != ']' && !is_space(*p)) {
        p++;
    }
    return p;
}

/* ------------------------------------------------------------
 * Utilitários de URL e Extração de ID do Vídeo
 * ------------------------------------------------------------ */

int liwustube_extract_video_id(const char *input, char *out_id, size_t out_size) {
    if (!input || !out_id || out_size < 12) return -1;
    out_id[0] = '\0';

    const char *start = input;

    /* Procura padrão v= */
    const char *v_param = strstr(input, "v=");
    if (v_param) {
        start = v_param + 2;
    } else {
        /* Procura /v/ ou /embed/ ou youtu.be/ */
        const char *slash_v = strstr(input, "/v/");
        if (slash_v) start = slash_v + 3;
        else {
            const char *embed = strstr(input, "/embed/");
            if (embed) start = embed + 7;
            else {
                const char *ybe = strstr(input, "youtu.be/");
                if (ybe) start = ybe + 9;
                else {
                    const char *last_slash = strrchr(input, '/');
                    if (last_slash && last_slash[1] != '\0') {
                        start = last_slash + 1;
                    }
                }
            }
        }
    }

    size_t i = 0;
    while (start[i] && is_alnum_dash(start[i]) && i + 1 < out_size) {
        out_id[i] = start[i];
        i++;
    }
    out_id[i] = '\0';

    if (i >= 6) {
        return 0;
    }
    return -1;
}

static void url_encode(const char *src, char *dst, size_t dst_size) {
    size_t w = 0;
    while (*src && w + 4 < dst_size) {
        char c = *src++;
        if (c == ' ') {
            dst[w++] = '+';
        } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') {
            dst[w++] = c;
        } else {
            static const char hex[] = "0123456789ABCDEF";
            dst[w++] = '%';
            dst[w++] = hex[(c >> 4) & 0x0F];
            dst[w++] = hex[c & 0x0F];
        }
    }
    if (w < dst_size) dst[w] = '\0';
}

static size_t str_copy(char *dst, size_t dst_size, const char *src) {
    size_t i = 0;
    if (!dst || dst_size == 0) return 0;
    while (src && src[i] && i + 1 < dst_size) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
    return i;
}

static size_t str_cat(char *dst, size_t dst_size, const char *src) {
    size_t dlen = 0;
    while (dst && dlen < dst_size && dst[dlen]) dlen++;
    if (dlen >= dst_size) return dlen;
    size_t i = 0;
    while (src && src[i] && dlen + i + 1 < dst_size) {
        dst[dlen + i] = src[i];
        i++;
    }
    dst[dlen + i] = '\0';
    return dlen + i;
}

static void format_two_digits(int v, char *out) {
    if (v < 0) v = 0;
    if (v > 99) v = 99;
    out[0] = (char)('0' + (v / 10));
    out[1] = (char)('0' + (v % 10));
}

static char g_liwustube_api[LIWUSTUBE_MAX_URL] = LIWUSTUBE_DEFAULT_BRIDGE_API;

const char *liwustube_get_api(void) {
    return g_liwustube_api;
}

void liwustube_set_api(const char *url) {
    if (!url || !url[0]) {
        str_copy(g_liwustube_api, sizeof(g_liwustube_api), LIWUSTUBE_DEFAULT_BRIDGE_API);
        return;
    }
    str_copy(g_liwustube_api, sizeof(g_liwustube_api), url);
    size_t len = strlen(g_liwustube_api);
    if (len > 0 && g_liwustube_api[len - 1] == '/') {
        g_liwustube_api[len - 1] = '\0';
    }
}

int liwustube_build_search_url(const char *base_api, const char *query, char *out_url, size_t out_size) {
    if (!query || !out_url || out_size < 32) return -1;
    if (!base_api) base_api = liwustube_get_api();

    char enc[128];
    url_encode(query, enc, sizeof(enc));

    out_url[0] = '\0';
    str_copy(out_url, out_size, base_api);

    if (strstr(base_api, "inv.") || strstr(base_api, "invidious")) {
        str_cat(out_url, out_size, "/api/v1/search?q=");
        str_cat(out_url, out_size, enc);
        str_cat(out_url, out_size, "&type=video");
    } else {
        str_cat(out_url, out_size, "/search?q=");
        str_cat(out_url, out_size, enc);
        str_cat(out_url, out_size, "&filter=videos");
    }
    return 0;
}

int liwustube_build_stream_url(const char *base_api, const char *video_id, char *out_url, size_t out_size) {
    if (!video_id || !out_url || out_size < 32) return -1;
    if (!base_api) base_api = liwustube_get_api();

    out_url[0] = '\0';
    str_copy(out_url, out_size, base_api);

    if (strstr(base_api, "inv.") || strstr(base_api, "invidious")) {
        str_cat(out_url, out_size, "/api/v1/videos/");
        str_cat(out_url, out_size, video_id);
    } else {
        str_cat(out_url, out_size, "/streams/");
        str_cat(out_url, out_size, video_id);
    }
    return 0;
}

void liwustube_format_duration(int seconds, char *buf, size_t buf_size) {
    if (!buf || buf_size < 6) return;
    if (seconds < 0) seconds = 0;

    int h = seconds / 3600;
    int m = (seconds % 3600) / 60;
    int s = seconds % 60;

    if (h > 0) {
        if (buf_size < 9) return;
        format_two_digits(h, buf);
        buf[2] = ':';
        format_two_digits(m, buf + 3);
        buf[5] = ':';
        format_two_digits(s, buf + 6);
        buf[8] = '\0';
    } else {
        format_two_digits(m, buf);
        buf[2] = ':';
        format_two_digits(s, buf + 3);
        buf[5] = '\0';
    }
}

/* ------------------------------------------------------------
 * Parsing da Resposta de Busca JSON
 * ------------------------------------------------------------ */

int liwustube_parse_search_json(const char *json, size_t json_len, liwustube_search_result_t *out_res) {
    if (!json || json_len == 0 || !out_res) return -1;
    memset(out_res, 0, sizeof(*out_res));

    const char *end = json + json_len;
    const char *p = skip_whitespace(json, end);

    /* Encontra o início do array de itens */
    const char *items_start = NULL;
    const char *items_key = strstr(json, "\"items\":");
    if (items_key) {
        items_start = strchr(items_key, '[');
    } else if (*p == '[') {
        items_start = p;
    }

    if (!items_start) return -1;
    p = items_start + 1;

    while (p < end && out_res->count < LIWUSTUBE_MAX_RESULTS) {
        p = skip_whitespace(p, end);
        if (p >= end || *p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') {
            p = skip_json_value(p, end);
            continue;
        }

        /* Começo de um item */
        const char *obj_start = p;
        const char *obj_end = skip_json_value(obj_start, end);
        if (!obj_end) break;

        liwustube_search_item_t *item = &out_res->items[out_res->count];
        memset(item, 0, sizeof(*item));

        /* Parseia campos dentro do objeto item */
        const char *cur = obj_start + 1;
        while (cur < obj_end) {
            cur = skip_whitespace(cur, obj_end);
            if (cur >= obj_end || *cur == '}') break;
            if (*cur == ',') { cur++; continue; }

            char key[64];
            cur = parse_json_string(cur, obj_end, key, sizeof(key));
            if (!cur) break;

            cur = skip_whitespace(cur, obj_end);
            if (cur < obj_end && *cur == ':') cur++;

            if (strcmp(key, "title") == 0) {
                cur = parse_json_string(cur, obj_end, item->title, sizeof(item->title));
            } else if (strcmp(key, "url") == 0 || strcmp(key, "videoId") == 0) {
                char raw_url[256];
                cur = parse_json_string(cur, obj_end, raw_url, sizeof(raw_url));
                liwustube_extract_video_id(raw_url, item->id, sizeof(item->id));
            } else if (strcmp(key, "uploaderName") == 0 || strcmp(key, "author") == 0) {
                cur = parse_json_string(cur, obj_end, item->author, sizeof(item->author));
            } else if (strcmp(key, "duration") == 0 || strcmp(key, "lengthSeconds") == 0) {
                int64_t dur = 0;
                cur = parse_json_number(cur, obj_end, &dur);
                item->duration_sec = (int)dur;
            } else if (strcmp(key, "views") == 0 || strcmp(key, "viewCount") == 0) {
                int64_t v = 0;
                cur = parse_json_number(cur, obj_end, &v);
                item->views = v;
            } else {
                cur = skip_json_value(cur, obj_end);
            }
        }

        /* Se temos ID e título válidos, adiciona */
        if (item->id[0] != '\0' && item->title[0] != '\0') {
            out_res->count++;
        }

        p = obj_end;
    }

    return out_res->count > 0 ? 0 : -1;
}

/* ------------------------------------------------------------
 * Parsing da Resposta de Stream JSON
 * ------------------------------------------------------------ */

int liwustube_parse_stream_json(const char *json, size_t json_len, liwustube_stream_info_t *out_info) {
    if (!json || json_len == 0 || !out_info) return -1;
    memset(out_info, 0, sizeof(*out_info));

    const char *end = json + json_len;

    /* Extrai metadados de nível superior */
    const char *title_key = strstr(json, "\"title\":");
    if (title_key) {
        const char *val = strchr(title_key, ':');
        if (val) parse_json_string(val + 1, end, out_info->title, sizeof(out_info->title));
    }

    const char *author_key = strstr(json, "\"uploader\":");
    if (!author_key) author_key = strstr(json, "\"author\":");
    if (author_key) {
        const char *val = strchr(author_key, ':');
        if (val) parse_json_string(val + 1, end, out_info->author, sizeof(out_info->author));
    }

    const char *duration_key = strstr(json, "\"duration\":");
    if (!duration_key) duration_key = strstr(json, "\"lengthSeconds\":");
    if (duration_key) {
        const char *val = strchr(duration_key, ':');
        if (val) {
            int64_t dur = 0;
            parse_json_number(val + 1, end, &dur);
            out_info->duration_sec = (int)dur;
        }
    }

    /* Procura array de streams ("videoStreams" no Piped ou "formatStreams" no Invidious) */
    const char *streams_key = strstr(json, "\"videoStreams\":");
    if (!streams_key) streams_key = strstr(json, "\"formatStreams\":");
    if (!streams_key) return -1;

    const char *streams_start = strchr(streams_key, '[');
    if (!streams_start) return -1;

    const char *p = streams_start + 1;
    bool found_best = false;

    while (p < end) {
        p = skip_whitespace(p, end);
        if (p >= end || *p == ']') break;
        if (*p == ',') { p++; continue; }
        if (*p != '{') {
            p = skip_json_value(p, end);
            continue;
        }

        const char *obj_start = p;
        const char *obj_end = skip_json_value(obj_start, end);
        if (!obj_end) break;

        char url[LIWUSTUBE_MAX_URL] = {0};
        char quality[16] = {0};
        char format[16] = {0};
        char vcodec[16] = {0};
        char acodec[16] = {0};
        bool video_only = true;
        int64_t content_len = 0;
        int64_t w = 0, h = 0;

        const char *cur = obj_start + 1;
        while (cur < obj_end) {
            cur = skip_whitespace(cur, obj_end);
            if (cur >= obj_end || *cur == '}') break;
            if (*cur == ',') { cur++; continue; }

            char key[64];
            cur = parse_json_string(cur, obj_end, key, sizeof(key));
            if (!cur) break;

            cur = skip_whitespace(cur, obj_end);
            if (cur < obj_end && *cur == ':') cur++;

            if (strcmp(key, "url") == 0) {
                cur = parse_json_string(cur, obj_end, url, sizeof(url));
            } else if (strcmp(key, "quality") == 0 || strcmp(key, "qualityLabel") == 0 || strcmp(key, "resolution") == 0) {
                char qtmp[16] = {0};
                cur = parse_json_string(cur, obj_end, qtmp, sizeof(qtmp));
                if (quality[0] == '\0' || strstr(qtmp, "360") != NULL || strstr(qtmp, "p") != NULL) {
                    strncpy(quality, qtmp, sizeof(quality) - 1);
                }
            } else if (strcmp(key, "format") == 0 || strcmp(key, "container") == 0 ||
                       strcmp(key, "type") == 0 || strcmp(key, "mimeType") == 0 || strcmp(key, "ext") == 0) {
                cur = parse_json_string(cur, obj_end, format, sizeof(format));
            } else if (strcmp(key, "codec") == 0 || strcmp(key, "encoding") == 0) {
                cur = parse_json_string(cur, obj_end, vcodec, sizeof(vcodec));
            } else if (strcmp(key, "audioEncoding") == 0) {
                cur = parse_json_string(cur, obj_end, acodec, sizeof(acodec));
            } else if (strcmp(key, "videoOnly") == 0) {
                cur = parse_json_bool(cur, obj_end, &video_only);
            } else if (strcmp(key, "contentLength") == 0) {
                cur = parse_json_number(cur, obj_end, &content_len);
            } else if (strcmp(key, "width") == 0) {
                cur = parse_json_number(cur, obj_end, &w);
            } else if (strcmp(key, "height") == 0) {
                cur = parse_json_number(cur, obj_end, &h);
            } else {
                cur = skip_json_value(cur, obj_end);
            }
        }

        /* Se for stream progressivo de vídeo + áudio (video_only == false ou formatStreams do Invidious/Bridge) */
        bool is_mp4 = (strstr(format, "mp4") != NULL || strstr(format, "MP4") != NULL ||
                       strstr(format, "MPEG_4") != NULL || strstr(url, ".mp4") != NULL ||
                       strstr(url, "/video/") != NULL || strstr(url, "mime=video") != NULL ||
                       strstr(url, "video%2Fmp4") != NULL || strstr(url, "video/mp4") != NULL ||
                       format[0] == '\0'); /* Se formato omitido, aceita com fallback */
        bool has_both = (!video_only || strstr(json, "\"formatStreams\":") != NULL || strstr(url, "/video/") != NULL);

        if (url[0] != '\0' && (is_mp4 || !found_best) && (has_both || !found_best)) {
            bool is_360p = (strstr(quality, "360") != NULL || h == 360);

            /* Preferência por 360p */
            if (is_360p || !found_best) {
                strncpy(out_info->stream_url, url, sizeof(out_info->stream_url) - 1);
                strncpy(out_info->quality, quality[0] ? quality : "360p", sizeof(out_info->quality) - 1);
                strncpy(out_info->container, "mp4", sizeof(out_info->container) - 1);
                strncpy(out_info->vcodec, vcodec[0] ? vcodec : "h264", sizeof(out_info->vcodec) - 1);
                strncpy(out_info->acodec, acodec[0] ? acodec : "aac", sizeof(out_info->acodec) - 1);
                out_info->content_length = (size_t)content_len;
                out_info->width = w > 0 ? (int)w : 640;
                out_info->height = h > 0 ? (int)h : 360;
                found_best = true;

                if (is_360p) break; /* Encontrou o formato ideal! */
            }
        }

        p = obj_end;
    }

    return found_best ? 0 : -1;
}
