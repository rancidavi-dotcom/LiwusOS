#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gui/browser_url.h"

static bool input_looks_like_url(const char *text) {
    if (!text || !text[0]) return false;
    return strstr(text, "://") != NULL || strchr(text, '/') != NULL ||
           strchr(text, '.') != NULL || strchr(text, '#') != NULL;
}

static bool build_search_url(const char *query, size_t query_len,
                             char *out, size_t out_sz) {
    static const char prefix[] = "https://www.bing.com/search?q=";
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = sizeof(prefix) - 1;

    if (!query || !out || out_sz <= pos) return false;
    memcpy(out, prefix, pos);
    for (size_t i = 0; i < query_len; i++) {
        unsigned char ch = (unsigned char)query[i];
        bool unreserved = (ch >= 'a' && ch <= 'z') ||
                          (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') ||
                          ch == '-' || ch == '_' || ch == '.' || ch == '~';
        size_t need = unreserved || ch == ' ' ? 1 : 3;
        if (pos + need >= out_sz) {
            out[0] = '\0';
            return false;
        }
        if (unreserved) out[pos++] = (char)ch;
        else if (ch == ' ') out[pos++] = '+';
        else {
            out[pos++] = '%';
            out[pos++] = hex[ch >> 4];
            out[pos++] = hex[ch & 0x0f];
        }
    }
    out[pos] = '\0';
    return true;
}

void browser_url_normalize_input(const char *raw, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!raw) return;
    while (*raw == ' ' || *raw == '\t') raw++;
    size_t rlen = strlen(raw);
    while (rlen && (raw[rlen - 1] == ' ' || raw[rlen - 1] == '\t')) rlen--;
    if (!rlen) return;

    if ((rlen == 4 && (raw[0] == 'w' || raw[0] == 'W') &&
         strncmp(raw + 1, "iki", 3) == 0) ||
        (rlen == 9 && (raw[0] == 'w' || raw[0] == 'W') &&
         strncmp(raw + 1, "ikipedia", 8) == 0)) {
        static const char wiki[] = "https://pt.wikipedia.org/";
        if (sizeof(wiki) <= out_sz) memcpy(out, wiki, sizeof(wiki));
        return;
    }

    const char *query = NULL;
    if (rlen > 5 && (raw[0] == 'w' || raw[0] == 'W') &&
        strncmp(raw + 1, "iki:", 4) == 0) query = raw + 5;
    else if (rlen > 2 && (raw[0] == 'w' || raw[0] == 'W') && raw[1] == ':')
        query = raw + 2;

    if (query) {
        while (*query == ' ' || *query == '\t') query++;
        static const char prefix[] =
            "https://pt.wikipedia.org/w/index.php?search=";
        size_t pos = sizeof(prefix) - 1;
        if (pos >= out_sz) return;
        memcpy(out, prefix, pos);
        size_t qlen = (size_t)(raw + rlen - query);
        for (size_t i = 0; i < qlen && pos + 1 < out_sz; i++)
            out[pos++] = query[i] == ' ' ? '+' : query[i];
        out[pos] = '\0';
        return;
    }

    if (!input_looks_like_url(raw)) {
        (void)build_search_url(raw, rlen, out, out_sz);
        return;
    }

    if (strstr(raw, "://") == NULL && strchr(raw, '.') != NULL &&
        raw[0] != '/' && raw[0] != '.') {
        static const char scheme[] = "https://";
        size_t scheme_len = sizeof(scheme) - 1;
        if (rlen + sizeof(scheme) > out_sz) return;
        memcpy(out, scheme, scheme_len);
        memcpy(out + scheme_len, raw, rlen);
        out[scheme_len + rlen] = '\0';
        return;
    }

    size_t cp = rlen < out_sz - 1 ? rlen : out_sz - 1;
    memcpy(out, raw, cp);
    out[cp] = '\0';
}
