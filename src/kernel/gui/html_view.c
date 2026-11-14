#include "gui/html_view.h"
#include "gui/html_css.h"
#include "gui/html_font.h"
#include "gui/html_img.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"

#define HTML_MAX_DEPTH         44
#define HTML_MAX_WIDGETS       8192
#define HTML_MAX_SIBLING_MAP   32768U
#define HTML_MAX_EXT_CSS       24
#define HTML_MAX_CSS_BYTES     (1024U * 1024U)
#define HTML_MAX_CSS_TOTAL     (3U * 1024U * 1024U)
#define HTML_MAX_IMAGE_BYTES   (8U * 1024U * 1024U)
#define HTML_MAX_IMGS          24
#define HTML_MAX_FORMS         24
#define HTML_MAX_FORM_FIELDS   16
#define HTML_MAX_TCOLS         8
#define HTML_TEXT_BUF_SZ       2048
#define HTML_TAG_ATTR_SZ       1536
#define HTML_URL_MAX           2048




/* ---- Freestanding helpers ---- */

static bool hc_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool hc_isdigit(char c) {
    return c >= '0' && c <= '9';
}

static bool hc_isalpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char hc_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static bool hc_eq_ci(const char *a, const char *b) {
    while (*a && *b) {
        if (hc_tolower(*a) != hc_tolower(*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool hc_prefix_ci(const char *s, const char *p) {
    while (*p) {
        if (hc_tolower(*s) != hc_tolower(*p)) return false;
        s++;
        p++;
    }
    return true;
}

static bool hc_contains_ci(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    for (size_t i = 0; hay[i]; i++) {
        size_t j = 0;
        while (j < nlen && hay[i + j] &&
               hc_tolower(hay[i + j]) == hc_tolower(needle[j])) {
            j++;
        }
        if (j == nlen) return true;
    }
    return false;
}

static void hc_copy(const char *src, char *dst, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    while (src && src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void hc_copy_lower(const char *src, size_t len, char *dst, size_t cap) {
    if (!cap) return;
    size_t i = 0;
    while (i < len && i + 1 < cap) {
        dst[i] = hc_tolower(src[i]);
        i++;
    }
    dst[i] = '\0';
}

static int hc_parse_int(const char *s) {
    if (!s) return 0;
    while (*s && hc_isspace(*s)) s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    int v = 0;
    while (hc_isdigit(*s)) {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v * sign;
}

static bool ends_with_ci(const char *s, const char *suf) {
    size_t sl = strlen(s);
    size_t pl = strlen(suf);
    if (sl < pl) return false;
    return hc_eq_ci(s + (sl - pl), suf);
}

/* ---- UTF-8 & HTML entity decoder ---- */

typedef struct {
    uint16_t cp;
    const char *rep;
} cp_fallback_t;

static const cp_fallback_t g_cp_fallbacks[] = {
    {0x2010, "-"}, {0x2011, "-"}, {0x2012, "-"}, {0x2015, "-"}, {0x2212, "-"},
    {0x2032, "'"}, {0x2033, "\""}, {0x2039, "<"}, {0x203A, ">"}, {0x2044, "/"},
    {0x2190, "<-"}, {0x2192, "->"}, {0x2215, "/"}, {0x2248, "~"}, {0x2260, "!="},
    {0x2264, "<="}, {0x2265, ">="}, {0x22C5, "*"},
    /* Stress marks and common IPA transcriptions (e.g. encyclopedias) */
    {0x02C8, "'"}, {0x02CC, ","}, {0x02D0, ":"}, {0x027E, "r"}, {0x0281, "r"},
    {0x0250, "a"}, {0x0251, "a"}, {0x0252, "a"},
    {0x0259, "e"}, {0x025B, "e"}, {0x025C, "e"}, {0x01DD, "e"},
    {0x0254, "o"}, {0x0275, "o"},
    {0x0268, "i"}, {0x026A, "i"},
    {0x0289, "u"}, {0x028A, "u"},
    {0x0283, "x"}, {0x0292, "j"}, {0x0272, "n"}, {0x028E, "l"}, {0x014B, "n"},
    {0x0261, "g"}, {0x0263, "g"},
    {0, NULL}
};

static size_t encode_utf8_cp(uint32_t cp, char *out, size_t cap) {
    if (cap == 0) return 0;
    /* Ignore zero-width invisibles and combining diacritical marks */
    if ((cp >= 0x200B && cp <= 0x200F) || cp == 0x2060 || cp == 0xFEFF ||
        (cp >= 0x0300 && cp <= 0x036F)) {
        return 0;
    }

    if (cp == 0x00A0 || (cp >= 0x2000 && cp <= 0x200A) ||
        cp == 0x202F || cp == 0x205F || cp == 0x3000) {
        out[0] = ' ';
        return 1;
    }
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp <= 0x00FF) {
        if (cap < 2) return 0;
        out[0] = (char)(0xC0U | (cp >> 6));
        out[1] = (char)(0x80U | (cp & 0x3FU));
        return 2;
    }
    if (cp == 0x2013 || cp == 0x2014 || cp == 0x2018 || cp == 0x2019 ||
        cp == 0x201C || cp == 0x201D || cp == 0x2022 || cp == 0x2026 ||
        cp == 0x20AC || cp == 0x2122) {
        if (cap < 3) return 0;
        out[0] = (char)(0xE0U | (cp >> 12));
        out[1] = (char)(0x80U | ((cp >> 6) & 0x3FU));
        out[2] = (char)(0x80U | (cp & 0x3FU));
        return 3;
    }
    for (size_t i = 0; g_cp_fallbacks[i].rep != NULL; i++) {
        if (g_cp_fallbacks[i].cp == cp) {
            size_t rlen = strlen(g_cp_fallbacks[i].rep);
            size_t take = (rlen < cap) ? rlen : cap;
            memcpy(out, g_cp_fallbacks[i].rep, take);
            return take;
        }
    }
    out[0] = '?';
    return 1;
}

typedef struct {
    const char *name;
    uint32_t cp;
} html_entity_t;

static const html_entity_t g_entities[] = {
    {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
    {"nbsp", ' '}, {"iexcl", 0x00A1}, {"cent", 0x00A2}, {"pound", 0x00A3},
    {"copy", 0x00A9}, {"ordf", 0x00AA}, {"laquo", 0x00AB}, {"shy", 0},
    {"reg", 0x00AE}, {"deg", 0x00B0}, {"plusmn", 0x00B1}, {"sup2", 0x00B2},
    {"sup3", 0x00B3}, {"micro", 0x00B5}, {"para", 0x00B6}, {"middot", 0x00B7},
    {"sup1", 0x00B9}, {"ordm", 0x00BA}, {"raquo", 0x00BB}, {"frac14", 0x00BC},
    {"frac12", 0x00BD}, {"frac34", 0x00BE}, {"iquest", 0x00BF},
    {"Agrave", 0x00C0}, {"Aacute", 0x00C1}, {"Acirc", 0x00C2}, {"Atilde", 0x00C3},
    {"Auml", 0x00C4}, {"Aring", 0x00C5}, {"AElig", 0x00C6}, {"Ccedil", 0x00C7},
    {"Egrave", 0x00C8}, {"Eacute", 0x00C9}, {"Ecirc", 0x00CA}, {"Euml", 0x00CB},
    {"Igrave", 0x00CC}, {"Iacute", 0x00CD}, {"Icirc", 0x00CE}, {"Iuml", 0x00CF},
    {"Ntilde", 0x00D1}, {"Ograve", 0x00D2}, {"Oacute", 0x00D3}, {"Ocirc", 0x00D4},
    {"Otilde", 0x00D5}, {"Ouml", 0x00D6}, {"times", 0x00D7}, {"Oslash", 0x00D8},
    {"Ugrave", 0x00D9}, {"Uacute", 0x00DA}, {"Ucirc", 0x00DB}, {"Uuml", 0x00DC},
    {"Yacute", 0x00DD}, {"szlig", 0x00DF},
    {"agrave", 0x00E0}, {"aacute", 0x00E1}, {"acirc", 0x00E2}, {"atilde", 0x00E3},
    {"auml", 0x00E4}, {"aring", 0x00E5}, {"aelig", 0x00E6}, {"ccedil", 0x00E7},
    {"egrave", 0x00E8}, {"eacute", 0x00E9}, {"ecirc", 0x00EA}, {"euml", 0x00EB},
    {"igrave", 0x00EC}, {"iacute", 0x00ED}, {"icirc", 0x00EE}, {"iuml", 0x00EF},
    {"ntilde", 0x00F1}, {"ograve", 0x00F2}, {"oacute", 0x00F3}, {"ocirc", 0x00F4},
    {"otilde", 0x00F5}, {"ouml", 0x00F6}, {"divide", 0x00F7}, {"oslash", 0x00F8},
    {"ugrave", 0x00F9}, {"uacute", 0x00FA}, {"ucirc", 0x00FB}, {"uuml", 0x00FC},
    {"yacute", 0x00FD}, {"yuml", 0x00FF},
    {"ndash", 0x2013}, {"mdash", 0x2014}, {"lsquo", 0x2018}, {"rsquo", 0x2019},
    {"ldquo", 0x201C}, {"rdquo", 0x201D}, {"bull", 0x2022}, {"hellip", 0x2026},
    {"euro", 0x20AC}, {"trade", 0x2122}, {"rarr", 0x2192}, {"larr", 0x2190},
    {"sect", 0x00A7},
    {NULL, 0}
};

static bool decode_entity(const char *src, size_t len, uint32_t *out_cp, size_t *consumed) {
    if (len < 3 || src[0] != '&') return false;
    size_t i = 1;
    while (i < len && i < 16 && src[i] != ';' && !hc_isspace(src[i]) && src[i] != '<') i++;
    if (i >= len || src[i] != ';') return false;

    *consumed = i + 1;
    if (i >= 3 && src[1] == '#') {
        uint32_t cp = 0;
        if (src[2] == 'x' || src[2] == 'X') {
            for (size_t k = 3; k < i; k++) {
                char c = hc_tolower(src[k]);
                int d = (c >= '0' && c <= '9') ? (c - '0')
                      : (c >= 'a' && c <= 'f') ? (10 + c - 'a') : -1;
                if (d < 0) return false;
                cp = cp * 16U + (uint32_t)d;
            }
        } else {
            for (size_t k = 2; k < i; k++) {
                if (!hc_isdigit(src[k])) return false;
                cp = cp * 10U + (uint32_t)(src[k] - '0');
            }
        }
        *out_cp = cp;
        return true;
    }

    char name[16];
    size_t nlen = i - 1;
    if (nlen >= sizeof(name)) return false;
    memcpy(name, src + 1, nlen);
    name[nlen] = '\0';

    for (int e = 0; g_entities[e].name; e++) {
        if (strcmp(name, g_entities[e].name) == 0) {
            *out_cp = g_entities[e].cp;
            return true;
        }
    }
    return false;
}

static size_t decode_html_text(const char *src, size_t len, char *out, size_t out_cap,
                               bool preserve_ws) {
    size_t i = 0;
    size_t j = 0;
    bool prev_space = false;

    while (i < len && j + 4 < out_cap) {
        unsigned char uc = (unsigned char)src[i];

        if (uc == '&') {
            uint32_t cp = 0;
            size_t cons = 0;
            if (decode_entity(src + i, len - i, &cp, &cons)) {
                if (cp == 0 || cp == 0x200B || cp == 0x200C || cp == 0x200D) {
                    i += cons;
                    continue;
                }
                if (!preserve_ws && cp == ' ') {
                    if (!prev_space) {
                        out[j++] = ' ';
                        prev_space = true;
                    }
                } else {
                    size_t n = encode_utf8_cp(cp, out + j, out_cap - j - 1);
                    j += n;
                    prev_space = false;
                }
                i += cons;
                continue;
            }
        }

        if (!preserve_ws && hc_isspace((char)uc)) {
            if (!prev_space) {
                out[j++] = ' ';
                prev_space = true;
            }
            i++;
            continue;
        }

        if (uc < 0x80) {
            if (preserve_ws || uc >= 0x20) {
                out[j++] = (char)uc;
                prev_space = false;
            }
            i++;
            continue;
        }

        /* Decode UTF-8 multi-byte sequence or fallback ISO-8859-1 byte */
        uint32_t cp = '?';
        size_t step = 1;
        if ((uc & 0xE0U) == 0xC0U && i + 1 < len && ((uint8_t)src[i + 1] & 0xC0U) == 0x80U) {
            cp = ((uint32_t)(uc & 0x1FU) << 6) | ((uint32_t)src[i + 1] & 0x3FU);
            step = 2;
        } else if ((uc & 0xF0U) == 0xE0U && i + 2 < len &&
                   ((uint8_t)src[i + 1] & 0xC0U) == 0x80U &&
                   ((uint8_t)src[i + 2] & 0xC0U) == 0x80U) {
            cp = ((uint32_t)(uc & 0x0FU) << 12) |
                 (((uint32_t)src[i + 1] & 0x3FU) << 6) |
                 ((uint32_t)src[i + 2] & 0x3FU);
            step = 3;
        } else if ((uc & 0xF8U) == 0xF0U && i + 3 < len &&
                   ((uint8_t)src[i + 1] & 0xC0U) == 0x80U &&
                   ((uint8_t)src[i + 2] & 0xC0U) == 0x80U &&
                   ((uint8_t)src[i + 3] & 0xC0U) == 0x80U) {
            cp = ((uint32_t)(uc & 0x07U) << 18) |
                 (((uint32_t)src[i + 1] & 0x3FU) << 12) |
                 (((uint32_t)src[i + 2] & 0x3FU) << 6) |
                 ((uint32_t)src[i + 3] & 0x3FU);
            step = 4;
        } else if (uc >= 0xA0U) {
            cp = (uint32_t)uc; /* Latin-1 byte fallback */
            step = 1;
        }

        size_t n = encode_utf8_cp(cp, out + j, out_cap - j - 1);
        j += n;
        if (n > 0) prev_space = false;
        i += step;
    }

    out[j] = '\0';
    return j;
}

/* ---- URL & HTML attribute helpers ---- */

static void resolve_url(const char *base_url, const char *href, char *out, size_t cap) {
    if (!out || !cap) return;
    out[0] = '\0';
    if (!href || !href[0]) return;

    while (*href && hc_isspace(*href)) href++;
    if (!*href || href[0] == '#' || hc_prefix_ci(href, "javascript:") ||
        hc_prefix_ci(href, "mailto:")) {
        return;
    }
    if (hc_prefix_ci(href, "data:")) {
        hc_copy(href, out, cap);
        return;
    }

    char dec[HTML_URL_MAX];
    decode_html_text(href, strlen(href), dec, sizeof(dec), true);
    /* Trim trailing whitespace */
    size_t dlen = strlen(dec);
    while (dlen > 0 && hc_isspace(dec[dlen - 1])) dec[--dlen] = '\0';
    if (!dec[0]) return;

    if (hc_prefix_ci(dec, "http://") || hc_prefix_ci(dec, "https://")) {
        hc_copy(dec, out, cap);
        return;
    }

    const char *scheme = "https";
    const char *host_start = NULL;
    if (base_url) {
        if (hc_prefix_ci(base_url, "http://")) {
            scheme = "http";
            host_start = base_url + 7;
        } else if (hc_prefix_ci(base_url, "https://")) {
            scheme = "https";
            host_start = base_url + 8;
        }
    }

    if (dec[0] == '/' && dec[1] == '/') {
        size_t slen = strlen(scheme);
        if (slen + 1 + dlen < cap) {
            memcpy(out, scheme, slen);
            out[slen] = ':';
            hc_copy(dec, out + slen + 1, cap - slen - 1);
        }
        return;
    }

    if (!host_start) {
        hc_copy(dec, out, cap);
        return;
    }

    const char *path_start = strchr(host_start, '/');
    size_t origin_len = path_start ? (size_t)(path_start - base_url) : strlen(base_url);

    if (dec[0] == '/') {
        if (origin_len < cap) {
            memcpy(out, base_url, origin_len);
            out[origin_len] = '\0';
            hc_copy(dec, out + origin_len, cap - origin_len);
        }
        return;
    }

    if (dec[0] == '?') {
        const char *q = strchr(base_url, '?');
        size_t stem_len = q ? (size_t)(q - base_url) : strlen(base_url);
        if (stem_len < cap) {
            memcpy(out, base_url, stem_len);
            out[stem_len] = '\0';
            hc_copy(dec, out + stem_len, cap - stem_len);
        }
        return;
    }

    /* Relative to directory */
    const char *last_slash = path_start ? strrchr(path_start, '/') : NULL;
    if (last_slash) {
        size_t dir_len = (size_t)(last_slash - base_url) + 1;
        if (dir_len < cap) {
            memcpy(out, base_url, dir_len);
            out[dir_len] = '\0';
            hc_copy(dec, out + dir_len, cap - dir_len);
        }
    } else if (origin_len + 1 < cap) {
        memcpy(out, base_url, origin_len);
        out[origin_len] = '/';
        out[origin_len + 1] = '\0';
        hc_copy(dec, out + origin_len + 1, cap - origin_len - 1);
    }
}

static bool extract_attr(const char *attrs, size_t attrs_len,
                         const char *key, char *out, size_t cap) {
    out[0] = '\0';
    size_t klen = strlen(key);
    size_t i = 0;

    while (i < attrs_len) {
        while (i < attrs_len && (hc_isspace(attrs[i]) || attrs[i] == '/')) i++;
        if (i >= attrs_len) break;

        size_t nstart = i;
        while (i < attrs_len && !hc_isspace(attrs[i]) && attrs[i] != '=' && attrs[i] != '/') i++;
        size_t nlen = i - nstart;

        while (i < attrs_len && hc_isspace(attrs[i])) i++;
        bool match = (nlen == klen);
        if (match) {
            for (size_t k = 0; k < klen; k++) {
                if (hc_tolower(attrs[nstart + k]) != hc_tolower(key[k])) {
                    match = false;
                    break;
                }
            }
        }

        if (i < attrs_len && attrs[i] == '=') {
            i++;
            while (i < attrs_len && hc_isspace(attrs[i])) i++;
            size_t vstart = i, vlen = 0;
            if (i < attrs_len && (attrs[i] == '"' || attrs[i] == '\'')) {
                char q = attrs[i++];
                vstart = i;
                while (i < attrs_len && attrs[i] != q) i++;
                vlen = i - vstart;
                if (i < attrs_len && attrs[i] == q) i++;
            } else {
                while (i < attrs_len && !hc_isspace(attrs[i]) && attrs[i] != '>') i++;
                vlen = i - vstart;
            }
            if (match) {
                size_t cp = (vlen < cap - 1) ? vlen : (cap - 1);
                memcpy(out, attrs + vstart, cp);
                out[cp] = '\0';
                return true;
            }
        } else if (match) {
            hc_copy("1", out, cap);
            return true;
        }
    }
    return false;
}

static bool has_bool_attr(const char *attrs, size_t attrs_len, const char *key);
static void capture_css_attributes(const html_stylesheet_t *ss,
                                   css_node_info_t *node,
                                   const char *attrs, size_t attrs_len) {
    if (!node || !attrs) return;
    for (unsigned pass = 0; pass < 2; ++pass) {
        size_t i = 0;
        while (i < attrs_len) {
            while (i < attrs_len && (hc_isspace(attrs[i]) || attrs[i] == '/')) ++i;
            if (i >= attrs_len) break;
            size_t name_start = i;
            while (i < attrs_len && !hc_isspace(attrs[i]) && attrs[i] != '=' &&
                   attrs[i] != '/' && attrs[i] != '>') ++i;
            size_t name_len = i - name_start;
            if (!name_len) { ++i; continue; }
            while (i < attrs_len && hc_isspace(attrs[i])) ++i;
            bool has_value = i < attrs_len && attrs[i] == '=';
            size_t value_start = i, value_len = 0;
            if (has_value) {
                ++i;
                while (i < attrs_len && hc_isspace(attrs[i])) ++i;
                if (i < attrs_len && (attrs[i] == '\'' || attrs[i] == '"')) {
                    char quote = attrs[i++];
                    value_start = i;
                    while (i < attrs_len && attrs[i] != quote) ++i;
                    value_len = i - value_start;
                    if (i < attrs_len) ++i;
                } else {
                    value_start = i;
                    while (i < attrs_len && !hc_isspace(attrs[i]) && attrs[i] != '>') ++i;
                    value_len = i - value_start;
                }
            }
            bool referenced = html_css_attribute_is_referenced(ss,
                attrs + name_start, name_len);
            if (referenced == (pass == 0)) {
                /* GitHub serves data-color-mode="auto" with its light theme
                 * attribute prefilled from the requester's saved preference.
                 * LIWUS currently chooses a dark desktop theme, so expose that
                 * preference to theme-token selectors as an automatic choice. */
                const char *value = has_value ? attrs + value_start : NULL;
                size_t stored_value_len = value_len;
                if (ss && ss->prefer_dark_scheme && node->tag[0] &&
                    strcmp(node->tag, "html") == 0 && name_len == 16 &&
                    hc_prefix_ci(attrs + name_start, "data-light-theme")) {
                    value = "dark";
                    stored_value_len = 4;
                }
                html_css_node_add_attribute(node, attrs + name_start, name_len,
                    value, stored_value_len, has_value);
            }
        }
        if (node->attr_count >= HTML_CSS_MAX_NODE_ATTRS) break;
    }
    if (has_bool_attr(attrs, attrs_len, "disabled"))
        node->pseudo_state |= CSS_PSEUDO_DISABLED;
    if (has_bool_attr(attrs, attrs_len, "checked") ||
        has_bool_attr(attrs, attrs_len, "selected"))
        node->pseudo_state |= CSS_PSEUDO_CHECKED;
    bool form_control = strcmp(node->tag, "input") == 0 ||
                        strcmp(node->tag, "button") == 0 ||
                        strcmp(node->tag, "select") == 0 ||
                        strcmp(node->tag, "textarea") == 0 ||
                        strcmp(node->tag, "option") == 0 ||
                        strcmp(node->tag, "fieldset") == 0;
    if (form_control && !(node->pseudo_state & CSS_PSEUDO_DISABLED))
        node->pseudo_state |= CSS_PSEUDO_ENABLED;
}

static bool has_bool_attr(const char *attrs, size_t attrs_len, const char *key) {
    char tmp[8];
    return extract_attr(attrs, attrs_len, key, tmp, sizeof(tmp));
}

/* ---- Page Memory, Link Maps, Forms ---- */

typedef struct {
    lv_span_t *span;
    char *href;
    char element_id[64];
} span_link_entry_t;

typedef struct {
    span_link_entry_t *entries;
    int count;
    int cap;
    void *page_mem_ptr;
} span_link_map_t;

typedef struct {
    lv_obj_t *ta;
    char name[48];
    char value[128];
    bool is_hidden;
} form_field_t;

typedef struct {
    char action[HTML_URL_MAX];
    form_field_t fields[HTML_MAX_FORM_FIELDS];
    int field_count;
} html_form_t;

typedef struct {
    html_view_opts_t opts;
    char base_url[HTML_URL_MAX];
    void **ptrs;
    int ptr_count;
    int ptr_cap;
    lv_img_dsc_t *imgs[HTML_MAX_IMGS];
    int img_count;
    size_t img_bytes;
    html_form_t forms[HTML_MAX_FORMS];
    int form_count;
} html_page_mem_t;

static void *page_alloc(html_page_mem_t *pm, size_t sz) {
    void *p = kmalloc(sz);
    if (!p) return NULL;
    if (pm->ptr_count >= pm->ptr_cap) {
        int nc = pm->ptr_cap ? pm->ptr_cap * 2 : 64;
        void **np = (void **)krealloc(pm->ptrs, (size_t)nc * sizeof(void *));
        if (!np) {
            kfree(p);
            return NULL;
        }
        pm->ptrs = np;
        pm->ptr_cap = nc;
    }
    pm->ptrs[pm->ptr_count++] = p;
    return p;
}

static char *page_strdup(html_page_mem_t *pm, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *p = (char *)page_alloc(pm, len);
    if (p) memcpy(p, s, len);
    return p;
}

static void page_mem_delete_cb(lv_event_t *e) {
    html_page_mem_t *pm = (html_page_mem_t *)lv_event_get_user_data(e);
    if (!pm) return;
    for (int i = 0; i < pm->img_count; i++) {
        if (pm->imgs[i]) html_img_free(pm->imgs[i]);
    }
    for (int i = 0; i < pm->ptr_count; i++) {
        if (pm->ptrs[i]) kfree(pm->ptrs[i]);
    }
    if (pm->ptrs) kfree(pm->ptrs);
    kfree(pm);
}

/* ---- URL encoding & event callbacks ---- */

static size_t url_encode(const char *s, char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    size_t j = 0;
    for (size_t i = 0; s && s[i] && j + 4 < cap; i++) {
        unsigned char c = (unsigned char)s[i];
        if (hc_isalpha((char)c) || hc_isdigit((char)c) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = (char)c;
        } else if (c == ' ') {
            out[j++] = '+';
        } else {
            out[j++] = '%';
            out[j++] = hex[(c >> 4) & 0xF];
            out[j++] = hex[c & 0xF];
        }
    }
    out[j] = '\0';
    return j;
}

typedef struct {
    size_t tag_offset;
    uint32_t parent_token;
    uint16_t sibling_count;
    uint16_t type_index;
    uint16_t type_count;
    uint32_t type_slot;
    bool is_empty;
} html_sibling_entry_t;

typedef struct {
    uint32_t parent_token;
    char tag[24];
    uint16_t count;
    bool used;
} html_type_count_entry_t;

#define HTML_TYPE_COUNT_CAP 65536U

typedef struct {
    html_page_mem_t *pm;
    char *href;
} href_click_ctx_t;

typedef struct {
    html_page_mem_t *pm;
    char element_id[64];
    char event[16];
} dom_event_ctx_t;

static void dom_event_cb(lv_event_t *e) {
    dom_event_ctx_t *ctx = (dom_event_ctx_t *)lv_event_get_user_data(e);
    if (!ctx || !ctx->pm || !ctx->pm->opts.on_dom_event ||
        !ctx->element_id[0]) return;
    const char *value = NULL;
    if (strcmp(ctx->event, "input") == 0) {
        lv_obj_t *target = lv_event_get_target(e);
        if (target) value = lv_textarea_get_text(target);
    }
    ctx->pm->opts.on_dom_event(ctx->element_id, ctx->event, value,
                               ctx->pm->opts.user_data);
}

static void add_dom_event_callback(html_page_mem_t *pm, lv_obj_t *obj,
                                   const char *element_id, const char *event,
                                   lv_event_code_t code) {
    if (!pm || !obj || !element_id || !element_id[0] || !pm->opts.on_dom_event)
        return;
    dom_event_ctx_t *ctx = (dom_event_ctx_t *)page_alloc(pm, sizeof(*ctx));
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    ctx->pm = pm;
    hc_copy(element_id, ctx->element_id, sizeof(ctx->element_id));
    hc_copy(event, ctx->event, sizeof(ctx->event));
    lv_obj_add_event_cb(obj, dom_event_cb, code, ctx);
}

static void obj_href_click_cb(lv_event_t *e) {
    href_click_ctx_t *ctx = (href_click_ctx_t *)lv_event_get_user_data(e);
    if (!ctx || !ctx->pm || !ctx->pm->opts.on_link || !ctx->href) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (indev && lv_indev_get_scroll_obj(indev) != NULL) return;
    ctx->pm->opts.on_link(ctx->href, ctx->pm->opts.user_data);
}

static void spangroup_click_cb(lv_event_t *e) {
    span_link_map_t *map = (span_link_map_t *)lv_event_get_user_data(e);
    if (!map || !map->entries || !map->page_mem_ptr) return;
    html_page_mem_t *pm = (html_page_mem_t *)map->page_mem_ptr;
    if (!pm->opts.on_link) return;

    lv_indev_t *indev = lv_indev_get_act();
    if (!indev || lv_indev_get_scroll_obj(indev) != NULL) return;

    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_obj_t *sg = lv_event_get_target(e);
    lv_span_t *hit = lv_spangroup_get_span_at(sg, &pt);
    if (!hit) return;

    for (int i = 0; i < map->count; i++) {
        if (map->entries[i].span == hit && map->entries[i].href) {
            if (map->entries[i].element_id[0] && pm->opts.on_dom_event)
                pm->opts.on_dom_event(map->entries[i].element_id, "click", NULL,
                                      pm->opts.user_data);
            pm->opts.on_link(map->entries[i].href, pm->opts.user_data);
            return;
        }
    }
}

typedef struct {
    html_page_mem_t *pm;
    int form_idx;
} form_submit_ctx_t;

static void submit_form_by_ctx(form_submit_ctx_t *ctx) {
    if (!ctx || !ctx->pm || !ctx->pm->opts.on_link) return;
    if (ctx->form_idx < 0 || ctx->form_idx >= ctx->pm->form_count) return;

    html_form_t *f = &ctx->pm->forms[ctx->form_idx];
    char url[1024];
    hc_copy(f->action[0] ? f->action : ctx->pm->base_url, url, sizeof(url));

    char *qmark = strchr(url, '?');
    size_t ulen = strlen(url);
    bool has_q = (qmark != NULL);

    for (int i = 0; i < f->field_count; i++) {
        form_field_t *fld = &f->fields[i];
        if (!fld->name[0]) continue;
        const char *val = fld->is_hidden ? fld->value
                        : (fld->ta ? lv_textarea_get_text(fld->ta) : fld->value);
        if (!val) val = "";

        char ek[128], ev[384];
        url_encode(fld->name, ek, sizeof(ek));
        url_encode(val, ev, sizeof(ev));

        size_t need = strlen(ek) + strlen(ev) + 2;
        if (ulen + need + 1 >= sizeof(url)) break;
        url[ulen++] = has_q ? '&' : '?';
        has_q = true;
        strcpy(url + ulen, ek);
        ulen += strlen(ek);
        url[ulen++] = '=';
        strcpy(url + ulen, ev);
        ulen += strlen(ev);
        url[ulen] = '\0';
    }
    ctx->pm->opts.on_link(url, ctx->pm->opts.user_data);
}

static void form_btn_click_cb(lv_event_t *e) {
    form_submit_ctx_t *ctx = (form_submit_ctx_t *)lv_event_get_user_data(e);
    submit_form_by_ctx(ctx);
}

static void form_input_ready_cb(lv_event_t *e) {
    form_submit_ctx_t *ctx = (form_submit_ctx_t *)lv_event_get_user_data(e);
    submit_form_by_ctx(ctx);
}

static void web_input_focus_cb(lv_event_t *e) {
    html_page_mem_t *pm = (html_page_mem_t *)lv_event_get_user_data(e);
    if (!pm || !pm->opts.on_input_focus) return;
    pm->opts.on_input_focus(lv_event_get_target(e), pm->opts.user_data);
}

/* ---- Head / Stylesheet Pre-scan ---- */

static size_t find_close_tag_pos(const char *html, size_t len, size_t from, const char *tag) {
    size_t tlen = strlen(tag);
    for (size_t i = from; i + tlen + 2 < len; i++) {
        if (html[i] == '<' && html[i + 1] == '/') {
            bool match = true;
            for (size_t k = 0; k < tlen; k++) {
                if (hc_tolower(html[i + 2 + k]) != hc_tolower(tag[k])) {
                    match = false;
                    break;
                }
            }
            if (match && (hc_isspace(html[i + 2 + tlen]) || html[i + 2 + tlen] == '>')) {
                return i;
            }
        }
    }
    return len;
}

#define HTML_EXT_SCRIPT_COUNT_MAX 32U
#define HTML_EXT_SCRIPT_BYTES_MAX (4U * 1024U * 1024U)
#define HTML_EXT_SCRIPT_ONE_MAX   (2U * 1024U * 1024U)
#define HTML_SCRIPT_EXPANDED_MAX  (8U * 1024U * 1024U)

static bool html_tag_name_is(const char *html, size_t start, size_t end,
                             const char *name) {
    size_t n = strlen(name);
    if (end - start != n) return false;
    for (size_t i = 0; i < n; i++) {
        if (hc_tolower(html[start + i]) != hc_tolower(name[i])) return false;
    }
    return true;
}

static size_t html_script_module_open(char *output, size_t capacity,
                                      const char *absolute_url) {
    static const char prefix[] = "<script type=\"module\" data-liwus-module-url=\"";
    static const char suffix[] = "\">";
    size_t used = sizeof(prefix) - 1U;
    if (!output || !absolute_url || used + sizeof(suffix) > capacity) return 0;
    memcpy(output, prefix, used);
    for (size_t i = 0; absolute_url[i]; ++i) {
        const char *replacement = NULL;
        size_t replacement_len = 0;
        switch (absolute_url[i]) {
        case '&': replacement = "&amp;"; replacement_len = 5; break;
        case '"': replacement = "&quot;"; replacement_len = 6; break;
        case '\'': replacement = "&#39;"; replacement_len = 5; break;
        case '<': replacement = "&lt;"; replacement_len = 4; break;
        case '>': replacement = "&gt;"; replacement_len = 4; break;
        default: break;
        }
        if (replacement) {
            if (replacement_len > capacity - used) return 0;
            memcpy(output + used, replacement, replacement_len);
            used += replacement_len;
        } else {
            if (used >= capacity) return 0;
            output[used++] = absolute_url[i];
        }
    }
    if (sizeof(suffix) > capacity - used) return 0;
    memcpy(output + used, suffix, sizeof(suffix) - 1U);
    return used + sizeof(suffix) - 1U;
}

int html_view_inline_external_scripts(const char *content, size_t len,
                                     const char *base_url,
                                     html_fetch_resource_cb_t fetch_resource,
                                     void *user_data,
                                     uint8_t **out_content,
                                     size_t *out_len) {
    if (!out_content || !out_len) return -1;
    *out_content = NULL;
    *out_len = 0;
    if (!content || !len || len > HTML_SCRIPT_EXPANDED_MAX || !fetch_resource)
        return -1;

    uint8_t *expanded = NULL;

    size_t input_pos = 0;
    size_t copied_pos = 0;
    size_t output_pos = 0;
    size_t total_script_bytes = 0;
    unsigned fetched_count = 0;
    unsigned replacements = 0;

    while (input_pos < len) {
        if (content[input_pos] != '<') {
            input_pos++;
            continue;
        }
        if (input_pos + 4 <= len && memcmp(content + input_pos, "<!--", 4) == 0) {
            const char *comment_end = NULL;
            for (size_t p = input_pos + 4; p + 3 <= len; p++) {
                if (memcmp(content + p, "-->", 3) == 0) {
                    comment_end = content + p + 3;
                    break;
                }
            }
            input_pos = comment_end ? (size_t)(comment_end - content) : len;
            continue;
        }
        size_t name_start = input_pos + 1;
        if (name_start >= len || content[name_start] == '/' || content[name_start] == '!' ||
            content[name_start] == '?') {
            input_pos++;
            continue;
        }
        while (name_start < len && hc_isspace(content[name_start])) name_start++;
        size_t name_end = name_start;
        while (name_end < len && !hc_isspace(content[name_end]) &&
               content[name_end] != '>' && content[name_end] != '/') name_end++;
        if (name_end == name_start) {
            input_pos++;
            continue;
        }
        size_t tag_end = name_end;
        char quote = 0;
        while (tag_end < len) {
            char c = content[tag_end];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
            tag_end++;
        }
        if (tag_end >= len) break;

        bool is_script = html_tag_name_is(content, name_start, name_end, "script");
        bool is_style = html_tag_name_is(content, name_start, name_end, "style");
        if (!is_script && !is_style) {
            input_pos = tag_end + 1;
            continue;
        }

        size_t close_start = find_close_tag_pos(content, len, tag_end + 1,
                                                is_script ? "script" : "style");
        if (close_start == len) break;
        size_t close_end = close_start;
        while (close_end < len && content[close_end] != '>') close_end++;
        if (close_end < len) close_end++;

        if (is_script && fetched_count < HTML_EXT_SCRIPT_COUNT_MAX &&
            close_end > close_start) {
            const char *attrs = content + name_end;
            size_t attrs_len = tag_end - name_end;
            char src[HTML_URL_MAX], type[64];
            src[0] = type[0] = '\0';
            bool has_src = extract_attr(attrs, attrs_len, "src", src, sizeof(src)) && src[0];
            (void)extract_attr(attrs, attrs_len, "type", type, sizeof(type));
            bool is_module = hc_eq_ci(type, "module");
            bool classic = !type[0] || hc_eq_ci(type, "text/javascript") ||
                           hc_eq_ci(type, "application/javascript") ||
                           hc_eq_ci(type, "text/ecmascript") ||
                           hc_eq_ci(type, "application/ecmascript") ||
                           is_module;
            if (has_src && classic) {
                char absolute_url[HTML_URL_MAX];
                resolve_url(base_url, src, absolute_url, sizeof(absolute_url));
                if ((hc_prefix_ci(absolute_url, "https://") ||
                     hc_prefix_ci(absolute_url, "http://"))) {
                    char module_open[HTML_URL_MAX * 6U + 64U];
                    size_t module_open_len = is_module
                        ? html_script_module_open(module_open,
                            sizeof(module_open), absolute_url) : 8U;
                    uint8_t *script = NULL;
                    size_t script_len = 0;
                    bool caller_free = false;
                    fetched_count++;
                    if (module_open_len &&
                        fetch_resource(absolute_url, &script, &script_len,
                                       &caller_free, user_data) && script && script_len &&
                        script_len <= HTML_EXT_SCRIPT_ONE_MAX &&
                        script_len <= HTML_EXT_SCRIPT_BYTES_MAX - total_script_bytes &&
                        output_pos + (input_pos - copied_pos) +
                            module_open_len +
                            script_len + 9U <= HTML_SCRIPT_EXPANDED_MAX) {
                        if (!expanded)
                            expanded = (uint8_t *)kmalloc(HTML_SCRIPT_EXPANDED_MAX + 1U);
                        if (!expanded) {
                            if (caller_free) kfree(script);
                            return -1;
                        }
                        memcpy(expanded + output_pos, content + copied_pos,
                               input_pos - copied_pos);
                        output_pos += input_pos - copied_pos;
                        if (is_module) {
                            memcpy(expanded + output_pos, module_open,
                                   module_open_len);
                            output_pos += module_open_len;
                        } else {
                            memcpy(expanded + output_pos, "<script>", 8);
                            output_pos += 8;
                        }
                        memcpy(expanded + output_pos, script, script_len);
                        output_pos += script_len;
                        memcpy(expanded + output_pos, "</script>", 9);
                        output_pos += 9;
                        copied_pos = close_end;
                        total_script_bytes += script_len;
                        replacements++;
                    }
                    if (caller_free && script) kfree(script);
                }
            }
        }
        input_pos = close_end;
    }

    if (replacements) {
        if (output_pos + len - copied_pos > HTML_SCRIPT_EXPANDED_MAX) {
            kfree(expanded);
            return -1;
        }
        memcpy(expanded + output_pos, content + copied_pos, len - copied_pos);
        output_pos += len - copied_pos;
        expanded[output_pos] = '\0';
        *out_content = expanded;
        *out_len = output_pos;
        return 1;
    }
    kfree(expanded);
    return 0;
}

/* Fetch CSS once before parsing so the selector arena can be sized from the
 * page's actual stylesheet byte budget. Reuse the normal resource cache in
 * the parse pass; this avoids repeatedly growing a large kernel-heap arena. */
static size_t prefetch_css_for_reservation(const char *html, size_t len,
                                           const char *base_url,
                                           const html_view_opts_t *opts,
                                           char (*seen_urls)[HTML_URL_MAX]) {
    if (!html || !opts || !opts->fetch_resource || !seen_urls) return 0;
    size_t css_bytes = 0;
    int seen_count = 0;
    size_t i = 0;
    while (i < len && css_bytes < HTML_MAX_CSS_TOTAL) {
        if (html[i] != '<') { i++; continue; }
        if (i + 3 < len && html[i + 1] == '!' && html[i + 2] == '-' &&
            html[i + 3] == '-') {
            const char *end = strstr(html + i + 4, "-->");
            i = end ? (size_t)(end - html) + 3 : len;
            continue;
        }
        size_t name_start = i + 1;
        while (name_start < len && hc_isspace(html[name_start])) name_start++;
        if (name_start >= len || html[name_start] == '/' ||
            html[name_start] == '!' || html[name_start] == '?') {
            i++;
            continue;
        }
        size_t name_end = name_start;
        while (name_end < len && !hc_isspace(html[name_end]) &&
               html[name_end] != '>' && html[name_end] != '/') name_end++;
        if (name_end == name_start) { i++; continue; }

        char tag[24];
        hc_copy_lower(html + name_start, name_end - name_start, tag, sizeof(tag));
        size_t tag_end = name_end;
        char quote = 0;
        while (tag_end < len) {
            char c = html[tag_end];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '"' || c == '\'') quote = c;
            else if (c == '>') break;
            tag_end++;
        }
        if (tag_end >= len) break;
        const char *attrs = html + name_end;
        size_t attrs_len = tag_end - name_end;

        if (strcmp(tag, "style") == 0) {
            size_t close = find_close_tag_pos(html, len, tag_end + 1, "style");
            if (close > tag_end + 1) {
                size_t add = close - (tag_end + 1);
                size_t remaining = HTML_MAX_CSS_TOTAL - css_bytes;
                css_bytes += add < remaining ? add : remaining;
            }
            i = close;
            continue;
        }
        if (strcmp(tag, "script") == 0) {
            i = find_close_tag_pos(html, len, tag_end + 1, "script");
            continue;
        }
        if (strcmp(tag, "link") == 0 && seen_count < HTML_MAX_EXT_CSS) {
            char rel[64], href[HTML_URL_MAX], media[48];
            extract_attr(attrs, attrs_len, "rel", rel, sizeof(rel));
            extract_attr(attrs, attrs_len, "href", href, sizeof(href));
            extract_attr(attrs, attrs_len, "media", media, sizeof(media));
            if (hc_contains_ci(rel, "stylesheet") && href[0] &&
                (!media[0] || !hc_eq_ci(media, "print"))) {
                char url[HTML_URL_MAX];
                resolve_url(base_url, href, url, sizeof(url));
                if (hc_prefix_ci(url, "http://") || hc_prefix_ci(url, "https://")) {
                    bool duplicate = false;
                    for (int n = 0; n < seen_count; ++n) {
                        if (strcmp(seen_urls[n], url) == 0) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (!duplicate) {
                        hc_copy(url, seen_urls[seen_count], HTML_URL_MAX);
                        seen_count++;
                        if (opts->on_status)
                            opts->on_status("Carregando estilos CSS...", opts->user_data);
                        uint8_t *data = NULL;
                        size_t data_len = 0;
                        bool caller_free = false;
                        if (opts->fetch_resource(url, &data, &data_len,
                                                 &caller_free, opts->user_data) &&
                            data && data_len) {
                            size_t remaining = HTML_MAX_CSS_TOTAL - css_bytes;
                            css_bytes += data_len < remaining ? data_len : remaining;
                        }
                        if (caller_free && data) kfree(data);
                    }
                }
            }
        }
        i = tag_end + 1;
    }
    return css_bytes;
}

static void prescan_head_and_styles(const char *html, size_t len,
                                    const char *base_url,
                                    html_stylesheet_t *ss,
                                    const html_view_opts_t *opts,
                                    char *title_out, size_t title_out_sz) {
    if (title_out && title_out_sz) title_out[0] = '\0';

    int ext_css_count = 0;
    char (*seen_css_urls)[HTML_URL_MAX] = (char (*)[HTML_URL_MAX])kmalloc(
        (size_t)HTML_MAX_EXT_CSS * HTML_URL_MAX);
    size_t anticipated_css_bytes = prefetch_css_for_reservation(
        html, len, base_url, opts, seen_css_urls);
    if (anticipated_css_bytes) {
        /* GitHub's public CSS uses roughly one selector part per 100 bytes.
         * Reserve with headroom before parsing so old large realloc blocks do
         * not exhaust the bounded kernel heap on dense pages. */
        size_t part_capacity = anticipated_css_bytes / 80U + 1024U;
        (void)html_css_reserve_selector_parts(ss, part_capacity);
    }
    if (seen_css_urls)
        memset(seen_css_urls, 0, (size_t)HTML_MAX_EXT_CSS * HTML_URL_MAX);
    size_t css_bytes = 0;
    size_t i = 0;
    while (i < len) {
        if (html[i] != '<') { i++; continue; }
        if (i + 3 < len && html[i + 1] == '!' && html[i + 2] == '-' && html[i + 3] == '-') {
            const char *cend = strstr(html + i + 4, "-->");
            i = cend ? (size_t)(cend - html) + 3 : len;
            continue;
        }
        size_t tstart = i + 1;
        while (tstart < len && hc_isspace(html[tstart])) tstart++;
        size_t tend = tstart;
        while (tend < len && !hc_isspace(html[tend]) && html[tend] != '>' && html[tend] != '/') tend++;
        char tname[24];
        hc_copy_lower(html + tstart, tend - tstart, tname, sizeof(tname));

        size_t gt = tend;
        char quote = 0;
        while (gt < len) {
            char c = html[gt];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
            gt++;
        }
        if (gt >= len) break;
        const char *attrs = html + tend;
        size_t alen = gt - tend;

        if (strcmp(tname, "title") == 0) {
            size_t close_p = find_close_tag_pos(html, len, gt + 1, "title");
            if (title_out && title_out_sz && !title_out[0]) {
                decode_html_text(html + gt + 1, close_p - (gt + 1),
                                 title_out, title_out_sz, false);
            }
            i = close_p;
            continue;
        }

        if (strcmp(tname, "style") == 0) {
            char media[48];
            extract_attr(attrs, alen, "media", media, sizeof(media));
            size_t close_p = find_close_tag_pos(html, len, gt + 1, "style");
            if (!media[0] || !hc_eq_ci(media, "print")) {
                size_t style_len = close_p - (gt + 1);
                size_t remaining = HTML_MAX_CSS_TOTAL - css_bytes;
                if (style_len > remaining) style_len = remaining;
                if (style_len) {
                    html_css_add_stylesheet(ss, html + gt + 1, style_len);
                    css_bytes += style_len;
                }
            }
            i = close_p;
            continue;
        }

        if (strcmp(tname, "link") == 0 && opts && opts->fetch_resource) {
            char rel[64], href[HTML_URL_MAX], media[48];
            extract_attr(attrs, alen, "rel", rel, sizeof(rel));
            extract_attr(attrs, alen, "href", href, sizeof(href));
            extract_attr(attrs, alen, "media", media, sizeof(media));
            if (hc_contains_ci(rel, "stylesheet") && href[0] &&
                (!media[0] || !hc_eq_ci(media, "print"))) {
                char abs_css[HTML_URL_MAX];
                resolve_url(base_url, href, abs_css, sizeof(abs_css));
                if ((hc_prefix_ci(abs_css, "http://") ||
                     hc_prefix_ci(abs_css, "https://")) &&
                    ext_css_count < HTML_MAX_EXT_CSS) {
                    bool duplicate = false;
                    for (int seen = 0; seen < ext_css_count && seen_css_urls; ++seen) {
                        if (strcmp(seen_css_urls[seen], abs_css) == 0) {
                            duplicate = true;
                            break;
                        }
                    }
                    if (duplicate) {
                        i = gt + 1;
                        continue;
                    }
                    if (seen_css_urls)
                        hc_copy(abs_css, seen_css_urls[ext_css_count], HTML_URL_MAX);
                    ext_css_count++;
                    if (opts->on_status) opts->on_status("Carregando estilos CSS...", opts->user_data);
                    uint8_t *css_data = NULL;
                    size_t css_len = 0;
                    bool caller_free = false;
                    if (opts->fetch_resource(abs_css, &css_data, &css_len,
                                             &caller_free, opts->user_data)) {
                        if (css_data && css_len > 0) {
                            size_t use_len = (css_len > HTML_MAX_CSS_BYTES) ? HTML_MAX_CSS_BYTES : css_len;
                            size_t remaining = HTML_MAX_CSS_TOTAL - css_bytes;
                            if (use_len > remaining) use_len = remaining;
                            if (use_len) {
                                html_css_add_stylesheet(ss, (const char *)css_data, use_len);
                                css_bytes += use_len;
                            }
                        }
                        if (caller_free && css_data) kfree(css_data);
                    }
                }
            }
        }

        if (strcmp(tname, "script") == 0) {
            i = find_close_tag_pos(html, len, gt + 1, "script");
            continue;
        }
        i = gt + 1;
    }
    if (seen_css_urls) kfree(seen_css_urls);
}

/* ---- Table column pre-scanner ---- */

static void prescan_table_cols(const char *html, size_t len, size_t start_pos,
                               int *out_cols, int col_pct[HTML_MAX_TCOLS]) {
    int max_cols = 0;
    bool has_leading_th = false;
    size_t i = start_pos;
    int table_depth = 1;
    int cur_row_cols = 0;
    bool row_first_cell = false;

    while (i < len && table_depth > 0) {
        if (html[i] != '<') { i++; continue; }
        size_t tstart = i + 1;
        bool closing = false;
        if (tstart < len && html[tstart] == '/') { closing = true; tstart++; }
        size_t tend = tstart;
        while (tend < len && !hc_isspace(html[tend]) && html[tend] != '>' && html[tend] != '/') tend++;
        char tn[16];
        hc_copy_lower(html + tstart, tend - tstart, tn, sizeof(tn));

        size_t gt = tend;
        while (gt < len && html[gt] != '>') gt++;
        if (gt >= len) break;

        if (!closing && strcmp(tn, "table") == 0) {
            table_depth++;
        } else if (closing && strcmp(tn, "table") == 0) {
            table_depth--;
            if (table_depth == 0) break;
        } else if (table_depth == 1) {
            if (!closing && strcmp(tn, "tr") == 0) {
                cur_row_cols = 0;
                row_first_cell = true;
            } else if (!closing && (strcmp(tn, "td") == 0 || strcmp(tn, "th") == 0)) {
                char cs_str[12];
                int cs = 1;
                if (extract_attr(html + tend, gt - tend, "colspan", cs_str, sizeof(cs_str))) {
                    int parsed = hc_parse_int(cs_str);
                    if (parsed > 1 && parsed <= HTML_MAX_TCOLS) cs = parsed;
                }
                if (row_first_cell && strcmp(tn, "th") == 0 && cs == 1) {
                    has_leading_th = true;
                }
                row_first_cell = false;
                cur_row_cols += cs;
                if (cur_row_cols > max_cols) max_cols = cur_row_cols;
            }
        }
        i = gt + 1;
    }

    if (max_cols < 1) max_cols = 1;
    if (max_cols > HTML_MAX_TCOLS) max_cols = HTML_MAX_TCOLS;
    *out_cols = max_cols;

    if (max_cols == 2 && has_leading_th) {
        col_pct[0] = 36;
        col_pct[1] = 64;
    } else {
        int base = 100 / max_cols;
        int rem = 100 - base * max_cols;
        for (int c = 0; c < max_cols; c++) {
            col_pct[c] = base + (c < rem ? 1 : 0);
        }
    }
}

/* ---- Streaming Box & Inline Tree Parser ---- */

typedef struct {
    lv_obj_t *lv_box;         /* NULL when tag does not create its own LVGL container */
    lv_obj_t *active_flow;    /* Effective LVGL parent for children (differs when float split active) */
    lv_obj_t *float_split;    /* Active side-by-side float container, if any */
    lv_obj_t *float_main_col;
    int float_blocks_left;    /* Return to normal full width after infobox/figure height proxy */
    int avail_w;
    uint8_t grid_child_index;
    bool width_is_content;
    bool is_flex_row;

    /* Current paragraph spangroup inside active_flow */
    lv_obj_t *cur_sg;
    span_link_map_t *cur_sg_map;
    bool need_space;

    /* Form / Table / List state */
    bool is_table;
    int table_cols;
    int table_col_pct[HTML_MAX_TCOLS];
    int row_cur_col;
    bool is_tr;
    bool is_ol;
    int list_item_idx;
} box_frame_t;

typedef struct {
    html_page_mem_t *pm;
    html_stylesheet_t *ss;
    lv_obj_t *viewport;
    int viewport_w;

    css_node_info_t dom[HTML_MAX_DEPTH];
    /* Keep the current snapshot plus the four preceding element snapshots. */
    css_node_info_t sibling_history[HTML_MAX_DEPTH][HTML_CSS_MAX_SEL_CHAIN];
    uint8_t sibling_history_next[HTML_MAX_DEPTH];
    uint32_t sibling_history_seen[HTML_MAX_DEPTH];
    css_style_t style[HTML_MAX_DEPTH];
    box_frame_t box[HTML_MAX_DEPTH];
    uint16_t child_counters[HTML_MAX_DEPTH];
    uint16_t pending_child_index;
    uint16_t pending_sibling_count;
    uint16_t pending_type_index;
    uint16_t pending_type_count;
    bool pending_is_empty;
    const css_node_info_t *pending_parent;
    const css_node_info_t *pending_prev_sibling;
    html_sibling_entry_t *sibling_map;
    uint32_t sibling_map_count;
    int depth;
    int hidden_depth;        /* > 0 skips content until matching depth closes */

    int widget_count;
    int remote_img_count;

    /* Inline attributes propagated onto text spans / clicks */
    int a_depth;             /* 0 when not inside <a> */
    char cur_href[HTML_URL_MAX];
    int active_form_idx;
    int pre_depth;

    /* Pending <button> label aggregation */
    bool in_button;
    bool button_is_submit;
    char btn_text[128];

    /* Accumulator for decoded text between tags */
    char raw_buf[HTML_TEXT_BUF_SZ];
    size_t raw_len;

    lv_color_t def_link_col;
} html_parser_t;

static void capture_css_sibling_snapshot(html_parser_t *p, const char *tag,
                                         const char *attrs, size_t attrs_len) {
    if (!p || !tag || p->depth <= 0 || p->depth > HTML_MAX_DEPTH) {
        if (p) {
            p->pending_parent = NULL;
            p->pending_prev_sibling = NULL;
        }
        return;
    }

    int parent_index = p->depth - 1;
    const uint8_t slot = p->sibling_history_next[parent_index];
    const uint8_t history_count = HTML_CSS_MAX_SEL_CHAIN;
    css_node_info_t *snapshot = &p->sibling_history[parent_index][slot];
    const css_node_info_t *previous = NULL;
    if (p->sibling_history_seen[parent_index]) {
        uint8_t prev_slot = slot == 0 ? (uint8_t)(history_count - 1) : (uint8_t)(slot - 1);
        previous = &p->sibling_history[parent_index][prev_slot];
    }
    if (p->sibling_history_seen[parent_index] >= history_count) {
        uint8_t second_oldest = (uint8_t)((slot + 1) % history_count);
        p->sibling_history[parent_index][second_oldest].prev_sibling = NULL;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    hc_copy(tag, snapshot->tag, sizeof(snapshot->tag));
    extract_attr(attrs, attrs_len, "id", snapshot->id, sizeof(snapshot->id));
    extract_attr(attrs, attrs_len, "class", snapshot->cls, sizeof(snapshot->cls));
    snapshot->child_index = p->pending_child_index;
    snapshot->sibling_count = p->pending_sibling_count;
        snapshot->type_index = p->pending_type_index;
        snapshot->type_count = p->pending_type_count;
        snapshot->is_empty = p->pending_is_empty;
    snapshot->hidden_attr = has_bool_attr(attrs, attrs_len, "hidden");
    snapshot->parent = &p->dom[parent_index];
    snapshot->prev_sibling = previous;
    capture_css_attributes(p->ss, snapshot, attrs, attrs_len);

    p->pending_parent = snapshot->parent;
    p->pending_prev_sibling = previous;
    p->sibling_history_next[parent_index] = (uint8_t)((slot + 1) % history_count);
    if (p->sibling_history_seen[parent_index] < history_count)
        ++p->sibling_history_seen[parent_index];
}

static box_frame_t *top_box(html_parser_t *p) {
    for (int i = p->depth - 1; i >= 0; i--) {
        if (p->box[i].lv_box) return &p->box[i];
    }
    return &p->box[0];
}

static lv_obj_t *current_flow_parent(html_parser_t *p, box_frame_t **out_owner) {
    box_frame_t *b = top_box(p);
    if (out_owner) *out_owner = b;
    if (b->float_split && b->float_main_col) {
        if (b->float_blocks_left > 0) return b->float_main_col;
        b->float_split = NULL;
        b->float_main_col = NULL;
    }
    return b->active_flow ? b->active_flow : b->lv_box;
}

static void close_cur_spangroup(box_frame_t *b) {
    b->cur_sg = NULL;
    b->cur_sg_map = NULL;
    b->need_space = false;
}

static void close_float_split(box_frame_t *b) {
    b->float_split = NULL;
    b->float_main_col = NULL;
    b->float_blocks_left = 0;
}

static void apply_lv_box_style(lv_obj_t *obj, const css_style_t *st,
                               bool is_card_default, bool is_cell) {
    if (!obj || !st) return;

    lv_obj_set_style_shadow_width(obj, 0, 0);
    lv_obj_set_style_outline_width(obj, 0, 0);

    if (st->has_bg && !st->bg_transparent) {
        lv_obj_set_style_bg_color(obj, st->bg, 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    } else if (is_cell && is_card_default) {
        lv_obj_set_style_bg_color(obj, lv_color_hex(0xf1f5f9), 0);
        lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    }

    int bw = 0;
    for (int i = 0; i < 4; i++) {
        if (st->border_w[i] > bw) bw = st->border_w[i];
    }
    if (st->border_w[0] == HTML_CSS_UNSET && is_cell) bw = 1;

    lv_obj_set_style_border_width(obj, bw, 0);
    if (bw > 0) {
        lv_color_t bc = st->has_border_color ? st->border_color : lv_color_hex(0xcbd5e1);
        lv_obj_set_style_border_color(obj, bc, 0);
        lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
        /* Partial-border support for single bottom divider (e.g. heading/hr/row) */
        lv_border_side_t sides = LV_BORDER_SIDE_NONE;
        if (st->border_w[0] > 0 || (st->border_w[0] == HTML_CSS_UNSET && is_cell)) sides |= LV_BORDER_SIDE_TOP;
        if (st->border_w[1] > 0 || (st->border_w[1] == HTML_CSS_UNSET && is_cell)) sides |= LV_BORDER_SIDE_RIGHT;
        if (st->border_w[2] > 0 || (st->border_w[2] == HTML_CSS_UNSET && is_cell)) sides |= LV_BORDER_SIDE_BOTTOM;
        if (st->border_w[3] > 0 || (st->border_w[3] == HTML_CSS_UNSET && is_cell)) sides |= LV_BORDER_SIDE_LEFT;
        if (sides != LV_BORDER_SIDE_NONE) lv_obj_set_style_border_side(obj, sides, 0);
    } else {
        lv_obj_set_style_border_opa(obj, LV_OPA_TRANSP, 0);
    }

    int pt = (st->pad[0] != HTML_CSS_UNSET) ? st->pad[0] : (is_cell ? 5 : 0);
    int pr = (st->pad[1] != HTML_CSS_UNSET) ? st->pad[1] : (is_cell ? 7 : 0);
    int pb = (st->pad[2] != HTML_CSS_UNSET) ? st->pad[2] : (is_cell ? 5 : 0);
    int pl = (st->pad[3] != HTML_CSS_UNSET) ? st->pad[3] : (is_cell ? 7 : 0);
    lv_obj_set_style_pad_top(obj, pt, 0);
    lv_obj_set_style_pad_right(obj, pr, 0);
    lv_obj_set_style_pad_bottom(obj, pb, 0);
    lv_obj_set_style_pad_left(obj, pl, 0);

    int rad = (st->radius != HTML_CSS_UNSET) ? st->radius : 0;
    lv_obj_set_style_radius(obj, rad, 0);
}

static bool is_void_tag(const char *t) {
    return strcmp(t, "br") == 0 || strcmp(t, "hr") == 0 ||
           strcmp(t, "img") == 0 || strcmp(t, "input") == 0 ||
           strcmp(t, "meta") == 0 || strcmp(t, "link") == 0 ||
           strcmp(t, "source") == 0 || strcmp(t, "param") == 0 ||
           strcmp(t, "wbr") == 0 || strcmp(t, "col") == 0;
}

static bool is_skip_subtree_tag(const char *t) {
    return strcmp(t, "head") == 0 || strcmp(t, "script") == 0 ||
           strcmp(t, "style") == 0 || strcmp(t, "noscript") == 0 ||
           strcmp(t, "svg") == 0 || strcmp(t, "template") == 0 ||
           strcmp(t, "iframe") == 0 || strcmp(t, "canvas") == 0 ||
           strcmp(t, "audio") == 0 || strcmp(t, "video") == 0;
}

static bool is_inline_tag(const char *t) {
    return strcmp(t, "a") == 0 || strcmp(t, "span") == 0 ||
           strcmp(t, "b") == 0 || strcmp(t, "strong") == 0 ||
           strcmp(t, "i") == 0 || strcmp(t, "em") == 0 ||
           strcmp(t, "u") == 0 || strcmp(t, "s") == 0 ||
           strcmp(t, "del") == 0 || strcmp(t, "ins") == 0 ||
           strcmp(t, "code") == 0 || strcmp(t, "kbd") == 0 ||
           strcmp(t, "tt") == 0 || strcmp(t, "samp") == 0 ||
           strcmp(t, "small") == 0 || strcmp(t, "sub") == 0 ||
           strcmp(t, "sup") == 0 || strcmp(t, "abbr") == 0 ||
           strcmp(t, "cite") == 0 || strcmp(t, "mark") == 0 ||
           strcmp(t, "label") == 0 || strcmp(t, "font") == 0 ||
           strcmp(t, "time") == 0 || strcmp(t, "q") == 0;
}

static bool style_needs_box(const css_style_t *st, const char *tag, const css_style_t *parent_st) {
    if (strcmp(tag, "body") == 0) {
        if (st->display == CSS_DISP_FLEX || st->display == CSS_DISP_GRID) return true;
        if (st->margin[1] == HTML_CSS_LEN_AUTO && st->margin[3] == HTML_CSS_LEN_AUTO) return true;
        if (st->max_width_px > 0 || st->width_px > 0 ||
            (st->width_pct > 0 && st->width_pct < 100)) {
            return true;
        }
        for (int i = 0; i < 4; i++) {
            if (st->border_w[i] > 0 || st->pad[i] > 4) return true;
        }
        return false;
    }
    if (strcmp(tag, "table") == 0 ||
        strcmp(tag, "tr") == 0 || strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0 ||
        strcmp(tag, "pre") == 0 || strcmp(tag, "blockquote") == 0 ||
        strcmp(tag, "hr") == 0 || strcmp(tag, "figure") == 0 ||
        strcmp(tag, "form") == 0 || strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
        return true;
    }
    if (st->display == CSS_DISP_FLEX || st->display == CSS_DISP_INLINE_FLEX ||
        st->display == CSS_DISP_GRID || st->display == CSS_DISP_TABLE ||
        st->display == CSS_DISP_TABLE_ROW || st->display == CSS_DISP_TABLE_CELL ||
        st->display == CSS_DISP_INLINE_BLOCK) {
        return true;
    }
    if (parent_st && (parent_st->display == CSS_DISP_FLEX ||
                      parent_st->display == CSS_DISP_INLINE_FLEX ||
                      parent_st->display == CSS_DISP_GRID)) {
        if (strcmp(tag, "button") != 0) return true;
    }
    if (st->float_mode != CSS_FLOAT_NONE) return true;
    if (st->has_bg && !st->bg_transparent) return true;
    for (int i = 0; i < 4; i++) {
        if (st->border_w[i] > 0) return true;
        if (st->pad[i] > 2) return true;
    }
    if (st->margin[1] == HTML_CSS_LEN_AUTO && st->margin[3] == HTML_CSS_LEN_AUTO) return true;
    if (st->width_px > 0 || st->width_pct > 0 || st->max_width_px > 0) return true;
    return false;
}

static void add_span_link_entry(html_page_mem_t *pm, span_link_map_t *map,
                                lv_span_t *span, const char *href,
                                const char *element_id) {
    if (!map || !span || !href || !href[0]) return;
    if (map->count >= map->cap) {
        int nc = map->cap ? map->cap * 2 : 16;
        span_link_entry_t *ne = (span_link_entry_t *)page_alloc(pm, (size_t)nc * sizeof(span_link_entry_t));
        if (!ne) return;
        if (map->entries && map->count > 0) {
            memcpy(ne, map->entries, (size_t)map->count * sizeof(span_link_entry_t));
        }
        map->entries = ne;
        map->cap = nc;
    }
    map->entries[map->count].span = span;
    map->entries[map->count].href = page_strdup(pm, href);
    hc_copy(element_id, map->entries[map->count].element_id,
            sizeof(map->entries[map->count].element_id));
    map->count++;
}

static void flush_text_buffer(html_parser_t *p) {
    if (p->raw_len == 0) return;

    if (p->hidden_depth > 0) {
        p->raw_len = 0;
        return;
    }

    if (p->in_button) {
        char dec[128];
        decode_html_text(p->raw_buf, p->raw_len, dec, sizeof(dec), false);
        size_t blen = strlen(p->btn_text);
        if (blen + strlen(dec) + 1 < sizeof(p->btn_text)) {
            strcat(p->btn_text, dec);
        }
        p->raw_len = 0;
        return;
    }

    bool preserve_ws = (p->pre_depth > 0);
    char dec[HTML_TEXT_BUF_SZ];
    size_t dlen = decode_html_text(p->raw_buf, p->raw_len, dec, sizeof(dec), preserve_ws);
    p->raw_len = 0;
    if (dlen == 0) return;

    /* Trim leading whitespace only when we don't have an active text flow */
    box_frame_t *owner = NULL;
    lv_obj_t *parent = current_flow_parent(p, &owner);
    if (!parent || !owner) return;

    const char *txt = dec;
    if (!preserve_ws && owner->cur_sg == NULL) {
        while (*txt && hc_isspace(*txt)) txt++;
    }
    if (!txt[0]) return;

    if (owner->cur_sg == NULL) {
        if (p->widget_count >= HTML_MAX_WIDGETS) return;
        lv_obj_t *sg = lv_spangroup_create(parent);
        if (!sg) return;
        p->widget_count++;

        bool shrink_fit = (owner->width_is_content && owner->float_main_col != parent) || owner->is_flex_row;
        if (shrink_fit) {
            lv_obj_set_width(sg, LV_SIZE_CONTENT);
            lv_obj_set_style_max_width(sg, owner->avail_w, 0);
            lv_spangroup_set_mode(sg, LV_SPAN_MODE_EXPAND);
        } else {
            lv_obj_set_width(sg, LV_PCT(100));
            lv_spangroup_set_mode(sg, LV_SPAN_MODE_BREAK);
        }
        lv_obj_set_height(sg, LV_SIZE_CONTENT);
        lv_spangroup_set_overflow(sg, LV_SPAN_OVERFLOW_CLIP);
        lv_obj_clear_flag(sg, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(sg, LV_OBJ_FLAG_GESTURE_BUBBLE);

        const css_style_t *cur_st = &p->style[p->depth - 1];
        if (cur_st->text_align == CSS_ALIGN_CENTER) lv_spangroup_set_align(sg, LV_TEXT_ALIGN_CENTER);
        else if (cur_st->text_align == CSS_ALIGN_RIGHT) lv_spangroup_set_align(sg, LV_TEXT_ALIGN_RIGHT);
        else lv_spangroup_set_align(sg, LV_TEXT_ALIGN_LEFT);

        owner->cur_sg = sg;
        owner->cur_sg_map = NULL;
        owner->need_space = false;

        if (owner->float_split && owner->float_blocks_left > 0) {
            owner->float_blocks_left--;
        }
    }

    const css_style_t *st = &p->style[p->depth - 1];
    int font_px = (st->font_px > 0) ? st->font_px : 14;
    const lv_font_t *font = html_font_for_px(font_px);

    lv_color_t fg = st->has_color ? st->color : lv_color_hex(0x1f2328);
    if (p->a_depth > 0 && !st->own_color && !st->has_color) fg = p->def_link_col;

    bool under = st->underline;
    bool strike = st->strike;

    char final_txt[HTML_TEXT_BUF_SZ];
    if (!preserve_ws && owner->need_space && txt[0] != ' ' &&
        txt[0] != '.' && txt[0] != ',' && txt[0] != ';' && txt[0] != ':' &&
        txt[0] != ')' && txt[0] != ']' && txt[0] != '!' && txt[0] != '?') {
        final_txt[0] = ' ';
        hc_copy(txt, final_txt + 1, sizeof(final_txt) - 1);
    } else {
        hc_copy(txt, final_txt, sizeof(final_txt));
    }

    size_t flen = strlen(final_txt);
    owner->need_space = (flen > 0 && final_txt[flen - 1] != ' ');

    lv_span_t *sp = lv_spangroup_new_span(owner->cur_sg);
    if (!sp) return;
    lv_span_set_text(sp, final_txt);
    lv_style_set_text_font(&sp->style, font);
    lv_style_set_text_color(&sp->style, fg);

    lv_text_decor_t decor = LV_TEXT_DECOR_NONE;
    if (under) decor |= LV_TEXT_DECOR_UNDERLINE;
    if (strike) decor |= LV_TEXT_DECOR_STRIKETHROUGH;
    lv_style_set_text_decor(&sp->style, decor);

    if (lv_spangroup_get_mode(owner->cur_sg) == LV_SPAN_MODE_EXPAND && owner->avail_w > 0) {
        if (lv_spangroup_get_expand_width(owner->cur_sg, (uint32_t)(owner->avail_w + 1)) >
            (uint32_t)owner->avail_w) {
            lv_obj_set_width(owner->cur_sg, owner->avail_w);
            lv_spangroup_set_mode(owner->cur_sg, LV_SPAN_MODE_BREAK);
        }
    }

    if (p->a_depth > 0 && p->cur_href[0]) {
        if (!owner->cur_sg_map) {
            span_link_map_t *map = (span_link_map_t *)page_alloc(p->pm, sizeof(span_link_map_t));
            if (map) {
                memset(map, 0, sizeof(*map));
                map->page_mem_ptr = p->pm;
                owner->cur_sg_map = map;
                lv_obj_add_flag(owner->cur_sg, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(owner->cur_sg, spangroup_click_cb, LV_EVENT_CLICKED, map);
            }
        }
        const char *anchor_id = (p->a_depth > 0 && p->a_depth <= HTML_MAX_DEPTH)
                              ? p->dom[p->a_depth - 1].id : "";
        add_span_link_entry(p->pm, owner->cur_sg_map, sp, p->cur_href,
                            anchor_id);
    }
}

/* ---- Tag Open Handlers ---- */

static void handle_img_tag(html_parser_t *p, const char *attrs, size_t alen) {
    char raw_src[HTML_URL_MAX], alt[128], w_str[16], h_str[16];
    extract_attr(attrs, alen, "src", raw_src, sizeof(raw_src));
    if (!raw_src[0] || hc_contains_ci(raw_src, "R0lGODlh")) {
        char dsrc[HTML_URL_MAX];
        if (extract_attr(attrs, alen, "data-src", dsrc, sizeof(dsrc)) && dsrc[0]) {
            hc_copy(dsrc, raw_src, sizeof(raw_src));
        }
    }
    extract_attr(attrs, alen, "alt", alt, sizeof(alt));
    extract_attr(attrs, alen, "width", w_str, sizeof(w_str));
    extract_attr(attrs, alen, "height", h_str, sizeof(h_str));

    if (!raw_src[0]) return;
    int attr_w = hc_parse_int(w_str);
    int attr_h = hc_parse_int(h_str);
    /* Ignore 1x1 tracking pixels and SVG/WEBP that our raster decoder doesn't decode */
    if ((attr_w == 1 && attr_h == 1) || hc_prefix_ci(raw_src, "data:image/svg")) return;

    char abs_url[HTML_URL_MAX];
    resolve_url(p->pm->base_url, raw_src, abs_url, sizeof(abs_url));
    if (!abs_url[0]) return;

    /* Strip query string check for .svg / .webp extension */
    char clean_path[HTML_URL_MAX];
    hc_copy(abs_url, clean_path, sizeof(clean_path));
    char *q = strchr(clean_path, '?');
    if (q) *q = '\0';
    if (ends_with_ci(clean_path, ".svg") || ends_with_ci(clean_path, ".webp")) {
        return;
    }

    box_frame_t *owner = NULL;
    lv_obj_t *parent = current_flow_parent(p, &owner);
    if (!parent || !owner) return;

    uint8_t *raw = NULL;
    size_t rlen = 0;
    bool caller_free = false;

    if (hc_prefix_ci(abs_url, "data:image/")) {
        if (!html_img_decode_data_uri(abs_url, &raw, &rlen)) return;
        caller_free = true;
    } else if (p->remote_img_count < HTML_MAX_IMGS && p->pm->opts.fetch_resource &&
               p->pm->img_count < HTML_MAX_IMGS) {
        p->remote_img_count++;
        if (p->pm->opts.on_status) p->pm->opts.on_status("Carregando imagens...", p->pm->opts.user_data);
        if (!p->pm->opts.fetch_resource(abs_url, &raw, &rlen, &caller_free, p->pm->opts.user_data)) {
            return;
        }
    } else {
        return;
    }

    int max_w = owner->avail_w - 16;
    if (max_w < 80) max_w = 80;
    if (attr_w > 0 && attr_w < max_w) max_w = attr_w;
    int max_h = (attr_h > 0 && attr_h < 380) ? attr_h : 340;

    lv_img_dsc_t *dsc = html_img_decode(raw, rlen, max_w, max_h);
    if (caller_free && raw) kfree(raw);
    if (!dsc) return;

    if (dsc->data_size > HTML_MAX_IMAGE_BYTES ||
        p->pm->img_bytes > HTML_MAX_IMAGE_BYTES - dsc->data_size) {
        html_img_free(dsc);
        return;
    }

    if (p->pm->img_count < HTML_MAX_IMGS && p->widget_count < HTML_MAX_WIDGETS) {
        close_cur_spangroup(owner);
        lv_obj_t *img = lv_img_create(parent);
        if (!img) {
            html_img_free(dsc);
            return;
        }
        p->widget_count++;
        p->pm->imgs[p->pm->img_count++] = dsc;
        p->pm->img_bytes += dsc->data_size;
        lv_img_set_src(img, dsc);
        lv_obj_set_size(img, (lv_coord_t)dsc->header.w,
                        (lv_coord_t)dsc->header.h);
        lv_obj_set_style_radius(img, 4, 0);
        lv_obj_set_style_clip_corner(img, true, 0);

        if (p->a_depth > 0 && p->cur_href[0]) {
            href_click_ctx_t *hctx = (href_click_ctx_t *)page_alloc(p->pm, sizeof(href_click_ctx_t));
            if (hctx) {
                hctx->pm = p->pm;
                hctx->href = page_strdup(p->pm, p->cur_href);
                lv_obj_add_flag(img, LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_event_cb(img, obj_href_click_cb, LV_EVENT_CLICKED, hctx);
            }
        }
    } else {
        html_img_free(dsc);
    }
}



static void handle_input_tag(html_parser_t *p, const char *attrs, size_t alen) {
    char type[24], name[48], val[128], ph[96], element_id[64];
    extract_attr(attrs, alen, "type", type, sizeof(type));
    if (!type[0]) hc_copy("text", type, sizeof(type));
    extract_attr(attrs, alen, "name", name, sizeof(name));
    extract_attr(attrs, alen, "value", val, sizeof(val));
    extract_attr(attrs, alen, "placeholder", ph, sizeof(ph));
    extract_attr(attrs, alen, "id", element_id, sizeof(element_id));

    char dec_val[128], dec_ph[96];
    decode_html_text(val, strlen(val), dec_val, sizeof(dec_val), true);
    decode_html_text(ph, strlen(ph), dec_ph, sizeof(dec_ph), true);

    int fidx = p->active_form_idx;
    html_form_t *frm = (fidx >= 0 && fidx < p->pm->form_count) ? &p->pm->forms[fidx] : NULL;
    css_style_t input_style;
    bool have_input_style = false;

    if (hc_eq_ci(type, "hidden")) {
        if (frm && frm->field_count < HTML_MAX_FORM_FIELDS && name[0]) {
            form_field_t *fld = &frm->fields[frm->field_count++];
            fld->ta = NULL;
            hc_copy(name, fld->name, sizeof(fld->name));
            hc_copy(dec_val, fld->value, sizeof(fld->value));
            fld->is_hidden = true;
        }
        return;
    }

    if (p->depth < HTML_MAX_DEPTH) {
        css_node_info_t *nd = &p->dom[p->depth];
        memset(nd, 0, sizeof(*nd));
        hc_copy("input", nd->tag, sizeof(nd->tag));
        extract_attr(attrs, alen, "id", nd->id, sizeof(nd->id));
        extract_attr(attrs, alen, "class", nd->cls, sizeof(nd->cls));
        nd->hidden_attr = has_bool_attr(attrs, alen, "hidden");
        nd->child_index = p->pending_child_index;
        nd->sibling_count = p->pending_sibling_count;
        nd->type_index = p->pending_type_index;
        nd->type_count = p->pending_type_count;
        nd->is_empty = p->pending_is_empty;
        nd->parent = p->pending_parent;
        nd->prev_sibling = p->pending_prev_sibling;
        capture_css_attributes(p->ss, nd, attrs, alen);
        char inline_style[512];
        extract_attr(attrs, alen, "style", inline_style, sizeof(inline_style));
        css_style_t ics;
        html_css_compute(p->ss, p->dom, p->depth + 1, inline_style,
                         (p->depth > 0) ? &p->style[p->depth - 1] : NULL, &ics);
        if (ics.display == CSS_DISP_NONE || ics.visibility == CSS_VIS_HIDDEN ||
            ics.offscreen_hidden || ics.is_sr_only ||
            (ics.opacity_zero && (hc_eq_ci(type, "checkbox") || hc_eq_ci(type, "radio")))) {
            return;
        }
        input_style = ics;
        have_input_style = true;
    }

    box_frame_t *owner = NULL;
    lv_obj_t *parent = current_flow_parent(p, &owner);
    if (!parent || !owner || p->widget_count >= HTML_MAX_WIDGETS) return;
    close_cur_spangroup(owner);

    if (hc_eq_ci(type, "submit") || hc_eq_ci(type, "button") || hc_eq_ci(type, "reset")) {
        lv_obj_t *btn = lv_btn_create(parent);
        if (!btn) return;
        p->widget_count++;
        lv_obj_set_size(btn, LV_SIZE_CONTENT, 30);
        if (have_input_style) {
            if (input_style.width_px >= 0 && input_style.width_px != HTML_CSS_UNSET)
                lv_obj_set_width(btn, input_style.width_px);
            else if (input_style.width_pct > 0 && input_style.width_pct <= 100)
                lv_obj_set_width(btn, (owner->avail_w * input_style.width_pct) / 100);
            if (input_style.flex_grow)
                lv_obj_set_flex_grow(btn, input_style.flex_grow);
        }
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x0b57d0), 0);
        lv_obj_set_style_radius(btn, 6, 0);
        lv_obj_set_style_pad_hor(btn, 12, 0);
        lv_obj_set_style_pad_ver(btn, 4, 0);
        lv_obj_t *lbl = lv_label_create(btn);
        lv_obj_set_style_text_font(lbl, html_font_for_px(14), 0);
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
        lv_label_set_text(lbl, dec_val[0] ? dec_val : "Enviar");
        lv_obj_center(lbl);
        add_dom_event_callback(p->pm, btn, element_id, "click", LV_EVENT_CLICKED);

        if (frm) {
            form_submit_ctx_t *fctx = (form_submit_ctx_t *)page_alloc(p->pm, sizeof(form_submit_ctx_t));
            if (fctx) {
                fctx->pm = p->pm;
                fctx->form_idx = fidx;
                lv_obj_add_event_cb(btn, form_btn_click_cb, LV_EVENT_CLICKED, fctx);
            }
        }
        return;
    }

    if (hc_eq_ci(type, "checkbox") || hc_eq_ci(type, "radio")) {
        lv_obj_t *cb = lv_checkbox_create(parent);
        if (cb) {
            p->widget_count++;
            lv_checkbox_set_text(cb, dec_val[0] ? dec_val : "");
            if (has_bool_attr(attrs, alen, "checked")) lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        return;
    }

    lv_obj_t *ta = lv_textarea_create(parent);
    if (!ta) return;
    p->widget_count++;
    lv_textarea_set_one_line(ta, true);
    int iw = owner->avail_w - 24;
    if (iw > 260) iw = 260;
    if (iw < 110) iw = 110;
    if (have_input_style) {
        if (input_style.width_px >= 0 && input_style.width_px != HTML_CSS_UNSET)
            iw = input_style.width_px;
        else if (input_style.width_pct > 0 && input_style.width_pct <= 100)
            iw = (owner->avail_w * input_style.width_pct) / 100;
        if (input_style.max_width_px > 0 && iw > input_style.max_width_px)
            iw = input_style.max_width_px;
        if (iw < 40) iw = 40;
        if (iw > owner->avail_w) iw = owner->avail_w;
    }
    lv_obj_set_width(ta, iw);
    if (have_input_style && input_style.flex_grow)
        lv_obj_set_flex_grow(ta, input_style.flex_grow);
    lv_obj_set_style_bg_color(ta, lv_color_white(), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0x111827), 0);
    lv_obj_set_style_text_font(ta, html_font_for_px(14), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x9ca3af), 0);
    lv_obj_set_style_radius(ta, 6, 0);
    lv_obj_set_style_pad_hor(ta, 8, 0);
    lv_obj_set_style_pad_ver(ta, 5, 0);

    if (hc_eq_ci(type, "password")) lv_textarea_set_password_mode(ta, true);
    if (dec_ph[0]) lv_textarea_set_placeholder_text(ta, dec_ph);
    if (dec_val[0]) lv_textarea_set_text(ta, dec_val);

    lv_obj_add_event_cb(ta, web_input_focus_cb, LV_EVENT_PRESSED, p->pm);
    add_dom_event_callback(p->pm, ta, element_id, "input", LV_EVENT_VALUE_CHANGED);
    add_dom_event_callback(p->pm, ta, element_id, "change", LV_EVENT_READY);
    if (frm) {
        if (frm->field_count < HTML_MAX_FORM_FIELDS && name[0]) {
            form_field_t *fld = &frm->fields[frm->field_count++];
            fld->ta = ta;
            hc_copy(name, fld->name, sizeof(fld->name));
            fld->value[0] = '\0';
            fld->is_hidden = false;
        }
        form_submit_ctx_t *fctx = (form_submit_ctx_t *)page_alloc(p->pm, sizeof(form_submit_ctx_t));
        if (fctx) {
            fctx->pm = p->pm;
            fctx->form_idx = fidx;
            lv_obj_add_event_cb(ta, form_input_ready_cb, LV_EVENT_READY, fctx);
        }
    }
}

static void apply_default_tag_style(const char *tag, css_style_t *st, const html_parser_t *p) {
    if (strcmp(tag, "h1") == 0) {
        if (!st->own_font_px) st->font_px = 24;
        if (!st->has_bold) { st->has_bold = true; st->bold = true; }
        if (st->margin[0] == HTML_CSS_UNSET) st->margin[0] = 6;
        if (st->margin[2] == HTML_CSS_UNSET) st->margin[2] = 6;
    } else if (strcmp(tag, "h2") == 0) {
        if (!st->own_font_px) st->font_px = 20;
        if (!st->has_bold) { st->has_bold = true; st->bold = true; }
        if (st->margin[0] == HTML_CSS_UNSET) st->margin[0] = 8;
        if (st->margin[2] == HTML_CSS_UNSET) st->margin[2] = 4;
    } else if (strcmp(tag, "h3") == 0) {
        if (!st->own_font_px) st->font_px = 18;
        if (!st->has_bold) { st->has_bold = true; st->bold = true; }
    } else if (strcmp(tag, "h4") == 0 || strcmp(tag, "h5") == 0 || strcmp(tag, "h6") == 0) {
        if (!st->own_font_px) st->font_px = 16;
        if (!st->has_bold) { st->has_bold = true; st->bold = true; }
    } else if (strcmp(tag, "b") == 0 || strcmp(tag, "strong") == 0 || strcmp(tag, "th") == 0) {
        if (!st->has_bold) { st->has_bold = true; st->bold = true; }
    } else if (strcmp(tag, "i") == 0 || strcmp(tag, "em") == 0) {
        if (!st->has_italic) { st->has_italic = true; st->italic = true; }
    } else if (strcmp(tag, "u") == 0 || strcmp(tag, "ins") == 0) {
        if (!st->own_underline) { st->has_underline = true; st->underline = true; }
    } else if (strcmp(tag, "s") == 0 || strcmp(tag, "del") == 0) {
        if (!st->has_strike) { st->has_strike = true; st->strike = true; }
    } else if (strcmp(tag, "small") == 0 || strcmp(tag, "sub") == 0 || strcmp(tag, "sup") == 0) {
        if (!st->own_font_px && (!st->font_px || st->font_px >= 14)) st->font_px = 12;
    } else if (strcmp(tag, "code") == 0 || strcmp(tag, "kbd") == 0 || strcmp(tag, "tt") == 0) {
        if (!st->own_color) { st->has_color = true; st->color = lv_color_hex(0x9f1239); }
        if (!st->own_font_px) st->font_px = 12;
    } else if (strcmp(tag, "pre") == 0) {
        if (!st->has_bg) { st->has_bg = true; st->bg_transparent = false; st->bg = lv_color_hex(0xf6f8fa); }
        if (st->border_w[0] == HTML_CSS_UNSET) {
            st->border_w[0] = st->border_w[1] = st->border_w[2] = st->border_w[3] = 1;
            st->has_border_color = true;
            st->border_color = lv_color_hex(0xd0d7de);
        }
        if (st->pad[0] == HTML_CSS_UNSET) st->pad[0] = st->pad[1] = st->pad[2] = st->pad[3] = 10;
        if (st->radius == HTML_CSS_UNSET) st->radius = 6;
        if (!st->own_font_px) st->font_px = 12;
        st->preformatted = true;
    } else if (strcmp(tag, "blockquote") == 0) {
        if (!st->has_bg) { st->has_bg = true; st->bg_transparent = false; st->bg = lv_color_hex(0xf8fafc); }
        if (st->border_w[3] == HTML_CSS_UNSET) {
            st->border_w[0] = st->border_w[1] = st->border_w[2] = 0;
            st->border_w[3] = 4;
            st->has_border_color = true;
            st->border_color = lv_color_hex(0x94a3b8);
        }
        if (st->pad[0] == HTML_CSS_UNSET) st->pad[0] = st->pad[1] = st->pad[2] = st->pad[3] = 8;
    } else if (strcmp(tag, "a") == 0) {
        if (!st->own_color) { st->has_color = true; st->color = p->def_link_col; }
        if (!st->own_underline) { st->has_underline = true; st->underline = true; }
    } else if (strcmp(tag, "center") == 0) {
        st->text_align = CSS_ALIGN_CENTER;
    }
}

#define HTML_FLEX_ORDER_TAG 0x4c570000U

static void html_place_ordered_flex_child(lv_obj_t *parent, lv_obj_t *child,
                                          int16_t order) {
    if (!parent || !child) return;

    lv_obj_set_user_data(child, (void *)(uintptr_t)
                         (HTML_FLEX_ORDER_TAG | (uint16_t)(order - INT16_MIN)));
    uint32_t child_count = lv_obj_get_child_cnt(parent);
    int32_t insert_at = (int32_t)child_count - 1;
    for (uint32_t i = 0; i + 1 < child_count; ++i) {
        lv_obj_t *sibling = lv_obj_get_child(parent, (int32_t)i);
        uintptr_t data = sibling ? (uintptr_t)lv_obj_get_user_data(sibling) : 0;
        int16_t sibling_order = 0;
        if ((data & 0xffff0000U) == HTML_FLEX_ORDER_TAG)
            sibling_order = (int16_t)((uint16_t)data + INT16_MIN);
        if (sibling_order > order) {
            insert_at = (int32_t)i;
            break;
        }
    }
    lv_obj_move_to_index(child, insert_at);
}

static lv_obj_t *create_container_box(html_parser_t *p, const char *tag,
                                      const css_style_t *st, const char *attrs, size_t alen,
                                      const char *html, size_t html_len, size_t tag_end_pos,
                                      box_frame_t *frame) {
    box_frame_t *owner = NULL;
    lv_obj_t *parent = current_flow_parent(p, &owner);
    if (!parent || !owner || p->widget_count >= HTML_MAX_WIDGETS) return NULL;

    close_cur_spangroup(owner);
    if (st->clear_both) {
        close_float_split(owner);
        parent = owner->active_flow ? owner->active_flow : owner->lv_box;
    }

    const css_style_t *parent_st = (p->depth >= 2) ? &p->style[p->depth - 2] : NULL;
    bool parent_is_flex = parent_st && (parent_st->display == CSS_DISP_FLEX ||
                                        parent_st->display == CSS_DISP_INLINE_FLEX ||
                                        parent_st->display == CSS_DISP_GRID);
    uint8_t auto_grid_slot = (parent_st && parent_st->display == CSS_DISP_GRID)
                           ? owner->grid_child_index++ : 0;
    uint8_t grid_slot = (st->grid_col_start > 0)
                      ? (uint8_t)(st->grid_col_start - 1) : auto_grid_slot;
    uint8_t grid_span = st->grid_col_span ? st->grid_col_span : 1;

    int box_w = owner->avail_w;
    bool row_flex_basis = parent_st && parent_st->flex_dir == CSS_FLEX_ROW &&
                          (st->flex_basis_zero ||
                           (st->flex_basis_pct >= 0 && st->flex_basis_pct <= 100));
    bool full_track_flex_item = parent_is_flex &&
                                parent_st->flex_dir == CSS_FLEX_ROW &&
                                parent_st->flex_wrap == CSS_WRAP_WRAP &&
                                row_flex_basis && st->flex_basis_pct >= 100;
    bool parent_stretches_width = parent_is_flex &&
                                  parent_st->flex_dir == CSS_FLEX_COL &&
                                  parent_st->align_items == CSS_AITEMS_STRETCH &&
                                  st->width_px <= 0 && st->width_pct <= 0 &&
                                  !row_flex_basis;
    if (row_flex_basis && st->flex_basis_zero) {
        box_w = 0;
    } else if (row_flex_basis) {
        box_w = (owner->avail_w * st->flex_basis_pct) / 100;
    } else if (st->width_px > 0) {
        box_w = (st->width_px < owner->avail_w) ? st->width_px : owner->avail_w;
    } else if (st->width_pct > 0 && st->width_pct <= 100) {
        box_w = (owner->avail_w * st->width_pct) / 100;
    }
    /* LVGL dimensions are border-box. CSS content-box widths exclude these
     * edges, so expand explicit content widths before assigning the object. */
    if (!st->border_box && !row_flex_basis &&
        (st->width_px > 0 || (st->width_pct > 0 && st->width_pct <= 100))) {
        for (int edge = 1; edge <= 3; edge += 2) {
            if (st->pad[edge] > 0) box_w += st->pad[edge];
            if (st->border_w[edge] > 0) box_w += st->border_w[edge];
        }
    }
    if (st->max_width_px > 0 && st->max_width_px < box_w) box_w = st->max_width_px;
    if (st->min_width_px > box_w && st->min_width_px <= owner->avail_w) box_w = st->min_width_px;
    if (!row_flex_basis && box_w < 60 && owner->avail_w >= 60) box_w = 60;

    bool grid_track_applied = false;
    if (parent_st && parent_st->display == CSS_DISP_GRID &&
        st->width_px <= 0 && !(st->width_pct > 0 && st->width_pct <= 100) &&
        parent_st->grid_col_count > 0) {
        uint8_t column_count = parent_st->grid_col_count;
        if (column_count > HTML_CSS_MAX_GRID_COLS)
            column_count = HTML_CSS_MAX_GRID_COLS;
        uint8_t track = grid_slot % column_count;
        if (grid_span > column_count - track)
            grid_span = column_count - track;
        int gap = (parent_st->column_gap != HTML_CSS_UNSET)
                ? parent_st->column_gap
                : ((parent_st->gap != HTML_CSS_UNSET) ? parent_st->gap : 0);
        int64_t grid_area = owner->avail_w;
        if (column_count > 1)
            grid_area -= (int64_t)gap * (column_count - 1);
        if (grid_area < 0) grid_area = 0;
        int64_t fixed_width = 0;
        int64_t fr_units = 0;
        int64_t track_width[HTML_CSS_MAX_GRID_COLS] = {0};
        bool auto_track[HTML_CSS_MAX_GRID_COLS] = {false};
        for (uint8_t i = 0; i < column_count; ++i) {
            int16_t value = parent_st->grid_col_value[i];
            switch (parent_st->grid_col_kind[i]) {
                case 1:
                    track_width[i] = value;
                    fixed_width += value;
                    break;
                case 2:
                    track_width[i] = (grid_area * value) / 100;
                    fixed_width += track_width[i];
                    break;
                case 5:
                    track_width[i] = (grid_area * value) / 100 +
                                     parent_st->grid_col_offset[i];
                    if (track_width[i] < 0) track_width[i] = 0;
                    fixed_width += track_width[i];
                    break;
                case 3:
                    fr_units += value > 0 ? value : 1;
                    break;
                case 4:
                    auto_track[i] = true;
                    break;
            }
        }
        int64_t auto_width = 0;
        for (uint8_t i = 0; i < column_count; ++i) {
            if (!auto_track[i]) continue;
            /* Intrinsic auto sizing needs a completed layout tree. Use a
             * bounded sidebar estimate for this streaming pass, then leave
             * the resulting track fixed so LVGL flex-grow cannot redistribute
             * the grid's explicit fr proportions. */
            int width = owner->avail_w / 3;
            if (width < 220) width = 220;
            if (width > 320) width = 320;
            if (width > owner->avail_w / 2) width = owner->avail_w / 2;
            track_width[i] = width;
            auto_width += width;
        }
        fixed_width += auto_width;
        int64_t fr_space = grid_area - fixed_width;
        if (fr_space < 0) fr_space = 0;
        if (fr_units > 0) {
            for (uint8_t i = 0; i < column_count; ++i) {
                if (parent_st->grid_col_kind[i] != 3) continue;
                int64_t units = parent_st->grid_col_value[i] > 0
                              ? parent_st->grid_col_value[i] : 1;
                track_width[i] = (fr_space * units) / fr_units;
            }
        } else {
            /* A grid with only intrinsic tracks gets an even fallback until
             * post-content intrinsic sizing is available. */
            int64_t assigned = 0;
            uint8_t intrinsic_count = 0;
            for (uint8_t i = 0; i < column_count; ++i) {
                if (parent_st->grid_col_kind[i] == 0 || auto_track[i])
                    intrinsic_count++;
            }
            if (intrinsic_count) {
                int64_t each = fr_space / intrinsic_count;
                for (uint8_t i = 0; i < column_count; ++i) {
                    if (parent_st->grid_col_kind[i] == 0) {
                        track_width[i] = each;
                        assigned += each;
                    }
                }
            }
            (void)assigned;
        }

        int64_t item_width = 0;
        for (uint8_t i = track; i < track + grid_span; ++i)
            item_width += track_width[i];
        if (grid_span > 1) item_width += (int64_t)gap * (grid_span - 1);
        if (item_width > owner->avail_w) item_width = owner->avail_w;
        if (item_width < 0) item_width = 0;
        box_w = (int)item_width;
        grid_track_applied = true;
    }

    /* Handle float left/right side-by-side layout when space permits */
    lv_obj_t *actual_parent = parent;
    bool is_floating = (st->float_mode == CSS_FLOAT_LEFT || st->float_mode == CSS_FLOAT_RIGHT) &&
                       !parent_is_flex && !owner->is_table && !owner->is_tr &&
                       owner->avail_w >= 580;

    if (is_floating) {
        int fw = (st->width_px > 0 && st->width_px < owner->avail_w / 2)
                 ? st->width_px : (owner->avail_w * 34) / 100;
        if (fw < 180) fw = 180;
        if (fw > 340) fw = 340;
        box_w = fw;

        lv_obj_t *split = lv_obj_create(owner->lv_box);
        if (split && p->widget_count + 3 < HTML_MAX_WIDGETS) {
            p->widget_count++;
            lv_obj_add_flag(split, LV_OBJ_FLAG_USER_1);
            lv_obj_add_flag(split, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
            lv_obj_set_size(split, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(split, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(split, 0, 0);
            lv_obj_set_style_pad_all(split, 0, 0);
            lv_obj_set_style_pad_column(split, 14, 0);
            lv_obj_clear_flag(split, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(split, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(split, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

            lv_obj_t *col_main = lv_obj_create(split);
            lv_obj_t *col_float = lv_obj_create(split);
            if (col_main && col_float) {
                p->widget_count += 2;
                lv_obj_t *cols[2] = {col_main, col_float};
                for (int c = 0; c < 2; c++) {
                    lv_obj_add_flag(cols[c], LV_OBJ_FLAG_USER_1);
                    lv_obj_add_flag(cols[c], LV_OBJ_FLAG_OVERFLOW_VISIBLE);
                    lv_obj_set_style_bg_opa(cols[c], LV_OPA_TRANSP, 0);
                    lv_obj_set_style_border_width(cols[c], 0, 0);
                    lv_obj_set_style_pad_all(cols[c], 0, 0);
                    lv_obj_set_style_pad_row(cols[c], 6, 0);
                    lv_obj_clear_flag(cols[c], LV_OBJ_FLAG_SCROLLABLE);
                    lv_obj_set_flex_flow(cols[c], LV_FLEX_FLOW_COLUMN);
                }
                lv_obj_set_height(col_main, LV_SIZE_CONTENT);
                lv_obj_set_flex_grow(col_main, 1);
                lv_obj_set_size(col_float, fw, LV_SIZE_CONTENT);
                if (st->float_mode == CSS_FLOAT_LEFT) {
                    lv_obj_move_to_index(col_float, 0);
                }
                owner->float_split = split;
                owner->float_main_col = col_main;
                owner->float_blocks_left = 5;
                actual_parent = col_float;
            }
        }
    } else if (st->margin[1] == HTML_CSS_LEN_AUTO && st->margin[3] == HTML_CSS_LEN_AUTO &&
               box_w < owner->avail_w - 8 && !parent_is_flex &&
               p->widget_count + 1 < HTML_MAX_WIDGETS) {
        /* Centered block wrapper for margin: 0 auto */
        lv_obj_t *wrap = lv_obj_create(actual_parent);
        if (wrap) {
            p->widget_count++;
            lv_obj_add_flag(wrap, LV_OBJ_FLAG_USER_1);
            lv_obj_add_flag(wrap, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
            lv_obj_set_size(wrap, LV_PCT(100), LV_SIZE_CONTENT);
            lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
            lv_obj_set_style_border_width(wrap, 0, 0);
            int mt = (st->margin[0] > 0 && st->margin[0] != HTML_CSS_UNSET) ? st->margin[0] : 0;
            int mb = (st->margin[2] > 0 && st->margin[2] != HTML_CSS_UNSET) ? st->margin[2] : 0;
            lv_obj_set_style_pad_top(wrap, mt, 0);
            lv_obj_set_style_pad_bottom(wrap, mb, 0);
            lv_obj_set_style_pad_hor(wrap, 0, 0);
            lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(wrap, LV_FLEX_FLOW_COLUMN);
            lv_obj_set_flex_align(wrap, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
            actual_parent = wrap;
        }
    }

    /* Handle real HTML tables & cells */
    if (strcmp(tag, "tr") == 0 && owner->is_table) {
        lv_obj_t *row = lv_obj_create(owner->lv_box);
        if (!row) return NULL;
        p->widget_count++;
        lv_obj_add_flag(row, LV_OBJ_FLAG_USER_1);
        lv_obj_add_flag(row, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        apply_lv_box_style(row, st, false, false);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
        lv_obj_set_style_pad_column(row, 0, 0);
        frame->is_tr = true;
        frame->row_cur_col = 0;
        frame->avail_w = owner->avail_w;
        frame->lv_box = row;
        frame->active_flow = row;
        return row;
    }

    if ((strcmp(tag, "td") == 0 || strcmp(tag, "th") == 0) && owner->is_tr) {
        box_frame_t *tbl = (p->depth >= 3 && p->box[p->depth - 3].is_table) ? &p->box[p->depth - 3]
                         : (p->depth >= 4 && p->box[p->depth - 4].is_table) ? &p->box[p->depth - 4]
                         : NULL;
        int pct = 100;
        if (tbl && tbl->table_cols > 0) {
            char cs_buf[12];
            int cs = 1;
            if (extract_attr(attrs, alen, "colspan", cs_buf, sizeof(cs_buf))) {
                int pv = hc_parse_int(cs_buf);
                if (pv > 1) cs = pv;
            }
            int sum = 0;
            int col_idx = owner->row_cur_col;
            for (int k = 0; k < cs && (col_idx + k) < tbl->table_cols; k++) {
                sum += tbl->table_col_pct[col_idx + k];
            }
            owner->row_cur_col += cs;
            if (sum > 0 && sum <= 100) pct = sum;
        }
        lv_obj_t *cell = lv_obj_create(owner->lv_box);
        if (!cell) return NULL;
        p->widget_count++;
        lv_obj_add_flag(cell, LV_OBJ_FLAG_USER_1);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
        lv_obj_set_size(cell, LV_PCT(pct), LV_SIZE_CONTENT);
        apply_lv_box_style(cell, st, strcmp(tag, "th") == 0, true);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_flex_flow(cell, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(cell, 3, 0);
        frame->avail_w = (owner->avail_w * pct) / 100;
        if (frame->avail_w < 40) frame->avail_w = 40;
        frame->lv_box = cell;
        frame->active_flow = cell;
        return cell;
    }

    lv_obj_t *box = lv_obj_create(actual_parent);
    if (!box) return NULL;
    p->widget_count++;
    if (parent_is_flex && actual_parent == parent)
        html_place_ordered_flex_child(parent, box, st->flex_order);
    if (st->height_px <= 16) lv_obj_add_flag(box, LV_OBJ_FLAG_USER_1);
    lv_obj_add_flag(box, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    if (st->flex_grow && !full_track_flex_item)
        lv_obj_set_flex_grow(box, st->flex_grow);

    bool has_explicit_w = (st->width_px > 0 || st->width_pct > 0 ||
                           row_flex_basis ||
                           st->max_width_px > 0 || is_floating || grid_track_applied);
    if (parent_stretches_width) {
        lv_obj_set_width(box, LV_PCT(100));
    } else if (has_explicit_w) {
        lv_obj_set_width(box, box_w);
    } else if (st->display == CSS_DISP_INLINE_BLOCK || st->display == CSS_DISP_INLINE_FLEX || parent_is_flex) {
        lv_obj_set_width(box, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(box, owner->avail_w, 0);
        frame->width_is_content = true;
    } else {
        lv_obj_set_width(box, LV_PCT(100));
    }
    int min_height = (st->min_height_px > 0) ? st->min_height_px : 0;
    int used_height = (st->height_px > 16) ? st->height_px : 0;
    if (min_height > used_height) used_height = min_height;
    lv_obj_set_height(box, used_height > 0 ? used_height : LV_SIZE_CONTENT);

    apply_lv_box_style(box, st, false, false);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);

    int gap = (st->gap != HTML_CSS_UNSET) ? st->gap : 0;
    if (st->display == CSS_DISP_FLEX || st->display == CSS_DISP_INLINE_FLEX ||
        st->display == CSS_DISP_GRID) {
        frame->is_flex_row = (st->flex_dir != CSS_FLEX_COL);
        lv_flex_flow_t flow = (st->flex_dir == CSS_FLEX_COL) ? LV_FLEX_FLOW_COLUMN
                            : (st->flex_wrap == CSS_WRAP_WRAP || st->display == CSS_DISP_GRID)
                            ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW;
        lv_obj_set_flex_flow(box, flow);
        lv_flex_align_t main_a = LV_FLEX_ALIGN_START;
        if (st->justify_content == CSS_JUSTIFY_CENTER) main_a = LV_FLEX_ALIGN_CENTER;
        else if (st->justify_content == CSS_JUSTIFY_END) main_a = LV_FLEX_ALIGN_END;
        else if (st->justify_content == CSS_JUSTIFY_BETWEEN) main_a = LV_FLEX_ALIGN_SPACE_BETWEEN;
        else if (st->justify_content == CSS_JUSTIFY_AROUND) main_a = LV_FLEX_ALIGN_SPACE_AROUND;

        lv_flex_align_t cross_a = LV_FLEX_ALIGN_START;
        if (st->align_items == CSS_AITEMS_CENTER) cross_a = LV_FLEX_ALIGN_CENTER;
        else if (st->align_items == CSS_AITEMS_END) cross_a = LV_FLEX_ALIGN_END;

        lv_obj_set_flex_align(box, main_a, cross_a, LV_FLEX_ALIGN_START);
        int column_gap = (st->column_gap != HTML_CSS_UNSET) ? st->column_gap : gap;
        int row_gap = (st->row_gap != HTML_CSS_UNSET) ? st->row_gap : gap;
        lv_obj_set_style_pad_column(box, column_gap, 0);
        lv_obj_set_style_pad_row(box, row_gap, 0);
    } else {
        lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(box, (strcmp(tag, "table") == 0) ? 0 : 4, 0);
    }

    if (strcmp(tag, "table") == 0) {
        frame->is_table = true;
        prescan_table_cols(html, html_len, tag_end_pos, &frame->table_cols, frame->table_col_pct);
    }
    if (strcmp(tag, "ul") == 0 || strcmp(tag, "ol") == 0) {
        if (st->pad[3] == HTML_CSS_UNSET && st->display != CSS_DISP_FLEX) {
            lv_obj_set_style_pad_left(box, 18, 0);
        }
        frame->is_ol = (strcmp(tag, "ol") == 0);
        frame->list_item_idx = 0;
    }

    int pad_h = ((st->pad[1] > 0) ? st->pad[1] : 0) + ((st->pad[3] > 0) ? st->pad[3] : 0);
    frame->avail_w = box_w - pad_h;
    /* A zero-basis grow item has no final width during this streaming parse.
     * Give its descendants the containing flex line's available width so
     * nested width:100% boxes are not permanently built at the 60 px floor. */
    if (row_flex_basis && st->flex_basis_zero && st->flex_grow &&
        owner->avail_w - pad_h > frame->avail_w) {
        frame->avail_w = owner->avail_w - pad_h;
    }
    if (frame->avail_w < 60) frame->avail_w = 60;
    frame->lv_box = box;
    frame->active_flow = box;
    if (attrs && alen) {
        char element_id[64];
        char onclick[8];
        extract_attr(attrs, alen, "id", element_id, sizeof(element_id));
        extract_attr(attrs, alen, "onclick", onclick, sizeof(onclick));
        if (element_id[0] && onclick[0]) {
            lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
            add_dom_event_callback(p->pm, box, element_id, "click",
                                   LV_EVENT_CLICKED);
        }
    }
    return box;
}

static void handle_open_tag(html_parser_t *p, const char *tag, const char *attrs, size_t alen,
                            bool self_closing, const char *html, size_t html_len, size_t tag_end) {
    /* Inline void / formatting markers first */
    if (strcmp(tag, "br") == 0) {
        if (p->hidden_depth == 0) {
            box_frame_t *owner = top_box(p);
            if (owner && owner->cur_sg) {
                lv_span_t *sp = lv_spangroup_new_span(owner->cur_sg);
                if (sp) lv_span_set_text(sp, "\n");
                owner->need_space = false;
            }
        }
        return;
    }

    if (strcmp(tag, "hr") == 0) {
        if (p->hidden_depth == 0 && p->widget_count < HTML_MAX_WIDGETS) {
            box_frame_t *owner = NULL;
            lv_obj_t *parent = current_flow_parent(p, &owner);
            if (parent && owner) {
                close_cur_spangroup(owner);
                lv_obj_t *line = lv_obj_create(parent);
                if (line) {
                    p->widget_count++;
                    lv_obj_set_size(line, LV_PCT(100), 1);
                    lv_obj_set_style_bg_color(line, lv_color_hex(0xd1d5db), 0);
                    lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
                    lv_obj_set_style_border_width(line, 0, 0);
                    lv_obj_set_style_pad_all(line, 0, 0);
                }
            }
        }
        return;
    }

    if (strcmp(tag, "img") == 0) {
        if (p->hidden_depth == 0) handle_img_tag(p, attrs, alen);
        return;
    }

    if (strcmp(tag, "input") == 0) {
        if (p->hidden_depth == 0) handle_input_tag(p, attrs, alen);
        return;
    }

    if (is_void_tag(tag)) return;

    if (p->depth >= HTML_MAX_DEPTH) return;

    /* Push node onto DOM stack */
    int d = p->depth++;
    css_node_info_t *nd = &p->dom[d];
    memset(nd, 0, sizeof(*nd));
    hc_copy(tag, nd->tag, sizeof(nd->tag));
    extract_attr(attrs, alen, "id", nd->id, sizeof(nd->id));
    extract_attr(attrs, alen, "class", nd->cls, sizeof(nd->cls));
    nd->hidden_attr = has_bool_attr(attrs, alen, "hidden");
    nd->child_index = p->pending_child_index;
    nd->sibling_count = p->pending_sibling_count;
    nd->type_index = p->pending_type_index;
    nd->type_count = p->pending_type_count;
    nd->is_empty = p->pending_is_empty;
    nd->parent = p->pending_parent;
    nd->prev_sibling = p->pending_prev_sibling;
    capture_css_attributes(p->ss, nd, attrs, alen);
    p->sibling_history_next[d] = 0;
    p->sibling_history_seen[d] = 0;
    p->child_counters[d] = 0;

    char inline_style[512];
    extract_attr(attrs, alen, "style", inline_style, sizeof(inline_style));

    const css_style_t *parent_st = (d > 0) ? &p->style[d - 1] : NULL;
    html_css_compute(p->ss, p->dom, p->depth, inline_style, parent_st, &p->style[d]);
    apply_default_tag_style(tag, &p->style[d], p);

    if (strcmp(tag, "html") == 0 || strcmp(tag, "body") == 0) {
        html_css_collect_matched_vars(p->ss, p->dom, p->depth);
        html_css_compute(p->ss, p->dom, p->depth, inline_style, parent_st, &p->style[d]);
    }

    memset(&p->box[d], 0, sizeof(box_frame_t));

    /* Check visibility / skip subtrees */
    if (p->hidden_depth == 0) {
        const css_style_t *cs = &p->style[d];
        if (is_skip_subtree_tag(tag) || cs->display == CSS_DISP_NONE ||
            cs->visibility == CSS_VIS_HIDDEN || cs->offscreen_hidden || cs->is_sr_only) {
            p->hidden_depth = p->depth;
        }
    }

    if (p->hidden_depth == 0) {
        if (strcmp(tag, "a") == 0) {
            char href[HTML_URL_MAX];
            if (extract_attr(attrs, alen, "href", href, sizeof(href)) && href[0]) {
                resolve_url(p->pm->base_url, href, p->cur_href, sizeof(p->cur_href));
                p->a_depth = p->depth;
            }
        } else if (strcmp(tag, "pre") == 0) {
            p->pre_depth++;
        } else if (strcmp(tag, "form") == 0 && p->pm->form_count < HTML_MAX_FORMS) {
            char act[HTML_URL_MAX];
            extract_attr(attrs, alen, "action", act, sizeof(act));
            int fi = p->pm->form_count++;
            memset(&p->pm->forms[fi], 0, sizeof(html_form_t));
            if (act[0]) resolve_url(p->pm->base_url, act, p->pm->forms[fi].action, HTML_URL_MAX);
            else hc_copy(p->pm->base_url, p->pm->forms[fi].action, HTML_URL_MAX);
            p->active_form_idx = fi;
        } else if (strcmp(tag, "button") == 0) {
            char bt[24];
            extract_attr(attrs, alen, "type", bt, sizeof(bt));
            p->in_button = true;
            p->button_is_submit = (!bt[0] || hc_eq_ci(bt, "submit"));
            p->btn_text[0] = '\0';
        } else if (!is_inline_tag(tag)) {
            /* Block-level tag flushes current paragraph unless continuing list item */
            box_frame_t *owner = top_box(p);
            if (owner) close_cur_spangroup(owner);
        }

        if (strcmp(tag, "li") == 0) {
            box_frame_t *lb = NULL;
            for (int k = p->depth - 2; k >= 0; k--) {
                if (p->box[k].lv_box) { lb = &p->box[k]; break; }
            }
            if (lb && !lb->is_table && p->style[d - 1].display != CSS_DISP_FLEX) {
                lb->list_item_idx++;
                char bullet[16];
                if (lb->is_ol) {
                    int n = lb->list_item_idx;
                    char tmp[12];
                    int ti = 0;
                    do { tmp[ti++] = (char)('0' + (n % 10)); n /= 10; } while (n > 0);
                    int bi = 0;
                    while (ti > 0) bullet[bi++] = tmp[--ti];
                    bullet[bi++] = '.';
                    bullet[bi++] = ' ';
                    bullet[bi] = '\0';
                } else {
                    /* UTF-8 bullet + space */
                    bullet[0] = (char)0xE2;
                    bullet[1] = (char)0x80;
                    bullet[2] = (char)0xA2;
                    bullet[3] = ' ';
                    bullet[4] = '\0';
                }
                memcpy(p->raw_buf, bullet, strlen(bullet));
                p->raw_len = strlen(bullet);
            }
        }

        char onclick[8];
        extract_attr(attrs, alen, "onclick", onclick, sizeof(onclick));
        bool has_dom_id = nd->id[0] && onclick[0] && p->pm->opts.on_dom_event &&
                          !is_inline_tag(tag);
        if (!self_closing && (style_needs_box(&p->style[d], tag, parent_st) ||
                              has_dom_id) &&
            strcmp(tag, "body") != 0 && strcmp(tag, "html") != 0) {
            create_container_box(p, tag, &p->style[d], attrs, alen, html, html_len, tag_end, &p->box[d]);
        }
    }

    if (self_closing) {
        if (p->hidden_depth == p->depth) p->hidden_depth = 0;
        p->depth--;
    }
}

static int html_measure_intrinsic_child_width(lv_obj_t *obj, bool row,
                                               unsigned depth) {
    if (!obj || depth >= 8) return 0;
    int own_width = (int)lv_obj_get_width(obj);
    if (own_width > 0) return own_width;

    uint32_t child_count = lv_obj_get_child_cnt(obj);
    int total = 0;
    int maximum = 0;
    for (uint32_t i = 0; i < child_count && i < 64; ++i) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        int child_width = child ? (int)lv_obj_get_width(child) : 0;
        if (child_width <= 0)
            child_width = html_measure_intrinsic_child_width(child, false,
                                                              depth + 1);
        if (child_width <= 0) continue;
        if (row) {
            if (child_width > INT32_MAX - total) return INT32_MAX;
            total += child_width;
        } else if (child_width > maximum) {
            maximum = child_width;
        }
    }
    return row ? total : maximum;
}

/* Measure auto-sized flex items from rendered content. LVGL's provisional
 * SIZE_CONTENT width can inherit the containing width while the page is still
 * being streamed; that is especially visible when an unsupported SVG icon has
 * width but zero height. Such empty boxes must not make every nav item a full
 * row. */
static int html_measure_intrinsic_flex_width(lv_obj_t *obj, unsigned depth) {
    if (!obj || depth >= 12) return 0;
    if (lv_obj_check_type(obj, &lv_spangroup_class)) {
        uint32_t text_width = lv_spangroup_get_expand_width(obj, 0);
        return text_width > INT32_MAX ? INT32_MAX : (int)text_width;
    }

    uint32_t child_count = lv_obj_get_child_cnt(obj);
    if (child_count == 0) {
        if (lv_obj_get_height(obj) <= 0) return 0;
        return (int)lv_obj_get_width(obj);
    }

    bool row = false;
    if (lv_obj_get_style_layout(obj, LV_PART_MAIN) == LV_LAYOUT_FLEX) {
        lv_flex_flow_t flow = lv_obj_get_style_flex_flow(obj, LV_PART_MAIN);
        row = flow == LV_FLEX_FLOW_ROW || flow == LV_FLEX_FLOW_ROW_WRAP ||
              flow == LV_FLEX_FLOW_ROW_REVERSE ||
              flow == LV_FLEX_FLOW_ROW_WRAP_REVERSE;
    }

    int total = 0;
    int maximum = 0;
    uint32_t measured = 0;
    for (uint32_t i = 0; i < child_count && i < 96; ++i) {
        lv_obj_t *child = lv_obj_get_child(obj, (int32_t)i);
        int width = html_measure_intrinsic_flex_width(child, depth + 1);
        if (width <= 0) continue;
        measured++;
        if (row) {
            if (width > INT32_MAX - total) return INT32_MAX;
            total += width;
        } else if (width > maximum) {
            maximum = width;
        }
    }
    if (!measured) return lv_obj_get_height(obj) > 0
                             ? (int)lv_obj_get_width(obj) : 0;

    int content = row ? total : maximum;
    if (row && measured > 1) {
        int gap = lv_obj_get_style_pad_column(obj, LV_PART_MAIN);
        if (gap > 0 && (uint32_t)gap <= INT32_MAX / (measured - 1))
            content += gap * (int)(measured - 1);
    }
    content += lv_obj_get_style_pad_left(obj, LV_PART_MAIN) +
               lv_obj_get_style_pad_right(obj, LV_PART_MAIN);
    return content;
}

static void handle_close_tag(html_parser_t *p, const char *tag) {
    /* Ignore closing tags for void elements */
    if (is_void_tag(tag)) return;

    int found = -1;
    for (int i = p->depth - 1; i >= 1; i--) {
        if (strcmp(p->dom[i].tag, tag) == 0) {
            found = i;
            break;
        }
    }
    if (found < 0) return;

    while (p->depth - 1 >= found) {
        int d = p->depth - 1;
        const char *closing_tag = p->dom[d].tag;

        if (p->hidden_depth == 0) {
            if (strcmp(closing_tag, "button") == 0 && p->in_button) {
                p->in_button = false;
                box_frame_t *owner = NULL;
                lv_obj_t *parent = current_flow_parent(p, &owner);
                if (parent && owner && p->widget_count < HTML_MAX_WIDGETS && p->btn_text[0]) {
                    close_cur_spangroup(owner);
                    lv_obj_t *btn = lv_btn_create(parent);
                    if (btn) {
                        p->widget_count++;
                        lv_obj_set_size(btn, LV_SIZE_CONTENT, 30);
                        const css_style_t *bs = &p->style[d];
                        lv_color_t bg = (bs->has_bg && !bs->bg_transparent) ? bs->bg : lv_color_hex(0xe5e7eb);
                        lv_color_t fg = bs->has_color ? bs->color : lv_color_hex(0x111827);
                        lv_obj_set_style_bg_color(btn, bg, 0);
                        lv_obj_set_style_radius(btn, 6, 0);
                        lv_obj_set_style_pad_hor(btn, 10, 0);
                        lv_obj_set_style_pad_ver(btn, 4, 0);
                        lv_obj_t *lbl = lv_label_create(btn);
                        lv_obj_set_style_text_font(lbl, html_font_for_px(14), 0);
                        lv_obj_set_style_text_color(lbl, fg, 0);
                        lv_label_set_text(lbl, p->btn_text);
                        lv_obj_center(lbl);

                        add_dom_event_callback(p->pm, btn, p->dom[d].id,
                                               "click", LV_EVENT_CLICKED);

                        if (p->a_depth > 0 && p->cur_href[0]) {
                            href_click_ctx_t *hctx = (href_click_ctx_t *)page_alloc(p->pm, sizeof(href_click_ctx_t));
                            if (hctx) {
                                hctx->pm = p->pm;
                                hctx->href = page_strdup(p->pm, p->cur_href);
                                lv_obj_add_event_cb(btn, obj_href_click_cb, LV_EVENT_CLICKED, hctx);
                            }
                        } else if (p->button_is_submit && p->active_form_idx >= 0) {
                            form_submit_ctx_t *fctx = (form_submit_ctx_t *)page_alloc(p->pm, sizeof(form_submit_ctx_t));
                            if (fctx) {
                                fctx->pm = p->pm;
                                fctx->form_idx = p->active_form_idx;
                                lv_obj_add_event_cb(btn, form_btn_click_cb, LV_EVENT_CLICKED, fctx);
                            }
                        }
                    }
                }
            }

            if (p->a_depth == p->depth) {
                p->a_depth = 0;
                p->cur_href[0] = '\0';
            }
            if (strcmp(closing_tag, "pre") == 0 && p->pre_depth > 0) p->pre_depth--;
            if (strcmp(closing_tag, "form") == 0) p->active_form_idx = -1;

            if (!is_inline_tag(closing_tag)) {
                box_frame_t *owner = top_box(p);
                if (owner) close_cur_spangroup(owner);
            }
        }

        if (p->hidden_depth == p->depth) {
            p->hidden_depth = 0;
        }
        if (p->hidden_depth == 0 && p->box[d].lv_box &&
            p->box[d].width_is_content) {
            /* LVGL may have measured an inline/flex child before its nested
             * descendants were parsed. Recompute intrinsic width at close so
             * descendants do not remain clipped by a zero-width ancestor. */
            lv_obj_update_layout(p->box[d].lv_box);
            if (d > 0 && p->box[d - 1].is_flex_row) {
                int intrinsic = html_measure_intrinsic_flex_width(
                    p->box[d].lv_box, 0);
                if (intrinsic > 0 && intrinsic <= p->box[d].avail_w) {
                    lv_obj_set_width(p->box[d].lv_box, intrinsic);
                }
            } else if (lv_obj_get_width(p->box[d].lv_box) <= 0 &&
                       p->box[d].avail_w > 0) {
                int intrinsic = html_measure_intrinsic_child_width(
                    p->box[d].lv_box, p->box[d].is_flex_row, 0);
                if (intrinsic <= 0 || intrinsic > p->box[d].avail_w)
                    intrinsic = p->box[d].avail_w;
                lv_obj_set_width(p->box[d].lv_box, intrinsic);
            }
        }
        p->depth--;
    }
}

/* ---- Public API ---- */

void html_view_show_message(lv_obj_t *container, const char *title,
                            const char *message, lv_color_t accent) {
    if (!container) return;
    lv_obj_clean(container);
    lv_obj_set_style_bg_color(container, lv_color_hex(0xf8fafc), 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);

    lv_obj_t *wrap = lv_obj_create(container);
    lv_obj_set_size(wrap, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(wrap, 0, 0);
    lv_obj_set_style_pad_all(wrap, 16, 0);
    lv_obj_clear_flag(wrap, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *card = lv_obj_create(wrap);
    lv_obj_set_size(card, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_pad_all(card, 16, 0);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl_t = lv_label_create(card);
    lv_obj_set_width(lbl_t, LV_PCT(100));
    lv_obj_set_style_text_font(lbl_t, html_font_for_px(18), 0);
    lv_obj_set_style_text_color(lbl_t, accent, 0);
    lv_label_set_text(lbl_t, title ? title : "Status");

    lv_obj_t *lbl_m = lv_label_create(card);
    lv_obj_set_width(lbl_m, LV_PCT(100));
    lv_label_set_long_mode(lbl_m, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl_m, html_font_for_px(14), 0);
    lv_obj_set_style_text_color(lbl_m, lv_color_hex(0x1f2937), 0);
    lv_label_set_text(lbl_m, message ? message : "");
}

static uint32_t html_type_count_hash(uint32_t parent_token, const char *tag) {
    uint32_t hash = 2166136261U ^ parent_token;
    for (size_t i = 0; tag[i]; ++i) {
        hash ^= (uint8_t)tag[i];
        hash *= 16777619U;
    }
    return hash;
}

static uint32_t html_count_sibling_map_nodes(const char *html, size_t len) {
    if (!html || !len) return 0;
    uint32_t count = 0;
    size_t i = 0;
    while (i < len && count < HTML_MAX_SIBLING_MAP) {
        if (html[i] != '<') { ++i; continue; }
        if (i + 3U < len && html[i + 1U] == '!' && html[i + 2U] == '-' &&
            html[i + 3U] == '-') {
            const char *end = strstr(html + i + 4U, "-->");
            i = end ? (size_t)(end - html) + 3U : len;
            continue;
        }
        size_t name_start = i + 1U;
        if (name_start >= len || html[name_start] == '!' ||
            html[name_start] == '?') {
            while (i < len && html[i] != '>') ++i;
            if (i < len) ++i;
            continue;
        }
        while (name_start < len && hc_isspace(html[name_start])) ++name_start;
        bool closing = name_start < len && html[name_start] == '/';
        if (closing) ++name_start;
        size_t name_end = name_start;
        while (name_end < len && !hc_isspace(html[name_end]) &&
               html[name_end] != '>' && html[name_end] != '/') ++name_end;
        if (name_end == name_start) { ++i; continue; }

        char tag[24];
        hc_copy_lower(html + name_start, name_end - name_start, tag, sizeof(tag));
        size_t tag_end = name_end;
        char quote = 0;
        while (tag_end < len) {
            char c = html[tag_end];
            if (quote) { if (c == quote) quote = 0; }
            else if (c == '"' || c == '\'') quote = c;
            else if (c == '>') break;
            ++tag_end;
        }
        if (tag_end >= len) break;

        if (!closing && strcmp(tag, "html") != 0) ++count;
        bool raw_body = !closing && (strcmp(tag, "script") == 0 ||
                        strcmp(tag, "style") == 0 || strcmp(tag, "svg") == 0);
        if (raw_body) {
            size_t close = find_close_tag_pos(html, len, tag_end + 1U, tag);
            i = close < len ? close : len;
        } else {
            i = tag_end + 1U;
        }
    }
    return count;
}

static uint32_t html_type_count_capacity(uint32_t element_count) {
    uint32_t wanted = element_count > HTML_MAX_SIBLING_MAP / 2U
        ? HTML_TYPE_COUNT_CAP : element_count * 2U;
    uint32_t capacity = 256U;
    while (capacity < wanted && capacity < HTML_TYPE_COUNT_CAP) capacity <<= 1U;
    return capacity;
}

static uint32_t html_type_count_slot(html_type_count_entry_t *table,
                                     uint32_t table_capacity,
                                    uint32_t parent_token, const char *tag,
                                    bool create) {
    if (!table || !table_capacity || !tag || !tag[0]) return table_capacity;
    uint32_t start = html_type_count_hash(parent_token, tag) & (table_capacity - 1U);
    for (uint32_t probe = 0; probe < table_capacity; ++probe) {
        uint32_t slot = (start + probe) & (table_capacity - 1U);
        html_type_count_entry_t *entry = &table[slot];
        if (!entry->used) {
            if (!create) return HTML_TYPE_COUNT_CAP;
            entry->used = true;
            entry->parent_token = parent_token;
            hc_copy(tag, entry->tag, sizeof(entry->tag));
            return slot;
        }
        if (entry->parent_token == parent_token && strcmp(entry->tag, tag) == 0)
            return slot;
    }
    return table_capacity;
}

static html_sibling_entry_t *html_build_sibling_map(const char *html, size_t len,
                                                     bool need_type_counts,
                                                     uint32_t *out_count) {
    if (out_count) *out_count = 0;
    if (!html || !len || !out_count) return NULL;

    uint32_t capacity = 256;
    html_sibling_entry_t *entries = (html_sibling_entry_t *)kmalloc(
        (size_t)capacity * sizeof(*entries));
    uint16_t *child_counts = (uint16_t *)kmalloc(
        ((size_t)capacity + 1U) * sizeof(*child_counts));
    uint8_t *has_content = (uint8_t *)kmalloc((size_t)capacity + 1U);
    uint32_t type_count_capacity = need_type_counts
        ? html_type_count_capacity(html_count_sibling_map_nodes(html, len)) : 0;
    html_type_count_entry_t *type_counts = need_type_counts
        ? (html_type_count_entry_t *)kmalloc(
            (size_t)type_count_capacity * sizeof(*type_counts)) : NULL;
    if (!entries || !child_counts || !has_content) {
        if (entries) kfree(entries);
        if (child_counts) kfree(child_counts);
        if (has_content) kfree(has_content);
        if (type_counts) kfree(type_counts);
        return NULL;
    }
    memset(child_counts, 0, ((size_t)capacity + 1U) * sizeof(*child_counts));
    memset(has_content, 0, (size_t)capacity + 1U);
    if (type_counts)
        memset(type_counts, 0, (size_t)type_count_capacity * sizeof(*type_counts));

    uint32_t stack_tokens[HTML_MAX_DEPTH + 1] = {0};
    char stack_tags[HTML_MAX_DEPTH + 1][24] = {{0}};
    hc_copy("html", stack_tags[0], sizeof(stack_tags[0]));
    uint32_t depth = 1;
    uint32_t count = 0;
    size_t i = 0;

    while (i < len && count < HTML_MAX_SIBLING_MAP) {
        if (html[i] != '<') {
            has_content[stack_tokens[depth - 1]] = 1;
            ++i;
            continue;
        }
        if (i + 3 < len && html[i + 1] == '!' && html[i + 2] == '-' &&
            html[i + 3] == '-') {
            const char *cend = strstr(html + i + 4, "-->");
            i = cend ? (size_t)(cend - html) + 3U : len;
            continue;
        }
        if (i + 1 >= len || html[i + 1] == '!') {
            while (i < len && html[i] != '>') ++i;
            if (i < len) ++i;
            continue;
        }

        size_t pos = i + 1;
        bool closing = false;
        while (pos < len && hc_isspace(html[pos])) ++pos;
        if (pos < len && html[pos] == '/') { closing = true; ++pos; }
        size_t name_start = pos;
        while (pos < len && !hc_isspace(html[pos]) && html[pos] != '>' &&
               html[pos] != '/') ++pos;
        char tag[24];
        hc_copy_lower(html + name_start, pos - name_start, tag, sizeof(tag));
        if (!tag[0]) { i = pos + (pos < len); continue; }

        size_t gt = pos;
        char quote = 0;
        while (gt < len) {
            char c = html[gt];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') break;
            ++gt;
        }
        if (gt >= len) break;

        if (closing) {
            for (uint32_t d = depth; d > 1; --d) {
                if (strcmp(stack_tags[d - 1], tag) == 0) {
                    depth = d - 1;
                    break;
                }
            }
            i = gt + 1;
            continue;
        }

        bool self_closing = gt > pos && html[gt - 1] == '/';
        if (strcmp(tag, "html") == 0) {
            i = gt + 1;
            continue;
        }
        uint32_t parent_token = stack_tokens[depth - 1];
        if (child_counts[parent_token] != UINT16_MAX) ++child_counts[parent_token];
        has_content[parent_token] = 1;

        uint32_t type_slot = type_counts
            ? html_type_count_slot(type_counts, type_count_capacity,
                                   parent_token, tag, true)
            : type_count_capacity;
        uint16_t type_index = 0;
        if (type_slot < type_count_capacity) {
            html_type_count_entry_t *type_entry = &type_counts[type_slot];
            if (type_entry->count != UINT16_MAX) ++type_entry->count;
            type_index = type_entry->count;
        }

        uint32_t token = count + 1U;
        entries[count++] = (html_sibling_entry_t){
            .tag_offset = i,
            .parent_token = parent_token,
            .sibling_count = 0,
            .type_index = type_index,
            .type_count = 0,
            .type_slot = type_slot
        };
        if (count == capacity && capacity < HTML_MAX_SIBLING_MAP) {
            uint32_t grown_capacity = capacity + capacity / 2U;
            if (grown_capacity > HTML_MAX_SIBLING_MAP)
                grown_capacity = HTML_MAX_SIBLING_MAP;
            html_sibling_entry_t *grown_entries = (html_sibling_entry_t *)krealloc(
                entries, (size_t)grown_capacity * sizeof(*entries));
            uint16_t *grown_counts = (uint16_t *)krealloc(
                child_counts, ((size_t)grown_capacity + 1U) * sizeof(*child_counts));
            uint8_t *grown_content = (uint8_t *)krealloc(
                has_content, (size_t)grown_capacity + 1U);
            if (!grown_entries || !grown_counts || !grown_content) {
                if (grown_entries) entries = grown_entries;
                if (grown_counts) child_counts = grown_counts;
                if (grown_content) has_content = grown_content;
                break;
            }
            entries = grown_entries;
            child_counts = grown_counts;
            has_content = grown_content;
            memset(child_counts + capacity + 1U, 0,
                   (size_t)(grown_capacity - capacity) * sizeof(*child_counts));
            memset(has_content + capacity + 1U, 0,
                   (size_t)(grown_capacity - capacity));
            capacity = grown_capacity;
        }

        bool raw_body = strcmp(tag, "script") == 0 || strcmp(tag, "style") == 0 ||
                        strcmp(tag, "svg") == 0;
        if (!self_closing && !is_void_tag(tag) && !raw_body &&
            depth < HTML_MAX_DEPTH + 1U) {
            stack_tokens[depth] = token;
            hc_copy(tag, stack_tags[depth], sizeof(stack_tags[depth]));
            ++depth;
        }
        if (raw_body) {
            size_t close = find_close_tag_pos(html, len, gt + 1U, tag);
            if (close > gt + 1U && close < len) has_content[token] = 1;
            i = close < len ? close : len;
        } else {
            i = gt + 1U;
        }
    }

    for (uint32_t e = 0; e < count; ++e) {
        entries[e].sibling_count = child_counts[entries[e].parent_token];
        if (type_counts && entries[e].type_slot < type_count_capacity)
            entries[e].type_count = type_counts[entries[e].type_slot].count;
        entries[e].is_empty = !has_content[e + 1U];
    }
    kfree(child_counts);
    kfree(has_content);
    if (type_counts) kfree(type_counts);
    *out_count = count;
    return entries;
}

static const html_sibling_entry_t *html_lookup_sibling_entry(
        const html_sibling_entry_t *entries, uint32_t count, size_t tag_offset) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2U;
        if (entries[mid].tag_offset < tag_offset) lo = mid + 1U;
        else hi = mid;
    }
    return lo < count && entries[lo].tag_offset == tag_offset ? &entries[lo] : NULL;
}

typedef struct {
    int32_t max_right;
    int32_t max_bottom;
    uint32_t visited;
} html_layout_extent_t;

/* Some real pages contain deeply nested flex/grid layouts whose LVGL
 * intrinsic height is smaller than the laid-out descendants. Measure the
 * rendered subtree once so the browser viewport can scroll through all of
 * the content instead of clipping everything after the parent's estimate. */
static void html_measure_layout_extent(lv_obj_t *obj, unsigned depth,
                                       html_layout_extent_t *extent) {
    if (!obj || !extent || depth > 128U ||
        extent->visited >= HTML_MAX_WIDGETS + 16U) return;

    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    if ((int32_t)area.x2 > extent->max_right)
        extent->max_right = (int32_t)area.x2;
    if ((int32_t)area.y2 > extent->max_bottom)
        extent->max_bottom = (int32_t)area.y2;
    extent->visited++;

    uint32_t children = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < children; ++i) {
        html_measure_layout_extent(lv_obj_get_child(obj, (int32_t)i),
                                   depth + 1U, extent);
        if (extent->visited >= HTML_MAX_WIDGETS + 16U) break;
    }
}

static int32_t html_fit_auto_height_boxes(lv_obj_t *obj, unsigned depth,
                                          uint32_t *visited, bool *changed) {
    if (!obj || depth > 128U || *visited >= HTML_MAX_WIDGETS + 16U)
        return 0;

    (*visited)++;
    uint32_t children = lv_obj_get_child_cnt(obj);
    int32_t child_bottom = INT32_MIN;
    for (uint32_t i = 0; i < children; ++i) {
        int32_t bottom = html_fit_auto_height_boxes(
            lv_obj_get_child(obj, (int32_t)i), depth + 1U, visited, changed);
        if (bottom > child_bottom) child_bottom = bottom;
        if (*visited >= HTML_MAX_WIDGETS + 16U) break;
    }

    lv_area_t area;
    lv_obj_get_coords(obj, &area);
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_USER_1) &&
        child_bottom > (int32_t)area.y2) {
        int64_t required = (int64_t)child_bottom - area.y1 + 1;
        if (required > lv_obj_get_height(obj) && required <= INT32_MAX) {
            lv_obj_set_height(obj, (lv_coord_t)required);
            *changed = true;
            /* Geometry is refreshed once between bottom-up passes. */
            return child_bottom;
        }
    }
    int32_t bottom = (int32_t)area.y2;
    return child_bottom > bottom ? child_bottom : bottom;
}

void html_view_render(lv_obj_t *container, const char *content, size_t len,
                      const char *base_url,
                      char *title_out, size_t title_out_sz,
                      const html_view_opts_t *opts) {
    if (!container) return;
    if (title_out && title_out_sz) title_out[0] = '\0';

    if (!content || len == 0) {
        html_view_show_message(container, "Pagina vazia",
                               "O servidor nao retornou conteudo visivel.",
                               lv_color_hex(0x64748b));
        return;
    }

    lv_obj_clean(container);
    lv_obj_scroll_to_y(container, 0, LV_ANIM_OFF);

    int vw = (opts && opts->viewport_w > 120) ? opts->viewport_w
           : (lv_obj_get_width(container) > 120 ? (int)lv_obj_get_width(container) - 20 : 880);

    bool is_html = hc_contains_ci(content, "<html") || hc_contains_ci(content, "<body") ||
                   hc_contains_ci(content, "<div") || hc_contains_ci(content, "<p") ||
                   hc_contains_ci(content, "<h1") || hc_contains_ci(content, "<!doctype");

    if (!is_html) {
        lv_obj_set_style_bg_color(container, lv_color_white(), 0);
        lv_obj_t *pre = lv_label_create(container);
        lv_obj_set_width(pre, LV_PCT(98));
        lv_label_set_long_mode(pre, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(pre, html_font_for_px(14), 0);
        lv_obj_set_style_text_color(pre, lv_color_hex(0x111827), 0);
        size_t cap = (len > 16384U) ? 16384U : len;
        char *buf = (char *)kmalloc(cap + 1);
        if (buf) {
            memcpy(buf, content, cap);
            buf[cap] = '\0';
            lv_label_set_text(pre, buf);
            kfree(buf);
        }
        return;
    }

    html_page_mem_t *pm = (html_page_mem_t *)kmalloc(sizeof(html_page_mem_t));
    html_parser_t *p = (html_parser_t *)kmalloc(sizeof(html_parser_t));
    html_stylesheet_t *ss = html_css_create(vw);
    if (!pm || !p || !ss) {
        if (pm) kfree(pm);
        if (p) kfree(p);
        if (ss) html_css_destroy(ss);
        return;
    }
    html_css_set_viewport_height(ss, (opts && opts->viewport_h > 0)
        ? opts->viewport_h : (int)lv_obj_get_height(container));
    /* LIWUS desktop ships with its Breeze Dark visual theme. */
    html_css_set_prefer_dark_scheme(ss, true);
    memset(pm, 0, sizeof(*pm));
    memset(p, 0, sizeof(*p));
    if (opts) pm->opts = *opts;
    if (base_url) hc_copy(base_url, pm->base_url, sizeof(pm->base_url));

    prescan_head_and_styles(content, len, pm->base_url, ss, opts, title_out, title_out_sz);

    /* Root doc container carrying page lifetime cleanup */
    lv_obj_t *doc_root = lv_obj_create(container);
    lv_obj_add_event_cb(doc_root, page_mem_delete_cb, LV_EVENT_DELETE, pm);
    lv_obj_add_flag(doc_root, LV_OBJ_FLAG_USER_1);
    lv_obj_add_flag(doc_root, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    lv_obj_set_size(doc_root, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_clear_flag(doc_root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(doc_root, 0, 0);
    lv_obj_set_style_pad_all(doc_root, 12, 0);
    lv_obj_set_style_pad_row(doc_root, 8, 0);
    lv_obj_set_flex_flow(doc_root, LV_FLEX_FLOW_COLUMN);

    /* Browser default light theme, overridden by html/body CSS */
    p->pm = pm;
    p->ss = ss;
    p->viewport = container;
    p->viewport_w = vw;
    p->active_form_idx = -1;
    p->def_link_col = lv_color_hex(0x0b57d0);
    p->sibling_map = html_build_sibling_map(content, len, ss->needs_type_counts,
                                            &p->sibling_map_count);

    p->depth = 1;
    strcpy(p->dom[0].tag, "html");
    p->dom[0].type_index = 1;
    p->dom[0].type_count = 1;
    html_css_style_init(&p->style[0], NULL);
    p->style[0].has_color = true;
    p->style[0].color = lv_color_hex(0x111827);
    p->style[0].font_px = 14;
    html_css_collect_matched_vars(ss, p->dom, 1);
    html_css_compute(ss, p->dom, 1, NULL, &p->style[0], &p->style[0]);

    lv_color_t canvas_bg = (p->style[0].has_bg && !p->style[0].bg_transparent)
                         ? p->style[0].bg : lv_color_white();
    lv_obj_set_style_bg_color(container, canvas_bg, 0);
    lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_opa(doc_root, LV_OPA_TRANSP, 0);

    p->box[0].lv_box = doc_root;
    p->box[0].active_flow = doc_root;
    p->box[0].avail_w = (vw > 40) ? (vw - 24) : 840;

    if (opts && opts->on_status) opts->on_status("Renderizando pagina...", opts->user_data);

    /* Fast streaming HTML tokenizer */
    size_t i = 0;
    while (i < len) {
        if (content[i] != '<') {
            if (p->hidden_depth == 0) {
                size_t tstart = i;
                while (i < len && content[i] != '<') i++;
                size_t tlen = i - tstart;
                size_t off = 0;
                while (off < tlen) {
                    size_t take = tlen - off;
                    if (p->raw_len + take >= HTML_TEXT_BUF_SZ) {
                        take = (HTML_TEXT_BUF_SZ - 1) - p->raw_len;
                    }
                    if (take > 0) {
                        memcpy(p->raw_buf + p->raw_len, content + tstart + off, take);
                        p->raw_len += take;
                        off += take;
                    }
                    if (p->raw_len >= HTML_TEXT_BUF_SZ - 1) {
                        flush_text_buffer(p);
                    }
                }
            } else {
                while (i < len && content[i] != '<') i++;
            }
            continue;
        }

        /* Comment or <!DOCTYPE> */
        if (i + 3 < len && content[i + 1] == '!' && content[i + 2] == '-' && content[i + 3] == '-') {
            const char *cend = strstr(content + i + 4, "-->");
            i = cend ? (size_t)(cend - content) + 3 : len;
            continue;
        }
        if (i + 1 < len && content[i + 1] == '!') {
            while (i < len && content[i] != '>') i++;
            if (i < len) i++;
            continue;
        }

        flush_text_buffer(p);

        size_t pos = i + 1;
        bool closing = false;
        while (pos < len && hc_isspace(content[pos])) pos++;
        if (pos < len && content[pos] == '/') {
            closing = true;
            pos++;
        }
        size_t tstart = pos;
        while (pos < len && !hc_isspace(content[pos]) && content[pos] != '>' && content[pos] != '/') pos++;
        char tname[24];
        hc_copy_lower(content + tstart, pos - tstart, tname, sizeof(tname));

        size_t gt = pos;
        char quote = 0;
        while (gt < len) {
            char c = content[gt];
            if (quote) {
                if (c == quote) quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '>') {
                break;
            }
            gt++;
        }
        if (gt >= len) break;

        bool self_closing = (!closing && gt > pos && content[gt - 1] == '/');
        if (!tname[0]) {
            i = gt + 1;
            continue;
        }

        /* CSS child positions count all element siblings, including nodes the
         * renderer does not draw (scripts, styles, SVG and void elements). */
        if (!closing && strcmp(tname, "html") != 0 && p->depth > 0 &&
            p->depth <= HTML_MAX_DEPTH) {
            uint16_t *counter = &p->child_counters[p->depth - 1];
            if (*counter != UINT16_MAX) (*counter)++;
            p->pending_child_index = *counter;
        } else if (!closing && strcmp(tname, "html") == 0) {
            p->pending_child_index = 1;
            p->pending_sibling_count = 1;
            p->pending_type_index = 1;
            p->pending_type_count = 1;
        }
        const html_sibling_entry_t *sibling_entry = closing ? NULL
            : html_lookup_sibling_entry(p->sibling_map, p->sibling_map_count, i);
        p->pending_sibling_count = sibling_entry ? sibling_entry->sibling_count : 0;
        p->pending_type_index = sibling_entry ? sibling_entry->type_index : 0;
        p->pending_type_count = sibling_entry ? sibling_entry->type_count : 0;
        if (!closing && strcmp(tname, "html") == 0) {
            p->pending_sibling_count = 1;
            p->pending_type_index = 1;
            p->pending_type_count = 1;
        }

        const char *attrs = content + pos;
        size_t alen = gt - pos;
        if (!closing && strcmp(tname, "html") != 0) {
            capture_css_sibling_snapshot(p, tname, attrs, alen);
        } else {
            p->pending_parent = NULL;
            p->pending_prev_sibling = NULL;
        }

        /* Jump right past script/style raw bodies quickly */
        if (!closing && (strcmp(tname, "script") == 0 || strcmp(tname, "style") == 0 ||
                         strcmp(tname, "svg") == 0)) {
            i = find_close_tag_pos(content, len, gt + 1, tname);
            continue;
        }

        if (closing) {
            handle_close_tag(p, tname);
        } else {
            if (strcmp(tname, "html") == 0 && p->depth == 1) {
                char h_id[40], h_cls[256];
                extract_attr(attrs, alen, "id", h_id, sizeof(h_id));
                extract_attr(attrs, alen, "class", h_cls, sizeof(h_cls));
                hc_copy(h_id, p->dom[0].id, sizeof(p->dom[0].id));
                hc_copy(h_cls, p->dom[0].cls, sizeof(p->dom[0].cls));
                p->dom[0].sibling_count = 1;
                capture_css_attributes(p->ss, &p->dom[0], attrs, alen);
                html_css_collect_matched_vars(ss, p->dom, 1);
                css_style_t hst;
                html_css_compute(ss, p->dom, 1, NULL, &p->style[0], &hst);
                p->style[0] = hst;
                if (hst.has_bg && !hst.bg_transparent) {
                    lv_obj_set_style_bg_color(container, hst.bg, 0);
                }
            } else if (strcmp(tname, "body") == 0 && p->depth == 1) {
                /* Push <body> at depth 2 while honoring constrained CSS boxes (e.g. centered max-width) */
                char body_id[40], body_cls[256], body_style[512];
                extract_attr(attrs, alen, "id", body_id, sizeof(body_id));
                extract_attr(attrs, alen, "class", body_cls, sizeof(body_cls));
                extract_attr(attrs, alen, "style", body_style, sizeof(body_style));
                p->depth = 2;
                memset(&p->dom[1], 0, sizeof(p->dom[1]));
                strcpy(p->dom[1].tag, "body");
                p->dom[1].child_index = p->pending_child_index;
                p->dom[1].sibling_count = p->pending_sibling_count;
                p->dom[1].type_index = p->pending_type_index;
                p->dom[1].type_count = p->pending_type_count;
                p->dom[1].parent = p->pending_parent;
                p->dom[1].prev_sibling = p->pending_prev_sibling;
                p->child_counters[1] = 0;
                p->sibling_history_next[1] = 0;
                p->sibling_history_seen[1] = 0;
                capture_css_attributes(p->ss, &p->dom[1], attrs, alen);
                hc_copy(body_id, p->dom[1].id, sizeof(p->dom[1].id));
                hc_copy(body_cls, p->dom[1].cls, sizeof(p->dom[1].cls));
                html_css_collect_matched_vars(ss, p->dom, 2);
                html_css_compute(ss, p->dom, 2, body_style, &p->style[0], &p->style[1]);
                if ((!p->style[0].has_bg || p->style[0].bg_transparent) &&
                    p->style[1].has_bg && !p->style[1].bg_transparent) {
                    lv_obj_set_style_bg_color(container, p->style[1].bg, 0);
                }
                memset(&p->box[1], 0, sizeof(box_frame_t));
                if (style_needs_box(&p->style[1], "body", &p->style[0])) {
                    bool body_custom_pad = false;
                    for (int pi = 0; pi < 4; pi++) {
                        if (p->style[1].pad[pi] != HTML_CSS_UNSET) body_custom_pad = true;
                    }
                    if (body_custom_pad || (p->style[1].margin[1] == HTML_CSS_LEN_AUTO &&
                                            p->style[1].margin[3] == HTML_CSS_LEN_AUTO)) {
                        lv_obj_set_style_pad_all(doc_root, 0, 0);
                        p->box[0].avail_w = (vw > 16) ? vw : 840;
                    }
                    create_container_box(p, "body", &p->style[1], attrs, alen,
                                         content, len, gt + 1, &p->box[1]);
                }
                if (!p->box[1].lv_box) {
                    p->box[1] = p->box[0];
                }
            } else {
                handle_open_tag(p, tname, attrs, alen, self_closing, content, len, gt + 1);
            }
        }
        i = gt + 1;
    }

    flush_text_buffer(p);

    /* Flex/grid descendants can extend beyond LVGL's intrinsic estimate.
     * An out-of-flow extent marker preserves both scroll axes without
     * resizing the flex root and causing its descendants to reflow. */
    lv_obj_scroll_to_y(container, 0, LV_ANIM_OFF);
    lv_obj_update_layout(container);
    if (doc_root) {
        /* Grow content-driven wrappers from the leaves outward. A few passes
         * allow each resized child to update its parent's intrinsic bounds. */
        for (unsigned pass = 0; pass < 3; ++pass) {
            uint32_t visited = 0;
            bool changed = false;
            (void)html_fit_auto_height_boxes(doc_root, 0, &visited, &changed);
            if (!changed) break;
            lv_obj_update_layout(container);
        }

        html_layout_extent_t extent = {0, 0, 0};
        lv_area_t root_area;
        lv_area_t viewport_area;
        lv_obj_get_coords(doc_root, &root_area);
        extent.max_right = (int32_t)root_area.x2;
        extent.max_bottom = (int32_t)root_area.y2;
        html_measure_layout_extent(doc_root, 0, &extent);
        lv_obj_get_coords(container, &viewport_area);
        int64_t scroll_x = (int64_t)extent.max_right - viewport_area.x1 + 1;
        int64_t scroll_y = (int64_t)extent.max_bottom - viewport_area.y1 + 1;
        bool needs_scroll_extent =
            scroll_x > lv_obj_get_width(container) ||
            scroll_y > lv_obj_get_height(container);
        if (needs_scroll_extent && scroll_x <= INT32_MAX &&
            scroll_y <= INT32_MAX && scroll_x >= 0 && scroll_y >= 0) {
            lv_obj_t *extent_marker = lv_obj_create(container);
            if (extent_marker) {
                lv_obj_clear_flag(extent_marker,
                                  LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
                lv_obj_add_flag(extent_marker, LV_OBJ_FLAG_IGNORE_LAYOUT);
                lv_obj_set_pos(extent_marker, (lv_coord_t)scroll_x,
                               (lv_coord_t)scroll_y);
                lv_obj_set_size(extent_marker, 1, 1);
                lv_obj_set_style_bg_opa(extent_marker, LV_OPA_TRANSP, 0);
                lv_obj_set_style_border_width(extent_marker, 0, 0);
                lv_obj_set_style_pad_all(extent_marker, 0, 0);
            }
            lv_obj_update_layout(container);
        }
    }

    html_css_destroy(ss);
    if (p->sibling_map) kfree(p->sibling_map);
    kfree(p);
}

#ifdef HTML_VIEW_HOST_TEST
bool html_view_test_sibling_metadata(const char *html, size_t len,
                                     bool need_type_counts, size_t tag_offset,
                                     uint16_t *sibling_count,
                                     uint16_t *type_index, uint16_t *type_count,
                                     bool *is_empty) {
    uint32_t entry_count = 0;
    html_sibling_entry_t *entries = html_build_sibling_map(html, len, need_type_counts,
                                                           &entry_count);
    if (!entries) return false;
    const html_sibling_entry_t *entry = html_lookup_sibling_entry(
        entries, entry_count, tag_offset);
    bool found = entry != NULL;
    if (entry) {
        if (sibling_count) *sibling_count = entry->sibling_count;
        if (type_index) *type_index = entry->type_index;
        if (type_count) *type_count = entry->type_count;
        if (is_empty) *is_empty = entry->is_empty;
    }
    kfree(entries);
    return found;
}
#endif
