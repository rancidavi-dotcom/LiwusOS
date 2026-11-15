#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "gui/browser_url.h"

typedef struct {
    const char *name;
    const char *search_prefix;
    const char *home_url;
} search_engine_info_t;

static const search_engine_info_t g_engines[BROWSER_SEARCH_COUNT] = {
    [BROWSER_SEARCH_BING] = {
        .name = "Bing",
        .search_prefix = "https://www.bing.com/search?q=",
        .home_url = "https://www.bing.com"
    },
    [BROWSER_SEARCH_GOOGLE] = {
        .name = "Google",
        .search_prefix = "https://www.google.com/search?q=",
        .home_url = "https://www.google.com"
    },
    [BROWSER_SEARCH_DUCKDUCKGO] = {
        .name = "DuckDuckGo",
        .search_prefix = "https://duckduckgo.com/?q=",
        .home_url = "https://duckduckgo.com"
    },
    [BROWSER_SEARCH_WIKIPEDIA] = {
        .name = "Wikipedia",
        .search_prefix = "https://pt.wikipedia.org/w/index.php?search=",
        .home_url = "https://pt.wikipedia.org"
    }
};

static browser_search_engine_t g_active_engine = BROWSER_SEARCH_BING;

void browser_url_set_search_engine(browser_search_engine_t engine) {
    if (engine < BROWSER_SEARCH_COUNT) {
        g_active_engine = engine;
    }
}

browser_search_engine_t browser_url_get_search_engine(void) {
    return g_active_engine;
}

const char *browser_url_get_search_engine_name(browser_search_engine_t engine) {
    if (engine >= BROWSER_SEARCH_COUNT) engine = BROWSER_SEARCH_BING;
    return g_engines[engine].name;
}

const char *browser_url_get_search_engine_home(browser_search_engine_t engine) {
    if (engine >= BROWSER_SEARCH_COUNT) engine = BROWSER_SEARCH_BING;
    return g_engines[engine].home_url;
}

const char *browser_url_get_search_engine_prefix(browser_search_engine_t engine) {
    if (engine >= BROWSER_SEARCH_COUNT) engine = BROWSER_SEARCH_BING;
    return g_engines[engine].search_prefix;
}

bool browser_url_build_search(browser_search_engine_t engine, const char *query,
                              size_t query_len, char *out, size_t out_sz) {
    if (engine >= BROWSER_SEARCH_COUNT) engine = BROWSER_SEARCH_BING;
    const char *prefix = g_engines[engine].search_prefix;
    static const char hex[] = "0123456789ABCDEF";
    size_t pos = strlen(prefix);

    if (!query || !out || out_sz <= pos) return false;
    memcpy(out, prefix, pos);
    for (size_t i = 0; i < query_len; i++) {
        unsigned char ch = (unsigned char)query[i];
        bool unreserved = (ch >= 'a' && ch <= 'z') ||
                          (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') ||
                          ch == '-' || ch == '_' || ch == '.' || ch == '~';
        size_t need = (unreserved || ch == ' ') ? 1 : 3;
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

static bool input_looks_like_url(const char *text, size_t len) {
    if (!text || len == 0) return false;
    /* Se contiver espacos dentro da extensao util, e termo de busca */
    for (size_t i = 0; i < len; i++) {
        if (text[i] == ' ' || text[i] == '\t') return false;
    }
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '.' || text[i] == '/' || text[i] == '#') return true;
        if (i + 2 < len && text[i] == ':' && text[i+1] == '/' && text[i+2] == '/') return true;
    }
    return false;
}

static bool strcaseneq(const char *a, const char *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (!a[i] || !b[i]) return a[i] == b[i];
        char ca = (a[i] >= 'A' && a[i] <= 'Z') ? (char)(a[i] + 32) : a[i];
        char cb = (b[i] >= 'A' && b[i] <= 'Z') ? (char)(b[i] + 32) : b[i];
        if (ca != cb) return false;
    }
    return true;
}

static bool strcaseeq(const char *a, const char *b) {
    while (*a && *b) {
        char ca = (*a >= 'A' && *a <= 'Z') ? (char)(*a + 32) : *a;
        char cb = (*b >= 'A' && *b <= 'Z') ? (char)(*b + 32) : *b;
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == *b;
}

void browser_url_normalize_input(const char *raw, char *out, size_t out_sz) {
    if (!out || out_sz == 0) return;
    out[0] = '\0';
    if (!raw) return;
    while (*raw == ' ' || *raw == '\t') raw++;
    size_t rlen = strlen(raw);
    while (rlen && (raw[rlen - 1] == ' ' || raw[rlen - 1] == '\t')) rlen--;
    if (!rlen) return;

    /* Match the address-bar maximum (BROWSER_URL_MAX in desktop.c) so the
     * length passed to the helpers below never reads beyond this buffer. */
    char trim[2048];
    if (rlen >= sizeof(trim)) return;
    size_t copy_len = rlen;
    memcpy(trim, raw, copy_len);
    trim[copy_len] = '\0';

    /* Atalhos para Home dos mecanismos e sites populares */
    if (strcaseeq(trim, "google") || strcaseeq(trim, "google.com")) {
        const char *u = "https://www.google.com";
        size_t ulen = strlen(u);
        if (ulen < out_sz) memcpy(out, u, ulen + 1);
        return;
    }
    if (strcaseeq(trim, "ddg") || strcaseeq(trim, "duckduckgo") || strcaseeq(trim, "duckduckgo.com")) {
        const char *u = "https://duckduckgo.com";
        size_t ulen = strlen(u);
        if (ulen < out_sz) memcpy(out, u, ulen + 1);
        return;
    }
    if (strcaseeq(trim, "bing") || strcaseeq(trim, "bing.com")) {
        const char *u = "https://www.bing.com";
        size_t ulen = strlen(u);
        if (ulen < out_sz) memcpy(out, u, ulen + 1);
        return;
    }
    if (strcaseeq(trim, "wiki") || strcaseeq(trim, "wikipedia") || strcaseeq(trim, "wikipedia.org")) {
        const char *u = "https://pt.wikipedia.org/";
        size_t ulen = strlen(u);
        if (ulen < out_sz) memcpy(out, u, ulen + 1);
        return;
    }

    /* Detecção de prefixos de mecanismo de busca */
    browser_search_engine_t prefix_eng = BROWSER_SEARCH_COUNT;
    const char *query = NULL;

    if (rlen > 7 && strcaseneq(trim, "google:", 7)) {
        prefix_eng = BROWSER_SEARCH_GOOGLE;
        query = trim + 7;
    } else if (rlen > 7 && strcaseneq(trim, "google ", 7)) {
        prefix_eng = BROWSER_SEARCH_GOOGLE;
        query = trim + 7;
    } else if (rlen > 2 && strcaseneq(trim, "g:", 2)) {
        prefix_eng = BROWSER_SEARCH_GOOGLE;
        query = trim + 2;
    } else if (rlen > 2 && strcaseneq(trim, "g ", 2)) {
        prefix_eng = BROWSER_SEARCH_GOOGLE;
        query = trim + 2;
    } else if (rlen > 4 && strcaseneq(trim, "ddg:", 4)) {
        prefix_eng = BROWSER_SEARCH_DUCKDUCKGO;
        query = trim + 4;
    } else if (rlen > 4 && strcaseneq(trim, "ddg ", 4)) {
        prefix_eng = BROWSER_SEARCH_DUCKDUCKGO;
        query = trim + 4;
    } else if (rlen > 5 && strcaseneq(trim, "duck:", 5)) {
        prefix_eng = BROWSER_SEARCH_DUCKDUCKGO;
        query = trim + 5;
    } else if (rlen > 5 && strcaseneq(trim, "duck ", 5)) {
        prefix_eng = BROWSER_SEARCH_DUCKDUCKGO;
        query = trim + 5;
    } else if (rlen > 2 && strcaseneq(trim, "d:", 2)) {
        prefix_eng = BROWSER_SEARCH_DUCKDUCKGO;
        query = trim + 2;
    } else if (rlen > 5 && strcaseneq(trim, "bing:", 5)) {
        prefix_eng = BROWSER_SEARCH_BING;
        query = trim + 5;
    } else if (rlen > 5 && strcaseneq(trim, "bing ", 5)) {
        prefix_eng = BROWSER_SEARCH_BING;
        query = trim + 5;
    } else if (rlen > 2 && strcaseneq(trim, "b:", 2)) {
        prefix_eng = BROWSER_SEARCH_BING;
        query = trim + 2;
    } else if (rlen > 2 && strcaseneq(trim, "b ", 2)) {
        prefix_eng = BROWSER_SEARCH_BING;
        query = trim + 2;
    } else if (rlen > 5 && strcaseneq(trim, "wiki:", 5)) {
        prefix_eng = BROWSER_SEARCH_WIKIPEDIA;
        query = trim + 5;
    } else if (rlen > 5 && strcaseneq(trim, "wiki ", 5)) {
        prefix_eng = BROWSER_SEARCH_WIKIPEDIA;
        query = trim + 5;
    } else if (rlen > 2 && strcaseneq(trim, "w:", 2)) {
        prefix_eng = BROWSER_SEARCH_WIKIPEDIA;
        query = trim + 2;
    }

    if (prefix_eng < BROWSER_SEARCH_COUNT && query) {
        while (*query == ' ' || *query == '\t') query++;
        size_t qlen = strlen(query);
        if (qlen == 0) {
            const char *home = g_engines[prefix_eng].home_url;
            size_t hlen = strlen(home);
            if (hlen < out_sz) memcpy(out, home, hlen + 1);
            return;
        }
        (void)browser_url_build_search(prefix_eng, query, qlen, out, out_sz);
        return;
    }

    /* Se não se parece com URL (ex: tem espaços ou não tem ponto/barra/esquema), realiza pesquisa direta */
    if (!input_looks_like_url(trim, rlen)) {
        (void)browser_url_build_search(g_active_engine, trim, rlen, out, out_sz);
        return;
    }

    /* Caso seja um endereço sem esquema explícito (ex: github.com) */
    if (strstr(trim, "://") == NULL && strchr(trim, '.') != NULL &&
        trim[0] != '/' && trim[0] != '.') {
        static const char scheme[] = "https://";
        size_t scheme_len = sizeof(scheme) - 1;
        if (rlen + sizeof(scheme) > out_sz) return;
        memcpy(out, scheme, scheme_len);
        memcpy(out + scheme_len, trim, rlen);
        out[scheme_len + rlen] = '\0';
        return;
    }

    size_t cp = rlen < out_sz - 1 ? rlen : out_sz - 1;
    memcpy(out, trim, cp);
    out[cp] = '\0';
}
