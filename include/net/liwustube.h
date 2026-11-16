#ifndef NET_LIWUSTUBE_H
#define NET_LIWUSTUBE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define LIWUSTUBE_MAX_RESULTS 16
#define LIWUSTUBE_MAX_TITLE   128
#define LIWUSTUBE_MAX_AUTHOR  64
#define LIWUSTUBE_MAX_ID      32
#define LIWUSTUBE_MAX_URL     2048

/* Instâncias públicas e Bridge padrão */
#define LIWUSTUBE_DEFAULT_PIPED_API     "https://api.piped.private.coffee"
#define LIWUSTUBE_FALLBACK_PIPED_API    "https://pipedapi.ducks.party"
#define LIWUSTUBE_DEFAULT_BRIDGE_API    "http://10.0.2.2:8085"
#define LIWUSTUBE_LOCAL_BRIDGE_API      "http://127.0.0.1:8085"
#define LIWUSTUBE_DEFAULT_INVIDIOUS_API "https://yt.chocolatemoo53.com"

const char *liwustube_get_api(void);
void liwustube_set_api(const char *url);
int liwustube_get_candidate_count(void);
const char *liwustube_get_candidate_api(int index);

typedef struct {
    char id[LIWUSTUBE_MAX_ID];
    char title[LIWUSTUBE_MAX_TITLE];
    char author[LIWUSTUBE_MAX_AUTHOR];
    int duration_sec;
    int64_t views;
} liwustube_search_item_t;

typedef struct {
    liwustube_search_item_t items[LIWUSTUBE_MAX_RESULTS];
    int count;
} liwustube_search_result_t;

typedef struct {
    char id[LIWUSTUBE_MAX_ID];
    char title[LIWUSTUBE_MAX_TITLE];
    char author[LIWUSTUBE_MAX_AUTHOR];
    int duration_sec;
    char stream_url[LIWUSTUBE_MAX_URL];
    char quality[16];   /* ex: "360p" */
    char container[16]; /* ex: "mp4" */
    char vcodec[16];    /* ex: "h264" */
    char acodec[16];    /* ex: "aac" */
    int width;
    int height;
    size_t content_length;
} liwustube_stream_info_t;

/* Parsing de JSON (freestanding, sem malloc, robusto contra overflow) */
int liwustube_parse_search_json(const char *json, size_t json_len, liwustube_search_result_t *out_res);
int liwustube_parse_stream_json(const char *json, size_t json_len, liwustube_stream_info_t *out_info);

/* Utilitários de URL e identificador de vídeo */
int liwustube_extract_video_id(const char *input, char *out_id, size_t out_size);
int liwustube_build_search_url(const char *base_api, const char *query, char *out_url, size_t out_size);
int liwustube_build_stream_url(const char *base_api, const char *video_id, char *out_url, size_t out_size);

/* Formatação amigável de tempo (MM:SS ou HH:MM:SS) */
void liwustube_format_duration(int seconds, char *buf, size_t buf_size);

#endif /* NET_LIWUSTUBE_H */
