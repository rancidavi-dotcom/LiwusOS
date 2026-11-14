#include "gui/html_css.h"

#define CSS_RULE_PARTS(ss, rule) ((ss)->selector_parts + (rule)->parts_off)
#include "kheap.h"
#include "string.h"
#include <limits.h>
#ifndef HTML_CSS_HOST_TEST
#include "serial.h"
#endif

/* Bounded and deduplicated: large stylesheets are applied to many nodes. */
static void css_log_drop(const char *reason, const char *prop, const char *val) {
#ifndef HTML_CSS_HOST_TEST
    static uint64_t seen[96];
    static unsigned count;
    uint64_t h = 1469598103934665603ULL;
    const char *parts[] = {reason, prop, val};
    for (unsigned i = 0; i < 3; ++i) {
        for (const char *s = parts[i]; *s; ++s) h = (h ^ (uint8_t)*s) * 1099511628211ULL;
        h = (h ^ 0xffU) * 1099511628211ULL;
    }
    for (unsigned i = 0; i < count; ++i) if (seen[i] == h) return;
    if (count == 96) return;
    seen[count++] = h;
    serial_printf("[CSS DROP] %s property=%s value=%s\n", reason, prop, val);
    if (count == 96) serial_printf("[CSS DROP] limit reached\n");
#else
    (void)reason; (void)prop; (void)val;
#endif
}

static void css_trace_grid(const char *reason, const char *prop, const char *val) {
#ifndef HTML_CSS_HOST_TEST
    static uint64_t seen[48];
    static unsigned count;
    uint64_t h = 1469598103934665603ULL;
    const char *parts[] = {reason, prop, val};
    for (unsigned i = 0; i < 3; ++i) {
        for (const char *s = parts[i]; *s; ++s) h = (h ^ (uint8_t)*s) * 1099511628211ULL;
        h = (h ^ 0xffU) * 1099511628211ULL;
    }
    for (unsigned i = 0; i < count; ++i) if (seen[i] == h) return;
    if (count == 48) return;
    seen[count++] = h;
    serial_printf("[CSS GRID] %s property=%s value=%s\n", reason, prop, val);
#else
    (void)reason; (void)prop; (void)val;
#endif
}

/* ---- Freestanding character & string helpers ---- */

static bool c_isspace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f';
}

static bool c_isdigit(char c) {
    return c >= '0' && c <= '9';
}

static bool c_isalnum(char c) {
    return c_isdigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static char c_tolower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

static uint64_t css_hash_slice(const char *s, size_t len, bool insensitive) {
    uint64_t hash = 1469598103934665603ULL;
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = (uint8_t)(insensitive ? c_tolower(s[i]) : s[i]);
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    return hash;
}

static css_token_hash_t css_hash_token(const char *s, size_t len) {
    css_token_hash_t hash = {
        .high = 1469598103934665603ULL,
        .low = 7809847782465536322ULL
    };
    for (size_t i = 0; i < len; ++i) {
        uint8_t c = (uint8_t)s[i];
        hash.high = (hash.high ^ c) * 1099511628211ULL;
        hash.low = (hash.low ^ (uint8_t)(c + 0x9dU)) * 14029467366897019727ULL;
        hash.low ^= hash.low >> 31;
    }
    return hash;
}

static bool css_token_hash_equal(css_token_hash_t a, css_token_hash_t b) {
    return a.high == b.high && a.low == b.low;
}

static uint64_t css_index_token_hash(css_token_hash_t hash) {
    return hash.high ^ ((hash.low << 23) | (hash.low >> 41));
}

static uint32_t css_index_bucket(uint64_t hash) {
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    return (uint32_t)hash & (HTML_CSS_INDEX_BUCKETS - 1U);
}

static void css_register_attribute_name(html_stylesheet_t *ss,
                                        uint64_t name_hash) {
    if (!ss) return;
    for (uint16_t i = 0; i < ss->referenced_attr_count; ++i) {
        if (ss->referenced_attr_hashes[i] == name_hash) return;
    }
    uint32_t bit_index = (uint32_t)name_hash & 255U;
    ss->referenced_attr_bloom[bit_index >> 6] |= 1ULL << (bit_index & 63U);
    if (ss->referenced_attr_count < HTML_CSS_MAX_ATTR_NAMES)
        ss->referenced_attr_hashes[ss->referenced_attr_count++] = name_hash;
}

void html_css_node_add_attribute(css_node_info_t *node,
                                const char *name, size_t name_len,
                                const char *value, size_t value_len,
                                bool has_value) {
    if (!node || !name || name_len == 0) return;
    uint64_t name_hash = css_hash_slice(name, name_len, true);
    for (uint8_t i = 0; i < node->attr_count; ++i) {
        css_node_attr_t *attr = &node->attrs[i];
        if (attr->name_hash == name_hash) {
            /* HTML keeps the first duplicate attribute on an element. */
            return;
        }
    }
    if (node->attr_count >= HTML_CSS_MAX_NODE_ATTRS) return;
    css_node_attr_t *attr = &node->attrs[node->attr_count++];
    attr->name_hash = name_hash;
    attr->has_value = has_value;
    const char *actual = value ? value : "";
    attr->value_hash = css_hash_slice(actual, value_len, false);
    attr->value_hash_ci = css_hash_slice(actual, value_len, true);
    attr->value_len = value_len > UINT16_MAX ? UINT16_MAX : (uint16_t)value_len;
    attr->value_stored_len = value_len > HTML_CSS_ATTR_VALUE_SNAPSHOT
        ? HTML_CSS_ATTR_VALUE_SNAPSHOT : (uint8_t)value_len;
    attr->value_truncated = value_len > HTML_CSS_ATTR_VALUE_SNAPSHOT;
    if (attr->value_stored_len) {
        memcpy(attr->value_prefix, actual, attr->value_stored_len);
        size_t suffix_len = attr->value_stored_len;
        size_t suffix_off = value_len > suffix_len ? value_len - suffix_len : 0;
        memcpy(attr->value_suffix, actual + suffix_off, suffix_len);
    }
}

bool html_css_attribute_is_referenced(const html_stylesheet_t *ss,
                                      const char *name, size_t name_len) {
    if (!ss || !name || name_len == 0) return false;
    uint64_t hash = css_hash_slice(name, name_len, true);
    uint32_t bit_index = (uint32_t)hash & 255U;
    if (!(ss->referenced_attr_bloom[bit_index >> 6] &
          (1ULL << (bit_index & 63U)))) return false;
    for (uint16_t i = 0; i < ss->referenced_attr_count; ++i) {
        if (ss->referenced_attr_hashes[i] == hash) return true;
    }
    return false;
}

static int hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = c_tolower(c);
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    return -1;
}

static bool ci_eq(const char *a, const char *b) {
    while (*a && *b) {
        if (c_tolower(*a) != c_tolower(*b)) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static bool ci_prefix(const char *s, const char *p) {
    while (*p) {
        if (c_tolower(*s) != c_tolower(*p)) return false;
        s++;
        p++;
    }
    return true;
}

static bool ci_contains(const char *hay, const char *needle) {
    size_t nlen = strlen(needle);
    if (nlen == 0) return true;
    for (size_t i = 0; hay[i]; i++) {
        size_t j = 0;
        while (j < nlen && hay[i + j] &&
               c_tolower(hay[i + j]) == c_tolower(needle[j])) {
            j++;
        }
        if (j == nlen) return true;
    }
    return false;
}

static void trim_slice(const char *s, size_t len, const char **out_s, size_t *out_len) {
    while (len > 0 && c_isspace(*s)) {
        s++;
        len--;
    }
    while (len > 0 && c_isspace(s[len - 1])) {
        len--;
    }
    *out_s = s;
    *out_len = len;
}

static void copy_lower_slice(const char *s, size_t len, char *dst, size_t cap) {
    size_t i = 0;
    while (i < len && i + 1 < cap) {
        dst[i] = c_tolower(s[i]);
        i++;
    }
    dst[i] = '\0';
}

static void copy_slice(const char *s, size_t len, char *dst, size_t cap) {
    size_t i = 0;
    while (i < len && i + 1 < cap) {
        dst[i] = s[i];
        i++;
    }
    dst[i] = '\0';
}

/* ---- Arena & custom variable table ---- */

static const css_style_t *g_css_var_scope;

static bool arena_append(html_stylesheet_t *ss, const char *s, size_t len,
                         uint32_t *out_off, uint32_t *out_len) {
    if (!ss || len == 0) {
        *out_off = 0;
        *out_len = 0;
        return false;
    }
    size_t need = ss->arena_len + len + 1;
    if (need > 2U * 1024U * 1024U) return false;
    if (need > ss->arena_cap) {
        size_t new_cap = ss->arena_cap ? ss->arena_cap * 2 : 32768;
        while (new_cap < need) new_cap *= 2;
        if (new_cap > 2U * 1024U * 1024U) new_cap = 2U * 1024U * 1024U;
        if (need > new_cap) return false;
        char *nb = (char *)krealloc(ss->arena, new_cap);
        if (!nb) return false;
        ss->arena = nb;
        ss->arena_cap = new_cap;
    }
    *out_off = (uint32_t)ss->arena_len;
    *out_len = (uint32_t)len;
    memcpy(ss->arena + ss->arena_len, s, len);
    ss->arena[ss->arena_len + len] = '\0';
    ss->arena_len += len + 1;
    return true;
}

static bool selector_arena_append(html_stylesheet_t *ss, const char *s,
                                  size_t len, uint32_t *out_off) {
    if (!ss || !s || !out_off || len == 0 || len > UINT16_MAX) return false;
    size_t need = ss->selector_arena_len + len + 1;
    if (need > 1024U * 1024U) return false;
    if (need > ss->selector_arena_cap) {
        size_t cap = ss->selector_arena_cap ? ss->selector_arena_cap * 2 : 4096;
        while (cap < need) cap *= 2;
        if (cap > 1024U * 1024U) cap = 1024U * 1024U;
        if (need > cap) return false;
        char *grown = (char *)krealloc(ss->selector_arena, cap);
        if (!grown) return false;
        ss->selector_arena = grown;
        ss->selector_arena_cap = cap;
    }
    *out_off = (uint32_t)ss->selector_arena_len;
    memcpy(ss->selector_arena + ss->selector_arena_len, s, len);
    ss->selector_arena[ss->selector_arena_len + len] = '\0';
    ss->selector_arena_len += len + 1;
    return true;
}

static void set_var(html_stylesheet_t *ss, const char *name, const char *val) {
    if (!ss || !name[0]) return;
    for (int i = 0; i < ss->var_count; i++) {
        if (strcmp(ss->vars[i].name, name) == 0) {
            copy_slice(val, strlen(val), ss->vars[i].value, sizeof(ss->vars[i].value));
            return;
        }
    }
    if (ss->var_count >= HTML_CSS_MAX_VARS) return;
    copy_slice(name, strlen(name), ss->vars[ss->var_count].name, sizeof(ss->vars[0].name));
    copy_slice(val, strlen(val), ss->vars[ss->var_count].value, sizeof(ss->vars[0].value));
    ss->var_count++;
}

static const char *lookup_var(const html_stylesheet_t *ss, const char *name) {
    if (!name) return NULL;
    if (g_css_var_scope) {
        for (int i = (int)g_css_var_scope->scoped_var_count - 1; i >= 0; --i) {
            if (strcmp(g_css_var_scope->scoped_vars[i].name, name) == 0)
                return g_css_var_scope->scoped_vars[i].value;
        }
    }
    if (!ss) return NULL;
    for (int i = ss->var_count - 1; i >= 0; i--) {
        if (strcmp(ss->vars[i].name, name) == 0) return ss->vars[i].value;
    }
    return NULL;
}

static void set_scoped_var(css_style_t *st, const char *name, const char *value) {
    if (!st || !name || !name[0] || !value || !value[0]) return;
    for (uint16_t i = 0; i < st->scoped_var_count; ++i) {
        if (strcmp(st->scoped_vars[i].name, name) == 0) {
            copy_slice(value, strlen(value), st->scoped_vars[i].value,
                       sizeof(st->scoped_vars[i].value));
            return;
        }
    }
    if (st->scoped_var_count >= HTML_CSS_MAX_SCOPED_VARS) return;
    css_var_t *var = &st->scoped_vars[st->scoped_var_count++];
    copy_slice(name, strlen(name), var->name, sizeof(var->name));
    copy_slice(value, strlen(value), var->value, sizeof(var->value));
}

/* Resolves var(--x, fallback) and light-dark(a, b) into buf. */
static void resolve_css_value(const html_stylesheet_t *ss, const char *expr,
                              char *buf, size_t cap, int depth) {
    if (!expr || !cap) return;
    const char *s = expr;
    size_t len = strlen(s);
    trim_slice(s, len, &s, &len);

    /* Expand each var() separately, including those inside calc() and box
     * shorthands. A single whole-value var() is handled below. */
    if (depth < 6) {
        size_t start = len, end = len;
        for (size_t i = 0; i + 4 <= len; ++i) {
            if (!ci_prefix(s + i, "var(")) continue;
            int nesting = 1;
            for (size_t j = i + 4; j < len; ++j) {
                if (s[j] == '(') nesting++;
                else if (s[j] == ')' && --nesting == 0) { end = j; break; }
            }
            start = i;
            break;
        }
        if (start < len && (start != 0 || end != len - 1)) {
            if (end >= len || end - start + 2 > 256) { buf[0] = '\0'; return; }
            char atom[256], expanded[256], rest[256];
            copy_slice(s + start, end - start + 1, atom, sizeof(atom));
            resolve_css_value(ss, atom, expanded, sizeof(expanded), depth + 1);
            if (!expanded[0]) { buf[0] = '\0'; return; }
            copy_slice(s + end + 1, len - end - 1, rest, sizeof(rest));
            char tail[256];
            resolve_css_value(ss, rest, tail, sizeof(tail), depth);
            const char *remaining = rest;
            while (c_isspace(*remaining)) ++remaining;
            if (*remaining && !tail[0]) { buf[0] = '\0'; return; }
            size_t n = 0;
            for (size_t i = 0; i < start && n + 1 < cap; ++i) buf[n++] = s[i];
            for (size_t i = 0; expanded[i] && n + 1 < cap; ++i) buf[n++] = expanded[i];
            /* Preserve whitespace that trim_slice would remove from the tail. */
            if (rest[0] && c_isspace(rest[0]) && n + 1 < cap) buf[n++] = ' ';
            for (size_t i = 0; tail[i] && n + 1 < cap; ++i) buf[n++] = tail[i];
            buf[n] = '\0';
            return;
        }
    }
    if (depth < 6 && len >= 5 && ci_prefix(s, "var(") && s[len - 1] == ')') {
        const char *inner = s + 4;
        size_t ilen = len - 5;
        int paren = 0;
        const char *comma = NULL;
        for (size_t i = 0; i < ilen; i++) {
            if (inner[i] == '(') paren++;
            else if (inner[i] == ')') paren--;
            else if (inner[i] == ',' && paren == 0) {
                comma = inner + i;
                break;
            }
        }
        const char *vname_s = inner;
        size_t vname_len = comma ? (size_t)(comma - inner) : ilen;
        trim_slice(vname_s, vname_len, &vname_s, &vname_len);
        char vname[64];
        copy_slice(vname_s, vname_len, vname, sizeof(vname));
        const char *found = lookup_var(ss, vname);
        if (found && found[0]) {
            resolve_css_value(ss, found, buf, cap, depth + 1);
            return;
        }
        if (comma) {
            const char *fb_s = comma + 1;
            size_t fb_len = (size_t)((inner + ilen) - fb_s);
            trim_slice(fb_s, fb_len, &fb_s, &fb_len);
            char tmp[192];
            copy_slice(fb_s, fb_len, tmp, sizeof(tmp));
            resolve_css_value(ss, tmp, buf, cap, depth + 1);
            return;
        }
        buf[0] = '\0';
        return;
    }

    if (depth < 6 && len >= 11 && ci_prefix(s, "light-dark(") && s[len - 1] == ')') {
        const char *inner = s + 11;
        size_t ilen = len - 12;
        int paren = 0;
        size_t split = ilen;
        for (size_t i = 0; i < ilen; i++) {
            if (inner[i] == '(') paren++;
            else if (inner[i] == ')') paren--;
            else if (inner[i] == ',' && paren == 0) {
                split = i;
                break;
            }
        }
        char first[192];
        copy_slice(inner, split, first, sizeof(first));
        resolve_css_value(ss, first, buf, cap, depth + 1);
        return;
    }

    copy_slice(s, len, buf, cap);
}

/* ---- Named colors & color parser ---- */

typedef struct {
    const char *name;
    uint32_t hex;
} css_named_color_t;

static const css_named_color_t g_named_colors[] = {
    {"aliceblue", 0xf0f8ff}, {"antiquewhite", 0xfaebd7}, {"aqua", 0x00ffff},
    {"aquamarine", 0x7fffd4}, {"azure", 0xf0ffff}, {"beige", 0xf5f5dc},
    {"bisque", 0xffe4c4}, {"black", 0x000000}, {"blanchedalmond", 0xffebcd},
    {"blue", 0x0000ff}, {"blueviolet", 0x8a2be2}, {"brown", 0xa52a2a},
    {"burlywood", 0xdeb887}, {"cadetblue", 0x5f9ea0}, {"chartreuse", 0x7fff00},
    {"chocolate", 0xd2691e}, {"coral", 0xff7f50}, {"cornflowerblue", 0x6495ed},
    {"cornsilk", 0xfff8dc}, {"crimson", 0xdc143c}, {"cyan", 0x00ffff},
    {"darkblue", 0x00008b}, {"darkcyan", 0x008b8b}, {"darkgoldenrod", 0xb8860b},
    {"darkgray", 0xa9a9a9}, {"darkgreen", 0x006400}, {"darkgrey", 0xa9a9a9},
    {"darkkhaki", 0xbdb76b}, {"darkmagenta", 0x8b008b}, {"darkolivegreen", 0x556b2f},
    {"darkorange", 0xff8c00}, {"darkorchid", 0x9932cc}, {"darkred", 0x8b0000},
    {"darksalmon", 0xe9967a}, {"darkseagreen", 0x8fbc8f}, {"darkslateblue", 0x483d8b},
    {"darkslategray", 0x2f4f4f}, {"darkslategrey", 0x2f4f4f}, {"darkturquoise", 0x00ced1},
    {"darkviolet", 0x9400d3}, {"deeppink", 0xff1493}, {"deepskyblue", 0x00bfff},
    {"dimgray", 0x696969}, {"dimgrey", 0x696969}, {"dodgerblue", 0x1e90ff},
    {"firebrick", 0xb22222}, {"floralwhite", 0xfffaf0}, {"forestgreen", 0x228b22},
    {"fuchsia", 0xff00ff}, {"gainsboro", 0xdcdcdc}, {"ghostwhite", 0xf8f8ff},
    {"gold", 0xffd700}, {"goldenrod", 0xdaa520}, {"gray", 0x808080},
    {"green", 0x008000}, {"greenyellow", 0xadff2f}, {"grey", 0x808080},
    {"honeydew", 0xf0fff0}, {"hotpink", 0xff69b4}, {"indianred", 0xcd5c5c},
    {"indigo", 0x4b0082}, {"ivory", 0xfffff0}, {"khaki", 0xf0e68c},
    {"lavender", 0xe6e6fa}, {"lavenderblush", 0xfff0f5}, {"lawngreen", 0x7cfc00},
    {"lemonchiffon", 0xfffacd}, {"lightblue", 0xadd8e6}, {"lightcoral", 0xf08080},
    {"lightcyan", 0xe0ffff}, {"lightgoldenrodyellow", 0xfafad2}, {"lightgray", 0xd3d3d3},
    {"lightgreen", 0x90ee90}, {"lightgrey", 0xd3d3d3}, {"lightpink", 0xffb6c1},
    {"lightsalmon", 0xffa07a}, {"lightseagreen", 0x20b2aa}, {"lightskyblue", 0x87cefa},
    {"lightslategray", 0x778899}, {"lightslategrey", 0x778899}, {"lightsteelblue", 0xb0c4de},
    {"lightyellow", 0xffffe0}, {"lime", 0x00ff00}, {"limegreen", 0x32cd32},
    {"linen", 0xfaf0e6}, {"magenta", 0xff00ff}, {"maroon", 0x800000},
    {"mediumaquamarine", 0x66cdaa}, {"mediumblue", 0x0000cd}, {"mediumorchid", 0xba55d3},
    {"mediumpurple", 0x9370db}, {"mediumseagreen", 0x3cb371}, {"mediumslateblue", 0x7b68ee},
    {"mediumspringgreen", 0x00fa9a}, {"mediumturquoise", 0x48d1cc}, {"mediumvioletred", 0xc71585},
    {"midnightblue", 0x191970}, {"mintcream", 0xf5fffa}, {"mistyrose", 0xffe4e1},
    {"moccasin", 0xffe4b5}, {"navajowhite", 0xffdead}, {"navy", 0x000080},
    {"oldlace", 0xfdf5e6}, {"olive", 0x808000}, {"olivedrab", 0x6b8e23},
    {"orange", 0xffa500}, {"orangered", 0xff4500}, {"orchid", 0xda70d6},
    {"palegoldenrod", 0xeee8aa}, {"palegreen", 0x98fb98}, {"paleturquoise", 0xafeeee},
    {"palevioletred", 0xdb7093}, {"papayawhip", 0xffefd5}, {"peachpuff", 0xffdab9},
    {"peru", 0xcd853f}, {"pink", 0xffc0cb}, {"plum", 0xdda0dd},
    {"powderblue", 0xb0e0e6}, {"purple", 0x800080}, {"rebeccapurple", 0x663399},
    {"red", 0xff0000}, {"rosybrown", 0xbc8f8f}, {"royalblue", 0x4169e1},
    {"saddlebrown", 0x8b4513}, {"salmon", 0xfa8072}, {"sandybrown", 0xf4a460},
    {"seagreen", 0x2e8b57}, {"seashell", 0xfff5ee}, {"sienna", 0xa0522d},
    {"silver", 0xc0c0c0}, {"skyblue", 0x87ceeb}, {"slateblue", 0x6a5acd},
    {"slategray", 0x708090}, {"slategrey", 0x708090}, {"snow", 0xfffafa},
    {"springgreen", 0x00ff7f}, {"steelblue", 0x4682b4}, {"tan", 0xd2b48c},
    {"teal", 0x008080}, {"thistle", 0xd8bfd8}, {"tomato", 0xff6347},
    {"turquoise", 0x40e0d0}, {"violet", 0xee82ee}, {"wheat", 0xf5deb3},
    {"white", 0xffffff}, {"whitesmoke", 0xf5f5f5}, {"yellow", 0xffff00},
    {"yellowgreen", 0x9acd32},
    {NULL, 0}
};

static lv_color_t blend_on_white(uint8_t r, uint8_t g, uint8_t b, int alpha_255) {
    if (alpha_255 >= 255) return lv_color_make(r, g, b);
    if (alpha_255 <= 0) return lv_color_make(255, 255, 255);
    int inv = 255 - alpha_255;
    uint8_t br = (uint8_t)((r * alpha_255 + 255 * inv) / 255);
    uint8_t bg = (uint8_t)((g * alpha_255 + 255 * inv) / 255);
    uint8_t bb = (uint8_t)((b * alpha_255 + 255 * inv) / 255);
    return lv_color_make(br, bg, bb);
}

static int parse_int_prefix(const char *s, const char **endp) {
    while (*s && c_isspace(*s)) s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') { s++; }
    int64_t val = 0;
    while (c_isdigit(*s)) {
        int digit = *s - '0';
        if (val < 1000000000LL)
            val = val * 10 + digit;
        if (val > 1000000000LL) val = 1000000000LL;
        s++;
    }
    if (endp) *endp = s;
    val *= sign;
    if (val > INT_MAX) return INT_MAX;
    if (val < INT_MIN) return INT_MIN;
    return (int)val;
}

static int parse_alpha_255(const char *s) {
    while (*s && (c_isspace(*s) || *s == '/' || *s == ',')) s++;
    bool pct = false;
    int int_part = 0;
    int frac_part = 0;
    int frac_div = 1;
    while (c_isdigit(*s)) {
        int digit = *s - '0';
        if (int_part < 1000) int_part = int_part * 10 + digit;
        if (int_part > 1000) int_part = 1000;
        s++;
    }
    if (*s == '.') {
        s++;
        while (c_isdigit(*s) && frac_div < 1000) {
            frac_part = frac_part * 10 + (*s - '0');
            frac_div *= 10;
            s++;
        }
        while (c_isdigit(*s)) s++;
    }
    if (*s == '%') pct = true;
    int val = pct ? ((int_part * 255 + (frac_part * 255) / frac_div) / 100)
                  : (int_part * 255 + (frac_part * 255) / frac_div);
    if (val < 0) val = 0;
    if (val > 255) val = 255;
    return val;
}

bool html_css_parse_color(const html_stylesheet_t *ss, const char *expr,
                          lv_color_t *out, bool *is_transparent) {
    if (!expr || !expr[0]) return false;
    char buf[192];
    resolve_css_value(ss, expr, buf, sizeof(buf), 0);
    const char *s = buf;
    size_t len = strlen(s);
    trim_slice(s, len, &s, &len);
    if (len == 0) return false;

    if (ci_eq(s, "transparent")) {
        if (is_transparent) *is_transparent = true;
        if (out) *out = lv_color_make(255, 255, 255);
        return true;
    }

    if (s[0] == '#') {
        size_t hlen = 0;
        while (1 + hlen < len && hex_val(s[1 + hlen]) >= 0) hlen++;
        if (hlen == 3 || hlen == 4) {
            int r = hex_val(s[1]) * 17;
            int g = hex_val(s[2]) * 17;
            int b = hex_val(s[3]) * 17;
            int a = (hlen == 4) ? hex_val(s[4]) * 17 : 255;
            if (a < 20) {
                if (is_transparent) *is_transparent = true;
                return true;
            }
            if (is_transparent) *is_transparent = false;
            if (out) *out = blend_on_white((uint8_t)r, (uint8_t)g, (uint8_t)b, a);
            return true;
        }
        if (hlen == 6 || hlen == 8) {
            int r = hex_val(s[1]) * 16 + hex_val(s[2]);
            int g = hex_val(s[3]) * 16 + hex_val(s[4]);
            int b = hex_val(s[5]) * 16 + hex_val(s[6]);
            int a = (hlen == 8) ? (hex_val(s[7]) * 16 + hex_val(s[8])) : 255;
            if (a < 20) {
                if (is_transparent) *is_transparent = true;
                return true;
            }
            if (is_transparent) *is_transparent = false;
            if (out) *out = blend_on_white((uint8_t)r, (uint8_t)g, (uint8_t)b, a);
            return true;
        }
        return false;
    }

    if (ci_prefix(s, "rgb(") || ci_prefix(s, "rgba(")) {
        const char *p = s + (ci_prefix(s, "rgba(") ? 5 : 4);
        int comp[3] = {0, 0, 0};
        for (int i = 0; i < 3; i++) {
            while (*p && (c_isspace(*p) || *p == ',')) p++;
            if (!c_isdigit(*p) && *p != '-' && *p != '+') return false;
            const char *endp = p;
            int v = parse_int_prefix(p, &endp);
            if (*endp == '.') {
                endp++;
                while (c_isdigit(*endp)) endp++;
            }
            if (*endp == '%') {
                v = (v * 255) / 100;
                endp++;
            }
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            comp[i] = v;
            p = endp;
        }
        int alpha = 255;
        while (*p && c_isspace(*p)) p++;
        if (*p == ',' || *p == '/') {
            alpha = parse_alpha_255(p);
        }
        if (alpha < 18) {
            if (is_transparent) *is_transparent = true;
            if (out) *out = lv_color_make(255, 255, 255);
            return true;
        }
        if (is_transparent) *is_transparent = false;
        if (out) *out = blend_on_white((uint8_t)comp[0], (uint8_t)comp[1], (uint8_t)comp[2], alpha);
        return true;
    }

    for (int i = 0; g_named_colors[i].name; i++) {
        if (ci_eq(s, g_named_colors[i].name)) {
            if (is_transparent) *is_transparent = false;
            if (out) *out = lv_color_hex(g_named_colors[i].hex);
            return true;
        }
    }
    return false;
}

/* ---- Length parser ---- */

static bool parse_css_length_depth(const html_stylesheet_t *ss, const char *expr,
                                   int16_t *px_out, int16_t *pct_out,
                                   bool *is_auto, unsigned depth) {
    if (depth > 8) return false;
    if (is_auto) *is_auto = false;
    if (px_out) *px_out = HTML_CSS_UNSET;
    if (pct_out) *pct_out = HTML_CSS_UNSET;

    char buf[160];
    resolve_css_value(ss, expr, buf, sizeof(buf), depth);
    const char *s = buf;
    size_t len = strlen(s);
    trim_slice(s, len, &s, &len);
    if (len == 0) return false;

    if (ci_eq(s, "auto")) {
        if (is_auto) *is_auto = true;
        return true;
    }
    if (ci_eq(s, "none") || ci_eq(s, "0") || ci_eq(s, "initial")) {
        if (ci_eq(s, "0") && px_out) {
            *px_out = 0;
            return true;
        }
        return false;
    }

    /* Handle bounded two-term calc() arithmetic used by real page shells,
     * such as GitHub's min-height:calc(100vh - 64px). */
    if (ci_prefix(s, "calc(") && s[len - 1] == ')') {
        const char *body = s + 5;
        size_t body_len = len - 6;
        int paren_depth = 0;
        size_t operator_at = SIZE_MAX;
        char operation = '\0';
        for (size_t i = 0; i < body_len; ++i) {
            if (body[i] == '(') paren_depth++;
            else if (body[i] == ')' && paren_depth > 0) paren_depth--;
            else if (paren_depth == 0 && (body[i] == '+' || body[i] == '-') &&
                     i > 0 && c_isspace(body[i - 1])) {
                operator_at = i;
                operation = body[i];
                break;
            }
        }
        if (operator_at == SIZE_MAX) return false;

        const char *left_begin = body;
        const char *right_begin = body + operator_at + 1;
        size_t left_len = operator_at;
        size_t right_len = body_len - operator_at - 1;
        trim_slice(left_begin, left_len, &left_begin, &left_len);
        trim_slice(right_begin, right_len, &right_begin, &right_len);
        if (left_len == 0 || right_len == 0 || left_len >= sizeof(buf) ||
            right_len >= sizeof(buf)) return false;
        char left[160], right[160];
        memcpy(left, left_begin, left_len);
        left[left_len] = '\0';
        memcpy(right, right_begin, right_len);
        right[right_len] = '\0';

        int16_t left_px = HTML_CSS_UNSET, left_pct = HTML_CSS_UNSET;
        int16_t right_px = HTML_CSS_UNSET, right_pct = HTML_CSS_UNSET;
        if (!parse_css_length_depth(ss, left, &left_px, &left_pct, NULL, depth + 1) ||
            !parse_css_length_depth(ss, right, &right_px, &right_pct, NULL, depth + 1))
            return false;

        if (left_px != HTML_CSS_UNSET && right_px != HTML_CSS_UNSET &&
            left_pct == HTML_CSS_UNSET && right_pct == HTML_CSS_UNSET) {
            int64_t value = operation == '+'
                          ? (int64_t)left_px + right_px
                          : (int64_t)left_px - right_px;
            if (value > INT16_MAX) value = INT16_MAX;
            if (value < INT16_MIN) value = INT16_MIN;
            if (px_out) *px_out = (int16_t)value;
            return true;
        }
        if (left_pct != HTML_CSS_UNSET && right_pct != HTML_CSS_UNSET &&
            left_px == HTML_CSS_UNSET && right_px == HTML_CSS_UNSET) {
            int value = operation == '+'
                      ? (int)left_pct + right_pct
                      : (int)left_pct - right_pct;
            if (value < 0) value = 0;
            if (value > 100) value = 100;
            if (pct_out) *pct_out = (int16_t)value;
            return true;
        }
        return false;
    }

    const char *p = s;
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    else if (*p == '+') { p++; }
    if (!c_isdigit(*p) && *p != '.') return false;

    /* CSS comes from the network. Keep numeric parsing saturating: a long
     * hostile length must not overflow signed int before it is clamped to the
     * renderer's int16_t style fields. */
    int64_t int_part = 0;
    while (c_isdigit(*p)) {
        int digit = *p - '0';
        if (int_part < 100000000000LL)
            int_part = int_part * 10 + digit;
        if (int_part > 100000000000LL)
            int_part = 100000000000LL;
        p++;
    }
    int64_t frac_tenths = 0;
    if (*p == '.') {
        p++;
        if (c_isdigit(*p)) {
            frac_tenths = *p - '0';
            p++;
        }
        while (c_isdigit(*p)) p++;
    }

    int64_t tenths = sign * (int_part * 10 + frac_tenths);
    while (*p && c_isspace(*p)) p++;

    /* Saturate before storing in the renderer's bounded style values. */
#define CSS_LENGTH_STORE(v) do { \
    int64_t css_length_value = (v); \
    if (css_length_value > INT16_MAX) css_length_value = INT16_MAX; \
    if (css_length_value < INT16_MIN) css_length_value = INT16_MIN; \
    if (px_out) *px_out = (int16_t)css_length_value; \
} while (0)

    if (*p == '\0' || ci_prefix(p, "px")) {
        int64_t px = tenths / 10;
        if (px == 0 && tenths != 0) px = (tenths > 0) ? 1 : -1;
        CSS_LENGTH_STORE(px);
        return true;
    }
    if (ci_prefix(p, "rem") || ci_prefix(p, "em")) {
        int64_t px = (tenths * 16 + (tenths >= 0 ? 5 : -5)) / 10;
        if (px == 0 && tenths != 0) px = (tenths > 0) ? 1 : -1;
        CSS_LENGTH_STORE(px);
        return true;
    }
    if (ci_prefix(p, "pt")) {
        int64_t px = (tenths * 4) / 30;
        CSS_LENGTH_STORE(px);
        return true;
    }
    if (*p == '%') {
        int pct = tenths / 10;
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
        if (pct_out) *pct_out = (int16_t)pct;
        return true;
    }
    if (ci_prefix(p, "vw")) {
        int vw = (ss && ss->viewport_w > 0) ? ss->viewport_w : 960;
        int64_t px = (tenths * vw) / 1000;
        CSS_LENGTH_STORE(px);
        return true;
    }
    if (ci_prefix(p, "vh")) {
        int vh = (ss && ss->viewport_h > 0) ? ss->viewport_h
               : ((ss && ss->viewport_w > 0) ? ((ss->viewport_w * 56) / 100) : 520);
        int64_t px = (tenths * vh) / 1000;
        CSS_LENGTH_STORE(px);
        return true;
    }
#undef CSS_LENGTH_STORE
    return false;
}

static bool parse_css_length(const html_stylesheet_t *ss, const char *expr,
                             int16_t *px_out, int16_t *pct_out, bool *is_auto) {
    return parse_css_length_depth(ss, expr, px_out, pct_out, is_auto, 0);
}

static int16_t clamp_i16(int v, int lo, int hi) {
    if (v < lo) return (int16_t)lo;
    if (v > hi) return (int16_t)hi;
    return (int16_t)v;
}

/* Splits a space-separated value respecting (...) parentheses. */
static int split_ws_tokens(const char *val, char tokens[][96], int max_tokens) {
    int count = 0;
    const char *p = val;
    while (*p && count < max_tokens) {
        while (*p && c_isspace(*p)) p++;
        if (!*p) break;
        const char *start = p;
        int paren = 0;
        while (*p && (paren > 0 || !c_isspace(*p))) {
            if (*p == '(') paren++;
            else if (*p == ')' && paren > 0) paren--;
            p++;
        }
        size_t tlen = (size_t)(p - start);
        copy_slice(start, tlen, tokens[count], 96);
        count++;
    }
    return count;
}

static void apply_box_shorthand(const html_stylesheet_t *ss, const char *val,
                                int16_t sides[4], bool allow_auto, int lo, int hi) {
    char tok[4][96];
    int n = split_ws_tokens(val, tok, 4);
    if (n == 0) return;

    int16_t parsed[4] = {HTML_CSS_UNSET, HTML_CSS_UNSET, HTML_CSS_UNSET, HTML_CSS_UNSET};
    for (int i = 0; i < n; i++) {
        int16_t px = HTML_CSS_UNSET;
        int16_t pct = HTML_CSS_UNSET;
        bool is_auto = false;
        if (!parse_css_length(ss, tok[i], &px, &pct, &is_auto)) return;
        if (is_auto && allow_auto) {
            parsed[i] = HTML_CSS_LEN_AUTO;
        } else if (px != HTML_CSS_UNSET) {
            parsed[i] = clamp_i16(px, lo, hi);
        } else if (pct != HTML_CSS_UNSET) {
            int vw = (ss && ss->viewport_w > 0) ? ss->viewport_w : 900;
            parsed[i] = clamp_i16((pct * vw) / 100, lo, hi);
        } else {
            return;
        }
    }

    if (n == 1) {
        sides[0] = sides[1] = sides[2] = sides[3] = parsed[0];
    } else if (n == 2) {
        sides[0] = sides[2] = parsed[0];
        sides[1] = sides[3] = parsed[1];
    } else if (n == 3) {
        sides[0] = parsed[0];
        sides[1] = sides[3] = parsed[1];
        sides[2] = parsed[2];
    } else {
        sides[0] = parsed[0];
        sides[1] = parsed[1];
        sides[2] = parsed[2];
        sides[3] = parsed[3];
    }
}

static void apply_single_edge_len(const html_stylesheet_t *ss, const char *val,
                                  int16_t *dst, bool allow_auto, int lo, int hi) {
    int16_t px = HTML_CSS_UNSET;
    int16_t pct = HTML_CSS_UNSET;
    bool is_auto = false;
    if (!parse_css_length(ss, val, &px, &pct, &is_auto)) return;
    if (is_auto && allow_auto) {
        *dst = HTML_CSS_LEN_AUTO;
    } else if (px != HTML_CSS_UNSET) {
        *dst = clamp_i16(px, lo, hi);
    }
}

static void apply_border_side(const html_stylesheet_t *ss, const char *val,
                              css_style_t *st, int side_mask) {
    char resolved[192];
    resolve_css_value(ss, val, resolved, sizeof(resolved), 0);
    if (ci_eq(resolved, "none") || ci_eq(resolved, "0") || ci_eq(resolved, "hidden")) {
        for (int i = 0; i < 4; i++) {
            if (side_mask & (1 << i)) st->border_w[i] = 0;
        }
        return;
    }

    char tok[8][96];
    int n = split_ws_tokens(resolved, tok, 8);
    int16_t bw = HTML_CSS_UNSET;
    bool is_none = false;
    bool got_color = false;
    bool col_trans = false;
    lv_color_t col = lv_color_make(200, 204, 209);

    for (int i = 0; i < n; i++) {
        if (ci_eq(tok[i], "none") || ci_eq(tok[i], "hidden")) {
            is_none = true;
            bw = 0;
        } else if (ci_eq(tok[i], "solid") || ci_eq(tok[i], "dashed") ||
                   ci_eq(tok[i], "dotted") || ci_eq(tok[i], "double") ||
                   ci_eq(tok[i], "groove") || ci_eq(tok[i], "ridge")) {
            if (bw == HTML_CSS_UNSET) bw = 1;
        } else if (ci_eq(tok[i], "thin")) {
            bw = 1;
        } else if (ci_eq(tok[i], "medium")) {
            bw = 2;
        } else if (ci_eq(tok[i], "thick")) {
            bw = 3;
        } else {
            int16_t px = HTML_CSS_UNSET;
            if (parse_css_length(ss, tok[i], &px, NULL, NULL) && px != HTML_CSS_UNSET) {
                bw = clamp_i16(px, 0, 12);
            } else if (html_css_parse_color(ss, tok[i], &col, &col_trans)) {
                got_color = true;
            }
        }
    }

    if (is_none) bw = 0;
    if (bw != HTML_CSS_UNSET) {
        for (int i = 0; i < 4; i++) {
            if (side_mask & (1 << i)) st->border_w[i] = bw;
        }
    }
    if (got_color && !col_trans) {
        st->has_border_color = true;
        st->border_color = col;
    } else if (got_color && col_trans && bw == HTML_CSS_UNSET) {
        for (int i = 0; i < 4; i++) {
            if (side_mask & (1 << i)) st->border_w[i] = 0;
        }
    }
}

/* ---- Declaration block evaluator ---- */

typedef struct {
    bool seen_pos_abs;
    bool seen_overflow_hidden;
    bool seen_1px_w;
    bool seen_1px_h;
    bool seen_zero_h;
} sr_track_t;

static void parse_grid_columns(const char *value, css_style_t *st) {
    if (!value || !st) return;
    st->grid_col_count = 0;
    memset(st->grid_col_kind, 0, sizeof(st->grid_col_kind));
    memset(st->grid_col_value, 0, sizeof(st->grid_col_value));
    memset(st->grid_col_offset, 0, sizeof(st->grid_col_offset));
    if (ci_eq(value, "none") || ci_eq(value, "subgrid") ||
        ci_eq(value, "masonry")) return;
    uint8_t count = 0;
    size_t i = 0;
    while (value[i] && count < HTML_CSS_MAX_GRID_COLS) {
        while (c_isspace(value[i])) i++;
        if (!value[i]) break;
        size_t start = i;
        int depth = 0;
        while (value[i]) {
            if (value[i] == '(') depth++;
            else if (value[i] == ')' && depth > 0) depth--;
            else if (c_isspace(value[i]) && depth == 0) break;
            i++;
        }
        size_t len = i - start;
        if (len == 0) continue;
        char token[96];
        if (len >= sizeof(token)) len = sizeof(token) - 1;
        memcpy(token, value + start, len);
        token[len] = '\0';

        /* For minmax(min,max), use the maximum track as the bounded estimate. */
        if (ci_prefix(token, "minmax(")) {
            char *comma = strchr(token, ',');
            char *close = strrchr(token, ')');
            if (comma && close && close > comma) {
                size_t max_len = (size_t)(close - comma - 1);
                memmove(token, comma + 1, max_len);
                token[max_len] = '\0';
                char *trim = token;
                while (c_isspace(*trim)) trim++;
                if (trim != token) memmove(token, trim, strlen(trim) + 1);
                while (*token && c_isspace(token[strlen(token) - 1]))
                    token[strlen(token) - 1] = '\0';
            }
        }

        uint8_t kind = 3;
        int16_t amount = 1;
        int16_t offset = 0;
        const char *fr = strstr(token, "fr");
        if (ci_prefix(token, "calc(") && token[strlen(token) - 1] == ')') {
            /* Grid track sizes commonly use calc(100% - fixed-sidebar - gap).
             * Keep the bounded percentage and pixel terms separately so the
             * layout pass can evaluate them against the actual grid width. */
            const char *expr = token + 5;
            size_t expr_len = strlen(token) - 6U;
            int32_t percent = 0;
            int32_t pixels = 0;
            size_t cursor = 0;
            int sign = 1;
            bool valid = expr_len > 0;
            while (valid && cursor < expr_len) {
                while (cursor < expr_len && c_isspace(expr[cursor])) cursor++;
                if (cursor >= expr_len) break;
                if (expr[cursor] == '+' || expr[cursor] == '-') {
                    sign = expr[cursor++] == '-' ? -1 : 1;
                    while (cursor < expr_len && c_isspace(expr[cursor])) cursor++;
                }
                size_t term_start = cursor;
                while (cursor < expr_len && expr[cursor] != '+' &&
                       expr[cursor] != '-') cursor++;
                size_t term_len = cursor - term_start;
                while (term_len && c_isspace(expr[term_start + term_len - 1]))
                    term_len--;
                while (term_len && c_isspace(expr[term_start])) {
                    term_start++;
                    term_len--;
                }
                if (!term_len || term_len >= 48) { valid = false; break; }
                char term[48];
                memcpy(term, expr + term_start, term_len);
                term[term_len] = '\0';
                int16_t term_px = HTML_CSS_UNSET;
                int16_t term_pct = HTML_CSS_UNSET;
                if (!parse_css_length(NULL, term, &term_px, &term_pct, NULL)) {
                    valid = false;
                    break;
                }
                if (term_pct != HTML_CSS_UNSET && term_px == HTML_CSS_UNSET)
                    percent += sign * term_pct;
                else if (term_px != HTML_CSS_UNSET && term_pct == HTML_CSS_UNSET)
                    pixels += sign * term_px;
                else {
                    valid = false;
                    break;
                }
                sign = 1;
            }
            if (valid && percent >= 0 && percent <= 100 &&
                pixels >= -4096 && pixels <= 4096) {
                kind = percent ? 5 : 1;
                amount = (int16_t)(percent ? percent : pixels);
                offset = percent ? (int16_t)pixels : 0;
            }
        } else if (fr) {
            const char *end = NULL;
            int n = parse_int_prefix(token, &end);
            if (end != token && n > 0 && n <= 16) amount = (int16_t)n;
        } else if (strchr(token, '%')) {
            const char *end = NULL;
            int n = parse_int_prefix(token, &end);
            if (end != token && n >= 0 && n <= 100) {
                kind = 2;
                amount = (int16_t)n;
            }
        } else {
            int16_t px = HTML_CSS_UNSET;
            if (parse_css_length(NULL, token, &px, NULL, NULL) &&
                px != HTML_CSS_UNSET && px >= 0 && px <= 4096) {
                kind = 1;
                amount = px;
            } else if (ci_eq(token, "auto") || ci_eq(token, "min-content") ||
                       ci_eq(token, "max-content")) {
                kind = 4;
                amount = 0;
            }
        }
        st->grid_col_kind[count] = kind;
        st->grid_col_value[count] = amount;
        st->grid_col_offset[count] = offset;
        count++;
    }
    if (count > 0) st->grid_col_count = count;
}

static void apply_single_decl(const html_stylesheet_t *ss,
                              const char *prop, const char *val,
                              css_style_t *st, sr_track_t *sr) {
    char rval[224];
    resolve_css_value(ss, val, rval, sizeof(rval), 0);
    if (!rval[0] && ci_contains(val, "var("))
        css_log_drop("unresolved-var", prop, val);

    if (ci_eq(prop, "display")) {
        if (ci_eq(rval, "grid") || ci_eq(rval, "inline-grid"))
            css_trace_grid("display-observed", prop, val);
        if (ci_eq(rval, "none")) st->display = CSS_DISP_NONE;
        else if (ci_eq(rval, "block") || ci_eq(rval, "flow-root")) st->display = CSS_DISP_BLOCK;
        else if (ci_eq(rval, "inline")) st->display = CSS_DISP_INLINE;
        else if (ci_eq(rval, "inline-block")) st->display = CSS_DISP_INLINE_BLOCK;
        else if (ci_eq(rval, "flex")) st->display = CSS_DISP_FLEX;
        else if (ci_eq(rval, "inline-flex")) st->display = CSS_DISP_INLINE_FLEX;
        else if (ci_eq(rval, "grid") || ci_eq(rval, "inline-grid")) st->display = CSS_DISP_GRID;
        else if (ci_eq(rval, "table")) st->display = CSS_DISP_TABLE;
        else if (ci_eq(rval, "table-row")) st->display = CSS_DISP_TABLE_ROW;
        else if (ci_eq(rval, "table-cell")) st->display = CSS_DISP_TABLE_CELL;
        else if (ci_eq(rval, "list-item")) st->display = CSS_DISP_LIST_ITEM;
    } else if (ci_eq(prop, "visibility")) {
        if (ci_eq(rval, "hidden") || ci_eq(rval, "collapse")) st->visibility = CSS_VIS_HIDDEN;
        else if (ci_eq(rval, "visible")) st->visibility = CSS_VIS_VISIBLE;
    } else if (ci_eq(prop, "color")) {
        lv_color_t c;
        bool tr = false;
        if (html_css_parse_color(ss, val, &c, &tr) && !tr) {
            st->has_color = true;
            st->own_color = true;
            st->color = c;
        }
    } else if (ci_eq(prop, "background-color")) {
        lv_color_t c;
        bool tr = false;
        if (html_css_parse_color(ss, val, &c, &tr)) {
            st->has_bg = true;
            st->bg_transparent = tr;
            if (!tr) st->bg = c;
        }
    } else if (ci_eq(prop, "background")) {
        if (ci_eq(rval, "none") || ci_eq(rval, "transparent")) {
            st->has_bg = true;
            st->bg_transparent = true;
        } else {
            char tok[8][96];
            int n = split_ws_tokens(rval, tok, 8);
            for (int i = 0; i < n; i++) {
                if (ci_prefix(tok[i], "url(") || ci_contains(tok[i], "gradient(")) continue;
                lv_color_t c;
                bool tr = false;
                if (html_css_parse_color(ss, tok[i], &c, &tr)) {
                    st->has_bg = true;
                    st->bg_transparent = tr;
                    if (!tr) st->bg = c;
                    break;
                }
            }
        }
    } else if (ci_eq(prop, "font")) {
        char tok[6][96];
        int n = split_ws_tokens(rval, tok, 6);
        for (int i = 0; i < n; i++) {
            if (ci_eq(tok[i], "bold") || ci_eq(tok[i], "700") || ci_eq(tok[i], "600")) {
                st->has_bold = true;
                st->bold = true;
            } else if (ci_eq(tok[i], "italic") || ci_eq(tok[i], "oblique")) {
                st->has_italic = true;
                st->italic = true;
            } else {
                const char *slash = strchr(tok[i], '/');
                char ftok[48];
                if (slash) {
                    copy_slice(tok[i], (size_t)(slash - tok[i]), ftok, sizeof(ftok));
                } else {
                    copy_slice(tok[i], strlen(tok[i]), ftok, sizeof(ftok));
                }
                int16_t px = HTML_CSS_UNSET, pct = HTML_CSS_UNSET;
                if (parse_css_length(ss, ftok, &px, &pct, NULL)) {
                    if (px > 0) { st->font_px = clamp_i16(px, 11, 32); st->own_font_px = true; }
                    else if (pct > 0) { st->font_px = clamp_i16((14 * pct) / 100, 11, 32); st->own_font_px = true; }
                }
            }
        }
    } else if (ci_eq(prop, "font-size")) {
        int16_t px = HTML_CSS_UNSET, pct = HTML_CSS_UNSET;
        if (ci_eq(rval, "small") || ci_eq(rval, "smaller")) { st->font_px = 12; st->own_font_px = true; }
        else if (ci_eq(rval, "medium")) { st->font_px = 14; st->own_font_px = true; }
        else if (ci_eq(rval, "large")) { st->font_px = 18; st->own_font_px = true; }
        else if (ci_eq(rval, "x-large")) { st->font_px = 20; st->own_font_px = true; }
        else if (ci_eq(rval, "xx-large")) { st->font_px = 24; st->own_font_px = true; }
        else if (parse_css_length(ss, val, &px, &pct, NULL)) {
            if (px != HTML_CSS_UNSET) {
                if (px == 0) st->is_sr_only = true;
                else { st->font_px = clamp_i16(px, 11, 32); st->own_font_px = true; }
            } else if (pct != HTML_CSS_UNSET && pct > 0) {
                st->font_px = clamp_i16((14 * pct) / 100, 11, 32);
                st->own_font_px = true;
            }
        }
    } else if (ci_eq(prop, "font-weight")) {
        if (ci_eq(rval, "bold") || ci_eq(rval, "bolder") ||
            ci_eq(rval, "600") || ci_eq(rval, "700") || ci_eq(rval, "800") || ci_eq(rval, "900")) {
            st->has_bold = true;
            st->bold = true;
        } else if (ci_eq(rval, "normal") || ci_eq(rval, "400") || ci_eq(rval, "300")) {
            st->has_bold = true;
            st->bold = false;
        }
    } else if (ci_eq(prop, "font-style")) {
        if (ci_eq(rval, "italic") || ci_eq(rval, "oblique")) {
            st->has_italic = true;
            st->italic = true;
        } else if (ci_eq(rval, "normal")) {
            st->has_italic = true;
            st->italic = false;
        }
    } else if (ci_eq(prop, "text-decoration") || ci_eq(prop, "text-decoration-line")) {
        if (ci_contains(rval, "underline")) {
            st->has_underline = true;
            st->own_underline = true;
            st->underline = true;
        } else if (ci_contains(rval, "line-through")) {
            st->has_strike = true;
            st->strike = true;
        } else if (ci_eq(rval, "none")) {
            st->has_underline = true;
            st->own_underline = true;
            st->underline = false;
            st->has_strike = true;
            st->strike = false;
        }
    } else if (ci_eq(prop, "text-align")) {
        if (ci_eq(rval, "center")) st->text_align = CSS_ALIGN_CENTER;
        else if (ci_eq(rval, "right") || ci_eq(rval, "end")) st->text_align = CSS_ALIGN_RIGHT;
        else if (ci_eq(rval, "left") || ci_eq(rval, "start")) st->text_align = CSS_ALIGN_LEFT;
    } else if (ci_eq(prop, "white-space")) {
        if (ci_eq(rval, "pre") || ci_eq(rval, "pre-wrap") || ci_eq(rval, "break-spaces")) {
            st->has_preformatted = true;
            st->preformatted = true;
        } else if (ci_eq(rval, "normal")) {
            st->has_preformatted = true;
            st->preformatted = false;
        }
    } else if (ci_eq(prop, "padding-inline") || ci_eq(prop, "margin-inline")) {
        char edges[2][96];
        int count = split_ws_tokens(rval, edges, 2);
        bool margin = ci_eq(prop, "margin-inline");
        int16_t *values = margin ? st->margin : st->pad;
        if (count > 0) {
            apply_single_edge_len(ss, edges[0], &values[3], margin, margin ? -24 : 0, margin ? 120 : 180);
            apply_single_edge_len(ss, edges[count > 1 ? 1 : 0], &values[1], margin, margin ? -24 : 0, margin ? 120 : 180);
        }
    } else if (ci_eq(prop, "box-sizing")) {
        if (ci_eq(rval, "border-box")) st->border_box = true;
        else if (ci_eq(rval, "content-box")) st->border_box = false;
    } else if (ci_eq(prop, "padding")) {
        apply_box_shorthand(ss, rval, st->pad, false, 0, 180);
    } else if (ci_eq(prop, "padding-top")) {
        apply_single_edge_len(ss, val, &st->pad[0], false, 0, 180);
    } else if (ci_eq(prop, "padding-right")) {
        apply_single_edge_len(ss, val, &st->pad[1], false, 0, 180);
    } else if (ci_eq(prop, "padding-bottom")) {
        apply_single_edge_len(ss, val, &st->pad[2], false, 0, 180);
    } else if (ci_eq(prop, "padding-left")) {
        apply_single_edge_len(ss, val, &st->pad[3], false, 0, 180);
    } else if (ci_eq(prop, "margin")) {
        apply_box_shorthand(ss, rval, st->margin, true, -24, 120);
    } else if (ci_eq(prop, "margin-top")) {
        apply_single_edge_len(ss, val, &st->margin[0], true, -24, 120);
    } else if (ci_eq(prop, "margin-right")) {
        apply_single_edge_len(ss, val, &st->margin[1], true, -24, 120);
    } else if (ci_eq(prop, "margin-bottom")) {
        apply_single_edge_len(ss, val, &st->margin[2], true, -24, 120);
    } else if (ci_eq(prop, "margin-left")) {
        apply_single_edge_len(ss, val, &st->margin[3], true, -24, 120);
        if (parse_int_prefix(rval, NULL) <= -500) st->offscreen_hidden = true;
    } else if (ci_eq(prop, "border")) {
        apply_border_side(ss, val, st, 0x0F);
    } else if (ci_eq(prop, "border-top")) {
        apply_border_side(ss, val, st, 0x01);
    } else if (ci_eq(prop, "border-right")) {
        apply_border_side(ss, val, st, 0x02);
    } else if (ci_eq(prop, "border-bottom")) {
        apply_border_side(ss, val, st, 0x04);
    } else if (ci_eq(prop, "border-left")) {
        apply_border_side(ss, val, st, 0x08);
    } else if (ci_eq(prop, "border-color")) {
        lv_color_t c;
        bool tr = false;
        if (html_css_parse_color(ss, val, &c, &tr) && !tr) {
            st->has_border_color = true;
            st->border_color = c;
        }
    } else if (ci_eq(prop, "border-width")) {
        int16_t b[4] = {HTML_CSS_UNSET, HTML_CSS_UNSET, HTML_CSS_UNSET, HTML_CSS_UNSET};
        apply_box_shorthand(ss, val, b, false, 0, 12);
        for (int i = 0; i < 4; i++) {
            if (b[i] != HTML_CSS_UNSET) st->border_w[i] = b[i];
        }
    } else if (ci_eq(prop, "border-radius")) {
        int16_t px = HTML_CSS_UNSET;
        if (parse_css_length(ss, val, &px, NULL, NULL) && px != HTML_CSS_UNSET) {
            st->radius = clamp_i16(px, 0, 28);
        }
    } else if (ci_eq(prop, "width")) {
        int16_t px = HTML_CSS_UNSET, pct = HTML_CSS_UNSET;
        bool is_auto = false;
        if (parse_css_length(ss, val, &px, &pct, &is_auto)) {
            if (is_auto) { st->width_px = HTML_CSS_LEN_AUTO; st->width_pct = HTML_CSS_UNSET; }
            else if (px != HTML_CSS_UNSET) {
                st->width_px = px;
                st->width_pct = HTML_CSS_UNSET;
                if (sr && px >= 0 && px <= 2) sr->seen_1px_w = true;
                else if (sr && px > 4) sr->seen_1px_w = false;
            } else if (pct != HTML_CSS_UNSET) {
                st->width_pct = pct;
                st->width_px = HTML_CSS_UNSET;
            }
        }
    } else if (ci_eq(prop, "max-width")) {
        int16_t px = HTML_CSS_UNSET, pct = HTML_CSS_UNSET;
        if (parse_css_length(ss, val, &px, &pct, NULL)) {
            if (px != HTML_CSS_UNSET) st->max_width_px = px;
            else if (pct != HTML_CSS_UNSET) st->max_width_pct = pct;
        }
    } else if (ci_eq(prop, "min-width")) {
        int16_t px = HTML_CSS_UNSET;
        if (parse_css_length(ss, val, &px, NULL, NULL) && px != HTML_CSS_UNSET) {
            st->min_width_px = px;
        }
    } else if (ci_eq(prop, "height")) {
        int16_t px = HTML_CSS_UNSET;
        if (parse_css_length(ss, val, &px, NULL, NULL) && px != HTML_CSS_UNSET) {
            st->height_px = px;
            if (sr) {
                sr->seen_1px_h = (px >= 0 && px <= 2);
                sr->seen_zero_h = (px == 0);
            }
        }
    } else if (ci_eq(prop, "min-height")) {
        int16_t px = HTML_CSS_UNSET;
        if (parse_css_length(ss, val, &px, NULL, NULL) && px != HTML_CSS_UNSET) {
            st->min_height_px = px;
        }
    } else if (ci_eq(prop, "max-height")) {
        int16_t px = HTML_CSS_UNSET;
        if (parse_css_length(ss, val, &px, NULL, NULL) && px != HTML_CSS_UNSET) {
            if (sr) sr->seen_zero_h = (px == 0);
        }
    } else if (ci_eq(prop, "opacity")) {
        const char *q = rval;
        bool has_nonzero = false;
        while (*q && *q != '%') {
            if (*q >= '1' && *q <= '9') { has_nonzero = true; break; }
            q++;
        }
        st->opacity_zero = !has_nonzero && (rval[0] == '0' || rval[0] == '.');
    } else if (ci_eq(prop, "flex-direction")) {
        if (ci_prefix(rval, "column")) st->flex_dir = CSS_FLEX_COL;
        else if (ci_prefix(rval, "row")) st->flex_dir = CSS_FLEX_ROW;
    } else if (ci_eq(prop, "flex-wrap")) {
        if (ci_eq(rval, "wrap") || ci_eq(rval, "wrap-reverse")) st->flex_wrap = CSS_WRAP_WRAP;
        else if (ci_eq(rval, "nowrap")) st->flex_wrap = CSS_WRAP_NOWRAP;
    } else if (ci_eq(prop, "flex-flow")) {
        if (ci_contains(rval, "column")) st->flex_dir = CSS_FLEX_COL;
        else if (ci_contains(rval, "row")) st->flex_dir = CSS_FLEX_ROW;
        if (ci_contains(rval, "wrap") && !ci_contains(rval, "nowrap")) st->flex_wrap = CSS_WRAP_WRAP;
    } else if (ci_eq(prop, "flex-grow")) {
        const char *end = NULL;
        int grow = parse_int_prefix(rval, &end);
        while (*end && c_isspace(*end)) end++;
        if (end != rval && !*end && grow >= 0 && grow <= 16)
            st->flex_grow = (uint8_t)grow;
    } else if (ci_eq(prop, "order")) {
        const char *end = NULL;
        int order = parse_int_prefix(rval, &end);
        while (*end && c_isspace(*end)) end++;
        if (end != rval && !*end && order >= INT16_MIN && order <= INT16_MAX)
            st->flex_order = (int16_t)order;
    } else if (ci_eq(prop, "flex")) {
        /* LVGL supplies flex growth; preserve percentage bases for row items. */
        /* The shorthand resets the old basis even when the new basis is auto. */
        st->flex_basis_pct = HTML_CSS_UNSET;
        st->flex_basis_zero = false;
        if (ci_eq(rval, "none") || ci_eq(rval, "initial")) {
            st->flex_grow = 0;
        } else if (ci_eq(rval, "auto")) {
            st->flex_grow = 1;
        } else {
            char flex_tokens[3][96];
            int count = split_ws_tokens(rval, flex_tokens, 3);
            if (count == 1 && strchr(flex_tokens[0], '%')) {
                /* CSS's single percentage form expands to 1 1 <basis>. */
                const char *end = NULL;
                int basis = parse_int_prefix(flex_tokens[0], &end);
                if (end != flex_tokens[0] && *end == '%' && !end[1] &&
                    basis >= 0 && basis <= 100) {
                    st->flex_grow = 1;
                    st->flex_basis_pct = (int16_t)basis;
                    st->flex_basis_zero = (basis == 0);
                }
            } else if (count > 0) {
                const char *end = NULL;
                int grow = parse_int_prefix(flex_tokens[0], &end);
                if (end != flex_tokens[0] && grow >= 0 && grow <= 16)
                    st->flex_grow = (uint8_t)grow;
                if (count == 1 && end != flex_tokens[0] && !*end &&
                    grow >= 0 && grow <= 16)
                    st->flex_basis_zero = true; /* flex: <number> => basis 0% */
                for (int i = 1; i < count; ++i) {
                    size_t len = strlen(flex_tokens[i]);
                    /* The second unitless number is flex-shrink, not a
                     * basis. A third token, or a value with units, is the
                     * basis. The kernel does not yet implement shrink. */
                    if (i == 1 && strcmp(flex_tokens[i], "0") == 0) {
                        if (count == 2) st->flex_basis_zero = true;
                        continue;
                    }
                    if (strcmp(flex_tokens[i], "0") == 0 ||
                        ci_eq(flex_tokens[i], "0px") ||
                        ci_eq(flex_tokens[i], "0%")) {
                        st->flex_basis_zero = true;
                        st->flex_basis_pct = HTML_CSS_UNSET;
                    }
                    if (len > 1 && flex_tokens[i][len - 1] == '%') {
                        flex_tokens[i][len - 1] = '\0';
                        const char *basis_end = NULL;
                        int basis = parse_int_prefix(flex_tokens[i], &basis_end);
                        if (basis_end != flex_tokens[i] && !*basis_end &&
                            basis >= 0 && basis <= 100)
                            st->flex_basis_pct = (int16_t)basis;
                        if (basis_end != flex_tokens[i] && !*basis_end &&
                            basis == 0)
                            st->flex_basis_zero = true;
                    }
                }
            }
        }
    } else if (ci_eq(prop, "justify-content")) {
        if (ci_eq(rval, "center")) st->justify_content = CSS_JUSTIFY_CENTER;
        else if (ci_contains(rval, "end")) st->justify_content = CSS_JUSTIFY_END;
        else if (ci_eq(rval, "space-between")) st->justify_content = CSS_JUSTIFY_BETWEEN;
        else if (ci_eq(rval, "space-around") || ci_eq(rval, "space-evenly")) {
            st->justify_content = CSS_JUSTIFY_AROUND;
        } else st->justify_content = CSS_JUSTIFY_START;
    } else if (ci_eq(prop, "align-items")) {
        if (ci_eq(rval, "center")) st->align_items = CSS_AITEMS_CENTER;
        else if (ci_contains(rval, "end")) st->align_items = CSS_AITEMS_END;
        else if (ci_eq(rval, "start") || ci_eq(rval, "flex-start"))
            st->align_items = CSS_AITEMS_START;
        else if (ci_eq(rval, "stretch")) st->align_items = CSS_AITEMS_STRETCH;
    } else if (ci_eq(prop, "grid-template-columns")) {
        parse_grid_columns(rval, st);
        bool unknown_track = !st->grid_col_count || ci_contains(rval, "repeat(") ||
                             ci_contains(rval, "minmax(") || strchr(rval, '.') != NULL;
        for (unsigned i = 0; i < st->grid_col_count; ++i)
            if (!st->grid_col_kind[i]) unknown_track = true;
        css_trace_grid(unknown_track ? "columns-unsupported-or-partial" : "columns-parsed", prop, val);
    } else if (ci_eq(prop, "grid-column") || ci_eq(prop, "grid-column-start")) {
        char start_token[48] = {0};
        char span_token[48] = {0};
        const char *slash = strchr(rval, '/');
        if (slash) {
            copy_slice(rval, (size_t)(slash - rval), start_token, sizeof(start_token));
            copy_slice(slash + 1, strlen(slash + 1), span_token, sizeof(span_token));
        } else {
            copy_slice(rval, strlen(rval), start_token, sizeof(start_token));
        }
        char *start = start_token;
        while (c_isspace(*start)) start++;
        const char *end = NULL;
        int line = parse_int_prefix(start, &end);
        if (end != start && line >= 1 && line <= HTML_CSS_MAX_GRID_COLS) {
            st->grid_col_start = (uint8_t)line;
        } else if (ci_eq(start, "auto")) {
            st->grid_col_start = 0;
        }
        char *span = span_token;
        while (c_isspace(*span)) span++;
        if (ci_prefix(span, "span")) {
            span += 4;
            while (c_isspace(*span)) span++;
            int count = parse_int_prefix(span, &end);
            if (end != span && count >= 1 && count <= HTML_CSS_MAX_GRID_COLS)
                st->grid_col_span = (uint8_t)count;
        }
    } else if (ci_eq(prop, "gap") || ci_eq(prop, "grid-gap")) {
        if (ci_eq(prop, "grid-gap")) css_trace_grid("gap-observed", prop, val);
        char gap_tokens[2][96];
        int count = split_ws_tokens(val, gap_tokens, 2);
        int16_t row = HTML_CSS_UNSET, column = HTML_CSS_UNSET;
        if (count > 0 && parse_css_length(ss, gap_tokens[0], &row, NULL, NULL) &&
            row != HTML_CSS_UNSET) {
            row = clamp_i16(row, 0, 48);
            column = row;
            if (count > 1) {
                int16_t second = HTML_CSS_UNSET;
                if (parse_css_length(ss, gap_tokens[1], &second, NULL, NULL) &&
                    second != HTML_CSS_UNSET)
                    column = clamp_i16(second, 0, 48);
            }
            st->gap = row;
            st->row_gap = row;
            st->column_gap = column;
        }
    } else if (ci_eq(prop, "column-gap") || ci_eq(prop, "row-gap")) {
        int16_t px = HTML_CSS_UNSET;
        if (parse_css_length(ss, val, &px, NULL, NULL) && px != HTML_CSS_UNSET) {
            px = clamp_i16(px, 0, 48);
            if (ci_eq(prop, "row-gap")) st->row_gap = px;
            else st->column_gap = px;
        }
    } else if (ci_eq(prop, "float")) {
        if (ci_eq(rval, "left") || ci_eq(rval, "inline-start")) st->float_mode = CSS_FLOAT_LEFT;
        else if (ci_eq(rval, "right") || ci_eq(rval, "inline-end")) st->float_mode = CSS_FLOAT_RIGHT;
        else if (ci_eq(rval, "none")) st->float_mode = CSS_FLOAT_NONE;
    } else if (ci_eq(prop, "clear")) {
        if (!ci_eq(rval, "none")) st->clear_both = true;
    } else if (ci_eq(prop, "position")) {
        if (ci_eq(rval, "absolute")) {
            st->position_mode = CSS_POS_ABSOLUTE;
            if (sr) sr->seen_pos_abs = true;
        } else if (ci_eq(rval, "fixed")) {
            st->position_mode = CSS_POS_FIXED;
            if (sr) sr->seen_pos_abs = true;
        } else if (ci_eq(rval, "relative")) {
            st->position_mode = CSS_POS_RELATIVE;
        } else if (ci_eq(rval, "static")) {
            st->position_mode = CSS_POS_STATIC;
            if (sr) sr->seen_pos_abs = false;
        }
    } else if (ci_eq(prop, "overflow")) {
        if (sr && ci_eq(rval, "hidden")) sr->seen_overflow_hidden = true;
        else if (sr && !ci_eq(rval, "hidden")) sr->seen_overflow_hidden = false;
    } else if (ci_eq(prop, "clip")) {
        if (ci_contains(rval, "rect(") && (ci_contains(rval, "0") || ci_contains(rval, "1px"))) {
            st->is_sr_only = true;
        } else if (ci_eq(rval, "auto")) {
            st->is_sr_only = false;
        }
    } else if (ci_eq(prop, "clip-path")) {
        if (ci_contains(rval, "inset(50%)") || ci_contains(rval, "inset(100%)")) {
            st->is_sr_only = true;
        } else if (ci_eq(rval, "none")) {
            st->is_sr_only = false;
        }
    } else if ((ci_eq(prop, "left") || ci_eq(prop, "top")) &&
               (st->position_mode == CSS_POS_ABSOLUTE || st->position_mode == CSS_POS_FIXED)) {
        int v = parse_int_prefix(rval, NULL);
        if (v <= -500) st->offscreen_hidden = true;
    } else {
        css_log_drop("unsupported-property-or-context", prop, val);
    }
}

/* Iterates declarations in block; pass important_pass=false for normal, true for !important. */
static void apply_decl_block(const html_stylesheet_t *ss, const char *block, size_t len,
                             css_style_t *st, sr_track_t *sr, bool important_pass,
                             bool custom_only) {
    size_t i = 0;
    while (i < len) {
        int paren = 0;
        char quote = 0;
        size_t dstart = i;
        while (i < len) {
            char c = block[i];
            if (quote) {
                if (c == quote && block[i - 1] != '\\') quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '/' && i + 1 < len && block[i + 1] == '*') {
                i += 2;
                while (i + 1 < len && !(block[i] == '*' && block[i + 1] == '/')) i++;
                if (i + 1 < len) i += 2;
                continue;
            } else if (c == '(') {
                paren++;
            } else if (c == ')' && paren > 0) {
                paren--;
            } else if (c == ';' && paren == 0) {
                break;
            }
            i++;
        }

        size_t dlen = i - dstart;
        if (i < len && block[i] == ';') i++;
        const char *ds = block + dstart;
        trim_slice(ds, dlen, &ds, &dlen);
        if (dlen < 3) continue;

        /* Strip inline comments around prop/val simply */
        const char *colon = NULL;
        for (size_t k = 0; k < dlen; k++) {
            if (ds[k] == ':') {
                colon = ds + k;
                break;
            }
        }
        if (!colon) continue;

        const char *ps = ds;
        size_t plen = (size_t)(colon - ds);
        trim_slice(ps, plen, &ps, &plen);
        const char *vs = colon + 1;
        size_t vlen = (size_t)((ds + dlen) - vs);
        trim_slice(vs, vlen, &vs, &vlen);
        if (plen == 0 || vlen == 0) continue;

        bool is_imp = false;
        if (vlen >= 10 && ci_contains(vs, "!important")) {
            const char *imp_pos = vs + vlen - 10;
            if (ci_eq(imp_pos, "!important")) {
                is_imp = true;
                vlen -= 10;
                trim_slice(vs, vlen, &vs, &vlen);
            } else {
                for (size_t k = 0; k + 10 <= vlen; k++) {
                    if (ci_prefix(vs + k, "!important")) {
                        is_imp = true;
                        vlen = k;
                        trim_slice(vs, vlen, &vs, &vlen);
                        break;
                    }
                }
            }
        }

        if (is_imp != important_pass) continue;

        char prop[64], val[256];
        bool is_custom = plen >= 2 && ps[0] == '-' && ps[1] == '-';
        if (is_custom != custom_only) continue;
        if (is_custom) copy_slice(ps, plen, prop, sizeof(prop));
        else copy_lower_slice(ps, plen, prop, sizeof(prop));
        copy_slice(vs, vlen, val, sizeof(val));
        if (is_custom) {
            set_scoped_var(st, prop, val);
        } else if (prop[0] && val[0]) {
            apply_single_decl(ss, prop, val, st, sr);
        }
    }
}

/* Extracts --var custom properties from a root/html/body rule block into ss->vars. */
static void collect_vars_from_block(html_stylesheet_t *ss, const char *block, size_t len) {
    size_t i = 0;
    while (i < len) {
        size_t dstart = i;
        int paren = 0;
        while (i < len && (paren > 0 || block[i] != ';')) {
            if (block[i] == '(') paren++;
            else if (block[i] == ')' && paren > 0) paren--;
            i++;
        }
        size_t dlen = i - dstart;
        if (i < len) i++;

        const char *ds = block + dstart;
        trim_slice(ds, dlen, &ds, &dlen);
        if (dlen < 4 || ds[0] != '-' || ds[1] != '-') continue;

        const char *colon = NULL;
        for (size_t k = 2; k < dlen; k++) {
            if (ds[k] == ':') { colon = ds + k; break; }
        }
        if (!colon) continue;

        const char *ns = ds;
        size_t nlen = (size_t)(colon - ds);
        trim_slice(ns, nlen, &ns, &nlen);
        const char *vs = colon + 1;
        size_t vlen = (size_t)((ds + dlen) - vs);
        trim_slice(vs, vlen, &vs, &vlen);
        if (nlen >= 3 && nlen < 48 && vlen > 0 && vlen < 96) {
            char name[48], val[96];
            copy_slice(ns, nlen, name, sizeof(name));
            copy_slice(vs, vlen, val, sizeof(val));
            set_var(ss, name, val);
        }
    }
}

/* ---- Selector parser ---- */

static bool css_parse_small_signed(const char *s, int *out) {
    if (!s || !*s) return false;
    int sign = 1;
    if (*s == '+') s++;
    else if (*s == '-') { sign = -1; s++; }
    if (!*s) return false;
    int value = 0;
    for (; *s; ++s) {
        if (*s < '0' || *s > '9') return false;
        int digit = *s - '0';
        if (value > (32768 - digit) / 10) return false;
        value = value * 10 + digit;
    }
    value *= sign;
    if (value < -32768 || value > 32767) return false;
    *out = value;
    return true;
}

static bool css_parse_nth_formula(const char *pseudo, const char *function,
                                  int16_t *a_out, int16_t *b_out) {
    size_t length = strlen(pseudo);
    size_t function_len = strlen(function);
    if (length < function_len + 2 || !ci_prefix(pseudo, function) ||
        pseudo[length - 1] != ')') return false;
    char expr[40];
    size_t out = 0;
    for (size_t i = function_len; i + 1 < length; ++i) {
        if (c_isspace(pseudo[i])) continue;
        if (out + 1 >= sizeof(expr)) return false;
        expr[out++] = pseudo[i];
    }
    expr[out] = '\0';
    if (strcmp(expr, "odd") == 0) {
        *a_out = 2; *b_out = 1; return true;
    }
    if (strcmp(expr, "even") == 0) {
        *a_out = 2; *b_out = 0; return true;
    }
    char *n = strchr(expr, 'n');
    int a = 0, b = 0;
    if (!n) {
        if (!css_parse_small_signed(expr, &b)) return false;
    } else {
        char saved = *n;
        *n = '\0';
        if (expr[0] == '\0' || strcmp(expr, "+") == 0) a = 1;
        else if (strcmp(expr, "-") == 0) a = -1;
        else if (!css_parse_small_signed(expr, &a)) return false;
        *n = saved;
        if (n[1] && !css_parse_small_signed(n + 1, &b)) return false;
    }
    *a_out = (int16_t)a;
    *b_out = (int16_t)b;
    return true;
}

static bool css_parse_nth_child(const char *pseudo, int16_t *a_out,
                                int16_t *b_out) {
    return css_parse_nth_formula(pseudo, "nth-child(", a_out, b_out);
}

static bool css_parse_nth_type(const char *pseudo, int16_t *a_out,
                               int16_t *b_out) {
    return css_parse_nth_formula(pseudo, "nth-of-type(", a_out, b_out);
}

static bool css_parse_nth_last_child(const char *pseudo, int16_t *a_out,
                                     int16_t *b_out) {
    return css_parse_nth_formula(pseudo, "nth-last-child(", a_out, b_out);
}

static bool css_parse_nth_last_type(const char *pseudo, int16_t *a_out,
                                    int16_t *b_out) {
    return css_parse_nth_formula(pseudo, "nth-last-of-type(", a_out, b_out);
}

static bool css_attr_name_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_' || c == '-' || c == ':';
}

static bool css_decode_attr_value(const char *input, size_t input_len,
                                  char *output, size_t output_cap,
                                  size_t *output_len) {
    size_t in = 0, out = 0;
    while (in < input_len) {
        uint32_t codepoint;
        if (input[in] != '\\') {
            codepoint = (uint8_t)input[in++];
        } else {
            ++in;
            if (in >= input_len || input[in] == '\n' || input[in] == '\r' ||
                input[in] == '\f') return false;
            int digit = hex_val(input[in]);
            if (digit >= 0) {
                codepoint = (uint32_t)digit;
                ++in;
                unsigned digits = 1;
                while (in < input_len && digits < 6 &&
                       (digit = hex_val(input[in])) >= 0) {
                    codepoint = (codepoint << 4) | (uint32_t)digit;
                    ++in;
                    ++digits;
                }
                if (in < input_len && c_isspace(input[in])) {
                    if (input[in] == '\r' && in + 1 < input_len &&
                        input[in + 1] == '\n') ++in;
                    ++in;
                }
                if (codepoint == 0 || codepoint > 0x7f) return false;
            } else {
                codepoint = (uint8_t)input[in++];
            }
        }
        if (out + 1 >= output_cap) return false;
        output[out++] = (char)codepoint;
    }
    output[out] = '\0';
    *output_len = out;
    return out != 0;
}

static bool css_parse_attribute(html_stylesheet_t *ss,
                                const char *s, size_t len,
                                css_attr_selector_t *out) {
    size_t i = 0;
    while (i < len && c_isspace(s[i])) ++i;
    size_t name_start = i;
    while (i < len && css_attr_name_char(s[i])) ++i;
    if (i == name_start) return false;
    out->name_hash = css_hash_slice(s + name_start, i - name_start, true);
    css_register_attribute_name(ss, out->name_hash);
    while (i < len && c_isspace(s[i])) ++i;
    if (i == len) {
        out->flags = 0;
        out->value_hash = 0;
        return true;
    }
    uint8_t operator_kind = 0;
    char operator_char = s[i++];
    if (operator_char != '=') {
        if (i >= len || s[i++] != '=') return false;
        switch (operator_char) {
            case '~': operator_kind = 1; break; /* whitespace token */
            case '|': operator_kind = 2; break; /* language prefix */
            case '^': operator_kind = 3; break; /* prefix */
            case '$': operator_kind = 4; break; /* suffix */
            case '*': operator_kind = 5; break; /* substring */
            default: return false;
        }
    }
    while (i < len && c_isspace(s[i])) ++i;
    size_t value_start = i, value_len = 0;
    if (i < len && (s[i] == '\'' || s[i] == '"')) {
        char quote = s[i++];
        value_start = i;
        while (i < len && s[i] != quote) {
            if (s[i] == '\\' && i + 1 < len) i += 2;
            else ++i;
        }
        if (i >= len) return false;
        value_len = i - value_start;
        ++i;
    } else {
        value_start = i;
        while (i < len && !c_isspace(s[i])) ++i;
        value_len = i - value_start;
    }
    if (value_len == 0) return false;
    while (i < len && c_isspace(s[i])) ++i;
    bool insensitive = false;
    if (i < len && (s[i] == 'i' || s[i] == 'I' || s[i] == 's' || s[i] == 'S')) {
        insensitive = s[i] == 'i' || s[i] == 'I';
        ++i;
        while (i < len && c_isspace(s[i])) ++i;
    }
    if (i != len) return false;
    char decoded_value[256];
    size_t decoded_len = 0;
    if (!css_decode_attr_value(s + value_start, value_len, decoded_value,
                               sizeof(decoded_value), &decoded_len)) return false;
    out->flags = 1U | (insensitive ? 2U : 0U) | (uint8_t)(operator_kind << 2);
    out->value_hash = css_hash_slice(decoded_value, decoded_len, insensitive);
    if (operator_kind != 0) {
        if (!selector_arena_append(ss, decoded_value, decoded_len,
                                   &out->value_off)) return false;
        out->value_len = (uint16_t)decoded_len;
    }
    return true;
}

static bool parse_simple_part(html_stylesheet_t *ss,
                              const char *s, size_t len, css_simple_sel_t *out,
                              uint16_t *spec);

static bool merge_simple_selector(css_simple_sel_t *out,
                                  const css_simple_sel_t *inner) {
    if (inner->tag[0] && strcmp(inner->tag, "*") != 0) {
        if (out->tag[0] && strcmp(out->tag, "*") != 0 &&
            strcmp(out->tag, inner->tag) != 0) return false;
        strcpy(out->tag, inner->tag);
    }
    if (inner->has_id) {
        if (out->has_id && !css_token_hash_equal(out->id, inner->id)) return false;
        out->id = inner->id;
        out->has_id = true;
    }
    for (uint8_t i = 0; i < inner->class_count; ++i) {
        if (out->class_count >= HTML_CSS_MAX_SEL_CLS) return false;
        out->classes[out->class_count++] = inner->classes[i];
    }
    for (uint8_t i = 0; i < inner->not_class_count; ++i) {
        if (out->not_class_count >= 2) return false;
        out->not_classes[out->not_class_count++] = inner->not_classes[i];
    }
    for (uint8_t i = 0; i < inner->attr_count; ++i) {
        if (out->attr_count >= HTML_CSS_MAX_SEL_ATTRS) return false;
        out->attrs[out->attr_count++] = inner->attrs[i];
    }
    for (uint8_t i = 0; i < inner->forbidden_attr_count; ++i) {
        if (out->forbidden_attr_count >= HTML_CSS_MAX_NOT_ATTRS) return false;
        out->forbidden_attrs[out->forbidden_attr_count++] = inner->forbidden_attrs[i];
    }
    if (inner->has_nth_child) {
        if (out->has_nth_child &&
            (out->nth_child_a != inner->nth_child_a ||
             out->nth_child_b != inner->nth_child_b ||
             out->nth_child_reverse != inner->nth_child_reverse)) return false;
        out->has_nth_child = true;
        out->nth_child_a = inner->nth_child_a;
        out->nth_child_b = inner->nth_child_b;
        out->nth_child_reverse = inner->nth_child_reverse;
    }
    if (inner->has_nth_type) {
        if (out->has_nth_type &&
            (out->nth_type_a != inner->nth_type_a ||
             out->nth_type_b != inner->nth_type_b ||
             out->nth_type_reverse != inner->nth_type_reverse)) return false;
        out->has_nth_type = true;
        out->nth_type_a = inner->nth_type_a;
        out->nth_type_b = inner->nth_type_b;
        out->nth_type_reverse = inner->nth_type_reverse;
    }
    if (inner->child_pseudo) {
        if (out->child_pseudo && out->child_pseudo != inner->child_pseudo) return false;
        out->child_pseudo = inner->child_pseudo;
    }
    if (inner->type_pseudo) {
        if (out->type_pseudo && out->type_pseudo != inner->type_pseudo) return false;
        out->type_pseudo = inner->type_pseudo;
    }
    if (inner->forbid_child_pseudo) {
        if (out->forbid_child_pseudo &&
            out->forbid_child_pseudo != inner->forbid_child_pseudo) return false;
        out->forbid_child_pseudo = inner->forbid_child_pseudo;
    }
    if (inner->has_forbid_nth_child) {
        if (out->has_forbid_nth_child &&
            (out->forbid_nth_child_a != inner->forbid_nth_child_a ||
             out->forbid_nth_child_b != inner->forbid_nth_child_b ||
             out->forbid_nth_child_reverse != inner->forbid_nth_child_reverse)) return false;
        out->has_forbid_nth_child = true;
        out->forbid_nth_child_a = inner->forbid_nth_child_a;
        out->forbid_nth_child_b = inner->forbid_nth_child_b;
        out->forbid_nth_child_reverse = inner->forbid_nth_child_reverse;
    }
    if (inner->has_forbid_nth_type) {
        if (out->has_forbid_nth_type &&
            (out->forbid_nth_type_a != inner->forbid_nth_type_a ||
             out->forbid_nth_type_b != inner->forbid_nth_type_b ||
             out->forbid_nth_type_reverse != inner->forbid_nth_type_reverse)) return false;
        out->has_forbid_nth_type = true;
        out->forbid_nth_type_a = inner->forbid_nth_type_a;
        out->forbid_nth_type_b = inner->forbid_nth_type_b;
        out->forbid_nth_type_reverse = inner->forbid_nth_type_reverse;
    }
    if (inner->forbid_type_pseudo) {
        if (out->forbid_type_pseudo &&
            out->forbid_type_pseudo != inner->forbid_type_pseudo) return false;
        out->forbid_type_pseudo = inner->forbid_type_pseudo;
    }
    out->require_state |= inner->require_state;
    out->forbid_state |= inner->forbid_state;
    out->require_hidden_attr |= inner->require_hidden_attr;
    out->forbid_hidden_attr |= inner->forbid_hidden_attr;
    out->require_empty |= inner->require_empty;
    out->forbid_empty |= inner->forbid_empty;
    return true;
}

static uint16_t css_pseudo_state(const char *name) {
    if (ci_eq(name, "hover")) return CSS_PSEUDO_HOVER;
    if (ci_eq(name, "focus")) return CSS_PSEUDO_FOCUS;
    if (ci_eq(name, "focus-visible")) return CSS_PSEUDO_FOCUS_VISIBLE;
    if (ci_eq(name, "focus-within")) return CSS_PSEUDO_FOCUS_WITHIN;
    if (ci_eq(name, "active")) return CSS_PSEUDO_ACTIVE;
    if (ci_eq(name, "checked")) return CSS_PSEUDO_CHECKED;
    if (ci_eq(name, "disabled")) return CSS_PSEUDO_DISABLED;
    if (ci_eq(name, "enabled")) return CSS_PSEUDO_ENABLED;
    return 0;
}

static bool parse_simple_part(html_stylesheet_t *ss, const char *s, size_t len,
                              css_simple_sel_t *out, uint16_t *spec) {
    memset(out, 0, sizeof(*out));
    size_t i = 0;
    bool anything = false;

    while (i < len) {
        char c = s[i];
        if (c == '.') {
            i++;
            size_t st = i;
            while (i < len && s[i] != '.' && s[i] != '#' && s[i] != ':' && s[i] != '[') i++;
            if (i == st) return false;
            if (out->class_count >= HTML_CSS_MAX_SEL_CLS) return false;
            out->classes[out->class_count++] = css_hash_token(s + st, i - st);
            *spec += 10;
            anything = true;
        } else if (c == '#') {
            i++;
            size_t st = i;
            while (i < len && s[i] != '.' && s[i] != '#' && s[i] != ':' && s[i] != '[') i++;
            if (i == st) return false;
            out->id = css_hash_token(s + st, i - st);
            out->has_id = true;
            *spec += 100;
            anything = true;
        } else if (c == '[') {
            size_t st = i + 1;
            int brack = 1;
            i++;
            while (i < len && brack > 0) {
                if (s[i] == '[') brack++;
                else if (s[i] == ']') brack--;
                i++;
            }
            size_t alen = (i > st && s[i - 1] == ']') ? (i - 1 - st) : (i - st);
            if (out->attr_count >= HTML_CSS_MAX_SEL_ATTRS ||
                !css_parse_attribute(ss, s + st, alen,
                                     &out->attrs[out->attr_count])) return false;
            if (alen == 6 && ci_prefix(s + st, "hidden"))
                out->require_hidden_attr = true;
            out->attr_count++;
            *spec += 10;
            anything = true;
        } else if (c == ':') {
            i++;
            if (i < len && s[i] == ':') return false; /* ::before / ::after etc. */
            size_t st = i;
            int paren = 0;
            while (i < len && (paren > 0 || (s[i] != '.' && s[i] != '#' && s[i] != ':' && s[i] != '['))) {
                if (s[i] == '(') paren++;
                else if (s[i] == ')' && paren > 0) paren--;
                i++;
            }
            char pbuf[256];
            copy_slice(s + st, i - st, pbuf, sizeof(pbuf));
            if (css_parse_nth_child(pbuf, &out->nth_child_a,
                                    &out->nth_child_b)) {
                out->has_nth_child = true;
                *spec += 10;
                anything = true;
            } else if (css_parse_nth_type(pbuf, &out->nth_type_a,
                                          &out->nth_type_b)) {
                out->has_nth_type = true;
                *spec += 10;
                anything = true;
            } else if (css_parse_nth_last_child(pbuf, &out->nth_child_a,
                                                &out->nth_child_b)) {
                out->has_nth_child = true;
                out->nth_child_reverse = true;
                *spec += 10;
                anything = true;
            } else if (css_parse_nth_last_type(pbuf, &out->nth_type_a,
                                               &out->nth_type_b)) {
                out->has_nth_type = true;
                out->nth_type_reverse = true;
                *spec += 10;
                anything = true;
            } else if ((ci_prefix(pbuf, "where(") || ci_prefix(pbuf, "is(")) &&
                       pbuf[strlen(pbuf) - 1] == ')') {
                bool zero_specificity = ci_prefix(pbuf, "where(");
                size_t prefix_len = zero_specificity ? 6 : 3;
                size_t inner_len = strlen(pbuf) - prefix_len - 1;
                char inner_text[240];
                if (inner_len == 0 || inner_len >= sizeof(inner_text)) return false;
                memcpy(inner_text, pbuf + prefix_len, inner_len);
                inner_text[inner_len] = '\0';
                if (strchr(inner_text, ',') || strchr(inner_text, '>') ||
                    strchr(inner_text, '+') || strchr(inner_text, '~')) return false;
                css_simple_sel_t inner;
                uint16_t before_spec = *spec;
                if (!parse_simple_part(ss, inner_text, inner_len, &inner, spec) ||
                    !merge_simple_selector(out, &inner)) return false;
                if (zero_specificity) *spec = before_spec;
                anything = true;
            } else if (ci_eq(pbuf, "root")) {
                copy_slice("html", 4, out->tag, sizeof(out->tag));
                *spec += 10;
                anything = true;
            } else if (ci_eq(pbuf, "first-child")) {
                out->child_pseudo = 1;
                *spec += 10;
                anything = true;
            } else if (ci_eq(pbuf, "last-child")) {
                out->child_pseudo = 2;
                *spec += 10;
                anything = true;
            } else if (ci_eq(pbuf, "only-child")) {
                out->child_pseudo = 3;
                *spec += 10;
                anything = true;
            } else if (ci_eq(pbuf, "first-of-type")) {
                out->type_pseudo = 1;
                *spec += 10;
                anything = true;
            } else if (ci_eq(pbuf, "last-of-type")) {
                out->type_pseudo = 2;
                *spec += 10;
                anything = true;
            } else if (ci_eq(pbuf, "only-of-type")) {
                out->type_pseudo = 3;
                *spec += 10;
                anything = true;
            } else if (ci_eq(pbuf, "empty")) {
                out->require_empty = true;
                *spec += 10;
                anything = true;
            } else if (css_pseudo_state(pbuf)) {
                out->require_state |= css_pseudo_state(pbuf);
                *spec += 10;
                anything = true;
            } else if (ci_eq(pbuf, "link") || ci_eq(pbuf, "any-link")) {
                *spec += 10;
                anything = true;
            } else if (ci_prefix(pbuf, "not(") && strlen(pbuf) >= 6 && pbuf[strlen(pbuf) - 1] == ')') {
                char inner[256];
                size_t ilen = strlen(pbuf) - 5;
                if (ilen >= sizeof(inner)) ilen = sizeof(inner) - 1;
                memcpy(inner, pbuf + 4, ilen);
                inner[ilen] = '\0';
                if (inner[0] == ':' && css_pseudo_state(inner + 1)) {
                    out->forbid_state |= css_pseudo_state(inner + 1);
                    *spec += 10;
                    anything = true;
                } else if (ci_eq(inner, ":first-child")) {
                    out->forbid_child_pseudo = 1;
                    *spec += 10;
                    anything = true;
                } else if (ci_eq(inner, ":last-child")) {
                    out->forbid_child_pseudo = 2;
                    *spec += 10;
                    anything = true;
                } else if (ci_eq(inner, ":only-child")) {
                    out->forbid_child_pseudo = 3;
                    *spec += 10;
                    anything = true;
                } else if (ci_eq(inner, ":first-of-type")) {
                    out->forbid_type_pseudo = 1;
                    *spec += 10;
                    anything = true;
                } else if (ci_eq(inner, ":last-of-type")) {
                    out->forbid_type_pseudo = 2;
                    *spec += 10;
                    anything = true;
                } else if (ci_eq(inner, ":only-of-type")) {
                    out->forbid_type_pseudo = 3;
                    *spec += 10;
                    anything = true;
                } else if (ci_eq(inner, ":empty")) {
                    out->forbid_empty = true;
                    *spec += 10;
                    anything = true;
                } else if (inner[0] == ':' && ci_prefix(inner + 1, "nth-child(") &&
                           ilen > 12 && inner[ilen - 1] == ')') {
                    if (!css_parse_nth_child(inner + 1,
                                             &out->forbid_nth_child_a,
                                             &out->forbid_nth_child_b)) return false;
                    out->has_forbid_nth_child = true;
                    *spec += 10;
                    anything = true;
                } else if (inner[0] == ':' && ci_prefix(inner + 1, "nth-of-type(") &&
                           ilen > 14 && inner[ilen - 1] == ')') {
                    if (!css_parse_nth_type(inner + 1,
                                            &out->forbid_nth_type_a,
                                            &out->forbid_nth_type_b)) return false;
                    out->has_forbid_nth_type = true;
                    *spec += 10;
                    anything = true;
                } else if (inner[0] == ':' && ci_prefix(inner + 1, "nth-last-child(") &&
                           ilen > 16 && inner[ilen - 1] == ')') {
                    if (!css_parse_nth_last_child(inner + 1,
                                                  &out->forbid_nth_child_a,
                                                  &out->forbid_nth_child_b)) return false;
                    out->has_forbid_nth_child = true;
                    out->forbid_nth_child_reverse = true;
                    *spec += 10;
                    anything = true;
                } else if (inner[0] == ':' && ci_prefix(inner + 1, "nth-last-of-type(") &&
                           ilen > 18 && inner[ilen - 1] == ')') {
                    if (!css_parse_nth_last_type(inner + 1,
                                                 &out->forbid_nth_type_a,
                                                 &out->forbid_nth_type_b)) return false;
                    out->has_forbid_nth_type = true;
                    out->forbid_nth_type_reverse = true;
                    *spec += 10;
                    anything = true;
                } else if (inner[0] == '.' && ilen > 1 && out->not_class_count < 2) {
                    out->not_classes[out->not_class_count] =
                        css_hash_token(inner + 1, ilen - 1);
                    out->not_class_count++;
                    *spec += 10;
                    anything = true;
                } else if (inner[0] == '[' && ilen > 2 && inner[ilen - 1] == ']' &&
                           out->forbidden_attr_count < HTML_CSS_MAX_NOT_ATTRS &&
                           css_parse_attribute(ss, inner + 1, ilen - 2,
                               &out->forbidden_attrs[out->forbidden_attr_count])) {
                    if (ci_eq(inner, "[hidden]")) out->forbid_hidden_attr = true;
                    out->forbidden_attr_count++;
                    *spec += 10;
                    anything = true;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        } else if (c == '*') {
            i++;
            if (!out->tag[0]) strcpy(out->tag, "*");
            anything = true;
        } else {
            size_t st = i;
            while (i < len && s[i] != '.' && s[i] != '#' && s[i] != ':' && s[i] != '[') i++;
            copy_lower_slice(s + st, i - st, out->tag, sizeof(out->tag));
            *spec += 1;
            anything = true;
        }
    }
    return anything;
}

static bool parse_selector_chain(html_stylesheet_t *ss, const char *sel,
                                 size_t len, css_rule_t *rule) {
    trim_slice(sel, len, &sel, &len);
    if (len == 0) return false;

    rule->part_count = 0;
    rule->specificity = 0;
    css_simple_sel_t parts[HTML_CSS_MAX_SEL_CHAIN];
    memset(parts, 0, sizeof(parts));

    size_t i = 0;
    uint8_t pending_comb = 0;

    while (i < len) {
        while (i < len && c_isspace(sel[i])) {
            if (rule->part_count > 0 && pending_comb == 0) pending_comb = 1;
            i++;
        }
        if (i >= len) break;

        if (sel[i] == '>') {
            if (rule->part_count == 0 || pending_comb == 2 ||
                pending_comb == 3 || pending_comb == 4) return false;
            pending_comb = 2;
            i++;
            continue;
        }
        if (sel[i] == '+' || sel[i] == '~') {
            if (rule->part_count == 0 || pending_comb == 2 ||
                pending_comb == 3 || pending_comb == 4) return false;
            pending_comb = sel[i] == '+' ? 3 : 4;
            i++;
            continue;
        }

        size_t pstart = i;
        int paren = 0;
        int brack = 0;
        while (i < len) {
            char c = sel[i];
            if (c == '(') paren++;
            else if (c == ')' && paren > 0) paren--;
            else if (c == '[') brack++;
            else if (c == ']' && brack > 0) brack--;
            else if (paren == 0 && brack == 0 && (c_isspace(c) || c == '>' || c == '+' || c == '~')) {
                break;
            }
            i++;
        }

        if (rule->part_count >= HTML_CSS_MAX_SEL_CHAIN) return false;
        css_simple_sel_t *part = &parts[rule->part_count];
        if (!parse_simple_part(ss, sel + pstart, i - pstart, part,
                               &rule->specificity)) {
            return false;
        }
        part->combinator = (rule->part_count == 0) ? 0 : (pending_comb ? pending_comb : 1);
        rule->part_count++;
        pending_comb = 0;
    }

    if (rule->part_count == 0) return false;

    size_t needed = ss->selector_part_count + rule->part_count;
    if (needed > ss->selector_part_capacity) {
        size_t cap = ss->selector_part_capacity ? ss->selector_part_capacity * 2 : 256;
        while (cap < needed) cap *= 2;
        if (!html_css_reserve_selector_parts(ss, cap)) return false;
    }
    rule->parts_off = (uint32_t)ss->selector_part_count;
    memcpy(ss->selector_parts + ss->selector_part_count, parts,
           rule->part_count * sizeof(css_simple_sel_t));
    ss->selector_part_count = needed;
    return true;
}

static void index_css_rule(html_stylesheet_t *ss, css_rule_t *rule) {
    if (!ss || !rule || rule->part_count == 0) return;
    css_simple_sel_t *right = &CSS_RULE_PARTS(ss, rule)[rule->part_count - 1];
    uint64_t hash = 0;
    uint8_t kind = 0;
    if (right->has_id) {
        hash = css_index_token_hash(right->id);
        kind = 1;
    } else if (right->class_count > 0) {
        hash = css_index_token_hash(right->classes[0]);
        kind = 2;
    } else if (right->tag[0] && strcmp(right->tag, "*") != 0) {
        hash = css_hash_slice(right->tag, strlen(right->tag), true);
        kind = 3;
    }

    uint32_t encoded = (uint32_t)ss->rule_count + 1U;
    rule->index_hash = hash;
    rule->index_kind = kind;
    if (kind == 0) {
        rule->index_next = ss->generic_head;
        ss->generic_head = encoded;
        return;
    }

    uint32_t *head = &ss->index_heads[kind - 1U][css_index_bucket(hash)];
    rule->index_next = *head;
    *head = encoded;
}

static bool is_root_only_rule(const html_stylesheet_t *ss, const css_rule_t *r) {
    if (r->part_count != 1) return false;
    const css_simple_sel_t *p = &CSS_RULE_PARTS(ss, r)[0];
    if (p->has_id || p->class_count > 0 || p->require_hidden_attr) return false;
    return ci_eq(p->tag, "html") || ci_eq(p->tag, "body") || ci_eq(p->tag, "*");
}

/* ---- At-rule (@media) condition evaluator ---- */

static bool media_length_px(const html_stylesheet_t *ss, const char *value,
                            int *out_px) {
    char token[64];
    while (*value && c_isspace(*value)) value++;
    if (ci_prefix(value, "calc(")) {
        const char *end = strrchr(value, ')');
        if (!end) return false;
        const char *inner = value + 5;
        while (inner < end && c_isspace(*inner)) inner++;
        const char *op = NULL;
        for (const char *q = inner; q < end; ++q) {
            if ((*q == '+' || *q == '-') && q > inner &&
                c_isspace(q[-1]) && q + 1 < end && c_isspace(q[1])) {
                op = q;
                break;
            }
        }
        if (!op) return false;
        const char *left_end = op;
        while (left_end > inner && c_isspace(left_end[-1])) left_end--;
        const char *right = op + 1;
        while (right < end && c_isspace(*right)) right++;
        char left_token[32], right_token[32];
        size_t left_len = (size_t)(left_end - inner);
        size_t right_len = (size_t)(end - right);
        if (!left_len || !right_len || left_len >= sizeof(left_token) ||
            right_len >= sizeof(right_token)) return false;
        memcpy(left_token, inner, left_len); left_token[left_len] = '\0';
        memcpy(right_token, right, right_len); right_token[right_len] = '\0';
        int16_t left_px = HTML_CSS_UNSET, left_pct = HTML_CSS_UNSET;
        int16_t right_px = HTML_CSS_UNSET, right_pct = HTML_CSS_UNSET;
        if (!parse_css_length(ss, left_token, &left_px, &left_pct, NULL) ||
            !parse_css_length(ss, right_token, &right_px, &right_pct, NULL) ||
            left_px == HTML_CSS_UNSET || right_px == HTML_CSS_UNSET ||
            left_pct != HTML_CSS_UNSET || right_pct != HTML_CSS_UNSET)
            return false;
        int64_t result = (int64_t)left_px + (*op == '+' ? right_px : -right_px);
        if (result < 0 || result > INT_MAX) return false;
        *out_px = (int)result;
        return true;
    }
    size_t n = 0;
    while (value[n] && !c_isspace(value[n]) && value[n] != ')' &&
           value[n] != ',') {
        if (n + 1 >= sizeof(token)) return false;
        token[n] = value[n];
        n++;
    }
    if (n == 0) return false;
    token[n] = '\0';
    int16_t px = HTML_CSS_UNSET;
    int16_t pct = HTML_CSS_UNSET;
    if (!parse_css_length(ss, token, &px, &pct, NULL) ||
        px == HTML_CSS_UNSET || pct != HTML_CSS_UNSET || px < 0)
        return false;
    *out_px = px;
    return true;
}

static bool eval_media_cond(const html_stylesheet_t *ss,
                            const char *cond, size_t len, int viewport_w,
                            int viewport_h) {
    char buf[256];
    copy_lower_slice(cond, len, buf, sizeof(buf));

    if (ci_contains(buf, "print") && !ci_contains(buf, "screen")) return false;
    bool wants_dark = ci_contains(buf, "prefers-color-scheme: dark") ||
                      ci_contains(buf, "prefers-color-scheme:dark");
    bool wants_light = ci_contains(buf, "prefers-color-scheme: light") ||
                       ci_contains(buf, "prefers-color-scheme:light");
    if ((wants_dark && !ss->prefer_dark_scheme) ||
        (wants_light && ss->prefer_dark_scheme)) return false;

    for (size_t i = 0; buf[i]; i++) {
        if (ci_prefix(buf + i, "min-width:")) {
            int min_px;
            if (!media_length_px(ss, buf + i + 10, &min_px) ||
                viewport_w < min_px) return false;
        } else if (ci_prefix(buf + i, "max-width:")) {
            int max_px;
            if (!media_length_px(ss, buf + i + 10, &max_px) ||
                viewport_w > max_px) return false;
        } else if (ci_prefix(buf + i, "min-height:")) {
            int min_px;
            if (!media_length_px(ss, buf + i + 11, &min_px) ||
                viewport_h < min_px) return false;
        } else if (ci_prefix(buf + i, "max-height:")) {
            int max_px;
            if (!media_length_px(ss, buf + i + 11, &max_px) ||
                viewport_h > max_px) return false;
        } else if (ci_prefix(buf + i, "width") &&
                   (i == 0 || !(c_isalnum(buf[i - 1]) || buf[i - 1] == '-'))) {
            const char *p = buf + i + 5;
            while (*p && c_isspace(*p)) p++;
            enum { MEDIA_CMP_NONE, MEDIA_CMP_GE, MEDIA_CMP_LE,
                   MEDIA_CMP_GT, MEDIA_CMP_LT, MEDIA_CMP_EQ } cmp = MEDIA_CMP_NONE;
            if (p[0] == '>' && p[1] == '=') { cmp = MEDIA_CMP_GE; p += 2; }
            else if (p[0] == '<' && p[1] == '=') { cmp = MEDIA_CMP_LE; p += 2; }
            else if (*p == '>') { cmp = MEDIA_CMP_GT; p++; }
            else if (*p == '<') { cmp = MEDIA_CMP_LT; p++; }
            else if (*p == '=') { cmp = MEDIA_CMP_EQ; p++; }
            else if (*p == ':') { cmp = MEDIA_CMP_EQ; p++; }
            if (cmp != MEDIA_CMP_NONE) {
                int boundary;
                if (media_length_px(ss, p, &boundary)) {
                    if ((cmp == MEDIA_CMP_GE && viewport_w < boundary) ||
                        (cmp == MEDIA_CMP_LE && viewport_w > boundary) ||
                        (cmp == MEDIA_CMP_GT && viewport_w <= boundary) ||
                        (cmp == MEDIA_CMP_LT && viewport_w >= boundary) ||
                        (cmp == MEDIA_CMP_EQ && viewport_w != boundary))
                        return false;
                } else return false;
            }
        } else if (ci_prefix(buf + i, "height") &&
                   (i == 0 || !(c_isalnum(buf[i - 1]) || buf[i - 1] == '-'))) {
            const char *p = buf + i + 6;
            while (*p && c_isspace(*p)) p++;
            enum { MEDIA_H_CMP_NONE, MEDIA_H_CMP_GE, MEDIA_H_CMP_LE,
                   MEDIA_H_CMP_GT, MEDIA_H_CMP_LT, MEDIA_H_CMP_EQ } cmp = MEDIA_H_CMP_NONE;
            if (p[0] == '>' && p[1] == '=') { cmp = MEDIA_H_CMP_GE; p += 2; }
            else if (p[0] == '<' && p[1] == '=') { cmp = MEDIA_H_CMP_LE; p += 2; }
            else if (*p == '>') { cmp = MEDIA_H_CMP_GT; p++; }
            else if (*p == '<') { cmp = MEDIA_H_CMP_LT; p++; }
            else if (*p == '=') { cmp = MEDIA_H_CMP_EQ; p++; }
            else if (*p == ':') { cmp = MEDIA_H_CMP_EQ; p++; }
            if (cmp != MEDIA_H_CMP_NONE) {
                int boundary;
                if (media_length_px(ss, p, &boundary)) {
                    if ((cmp == MEDIA_H_CMP_GE && viewport_h < boundary) ||
                        (cmp == MEDIA_H_CMP_LE && viewport_h > boundary) ||
                        (cmp == MEDIA_H_CMP_GT && viewport_h <= boundary) ||
                        (cmp == MEDIA_H_CMP_LT && viewport_h >= boundary) ||
                        (cmp == MEDIA_H_CMP_EQ && viewport_h != boundary))
                        return false;
                } else return false;
            }
        }
    }
    return true;
}

static size_t find_matching_brace(const char *s, size_t len, size_t open_idx) {
    int depth = 0;
    char quote = 0;
    for (size_t i = open_idx; i < len; i++) {
        char c = s[i];
        if (quote) {
            if (c == quote && i > 0 && s[i - 1] != '\\') quote = 0;
        } else if (c == '"' || c == '\'') {
            quote = c;
        } else if (c == '/' && i + 1 < len && s[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(s[i] == '*' && s[i + 1] == '/')) i++;
            i++;
        } else if (c == '{') {
            depth++;
        } else if (c == '}') {
            depth--;
            if (depth == 0) return i;
        }
    }
    return len;
}

static void parse_css_chunk(html_stylesheet_t *ss, const char *css, size_t len, int depth) {
    if (!ss || !css || len == 0 || depth > 4) return;
    size_t i = 0;

    while (i < len) {
        while (i < len && c_isspace(css[i])) i++;
        if (i >= len) break;

        if (css[i] == '/' && i + 1 < len && css[i + 1] == '*') {
            i += 2;
            while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/')) i++;
            i = (i + 1 < len) ? (i + 2) : len;
            continue;
        }

        if (css[i] == '@') {
            size_t at_start = i;
            while (i < len && css[i] != '{' && css[i] != ';') i++;
            if (i >= len) break;
            if (css[i] == ';') {
                i++;
                continue;
            }
            size_t open_b = i;
            size_t close_b = find_matching_brace(css, len, open_b);
            size_t head_len = open_b - at_start;
            const char *head = css + at_start;
            const char *body = css + open_b + 1;
            size_t body_len = (close_b > open_b + 1) ? (close_b - open_b - 1) : 0;

            if (ci_prefix(head, "@media")) {
                if (eval_media_cond(ss, head + 6, head_len - 6,
                                    ss->viewport_w, ss->viewport_h)) {
                    parse_css_chunk(ss, body, body_len, depth + 1);
                }
            } else if (ci_prefix(head, "@layer") ||
                       (ci_prefix(head, "@supports") && !ci_contains(head, " not "))) {
                parse_css_chunk(ss, body, body_len, depth + 1);
            }
            i = (close_b < len) ? (close_b + 1) : len;
            continue;
        }

        size_t sel_start = i;
        char quote = 0;
        while (i < len) {
            char c = css[i];
            if (quote) {
                if (c == quote && i > 0 && css[i - 1] != '\\') quote = 0;
            } else if (c == '"' || c == '\'') {
                quote = c;
            } else if (c == '/' && i + 1 < len && css[i + 1] == '*') {
                i += 2;
                while (i + 1 < len && !(css[i] == '*' && css[i + 1] == '/')) i++;
                if (i + 1 < len) i++;
            } else if (c == '{') {
                break;
            }
            i++;
        }
        if (i >= len || css[i] != '{') break;

        size_t sel_len = i - sel_start;
        size_t open_b = i;
        size_t close_b = find_matching_brace(css, len, open_b);
        const char *body = css + open_b + 1;
        size_t body_len = (close_b > open_b + 1) ? (close_b - open_b - 1) : 0;
        i = (close_b < len) ? (close_b + 1) : len;

        if (body_len == 0) continue;

        uint32_t decl_off = 0, decl_len = 0;
        bool decl_stored = false;
        bool collected_vars = false;

        /* Split comma-separated selector list safely */
        size_t sp = 0;
        while (sp < sel_len) {
            size_t item_start = sp;
            int paren = 0, brack = 0;
            while (sp < sel_len) {
                char c = css[sel_start + sp];
                if (c == '(') paren++;
                else if (c == ')' && paren > 0) paren--;
                else if (c == '[') brack++;
                else if (c == ']' && brack > 0) brack--;
                else if (c == ',' && paren == 0 && brack == 0) break;
                sp++;
            }
            size_t item_len = sp - item_start;
            if (sp < sel_len && css[sel_start + sp] == ',') sp++;

            if (ss->rule_count >= HTML_CSS_MAX_RULES) {
                ss->rules_truncated = true;
                break;
            }
            if (ss->rule_count == ss->rule_capacity) {
                int capacity = ss->rule_capacity
                    ? ss->rule_capacity * 2
                    : HTML_CSS_INITIAL_RULES;
                if (capacity <= ss->rule_capacity) capacity = ss->rule_capacity + 1;
                if (capacity > HTML_CSS_MAX_RULES)
                    capacity = HTML_CSS_MAX_RULES;
                css_rule_t *grown = (css_rule_t *)krealloc(
                    ss->rules, (size_t)capacity * sizeof(css_rule_t));
                if (!grown) {
                    ss->rules_truncated = true;
                    break;
                }
                memset(grown + ss->rule_capacity, 0,
                       (size_t)(capacity - ss->rule_capacity) * sizeof(css_rule_t));
                ss->rules = grown;
                ss->rule_capacity = capacity;
            }

            css_rule_t *rule = &ss->rules[ss->rule_count];
            if (!parse_selector_chain(ss, css + sel_start + item_start,
                                      item_len, rule)) {
                continue;
            }
            for (uint8_t part_index = 0; part_index < rule->part_count; ++part_index) {
                const css_simple_sel_t *part = &CSS_RULE_PARTS(ss, rule)[part_index];
                if (part->has_nth_type || part->type_pseudo ||
                    part->has_forbid_nth_type || part->forbid_type_pseudo) {
                    ss->needs_type_counts = true;
                    break;
                }
            }

            if (!collected_vars && is_root_only_rule(ss, rule)) {
                collect_vars_from_block(ss, body, body_len);
                collected_vars = true;
            }

            if (!decl_stored) {
                if (!arena_append(ss, body, body_len, &decl_off, &decl_len)) break;
                decl_stored = true;
            }
            rule->order = (uint16_t)ss->rule_count;
            rule->decl_off = decl_off;
            rule->decl_len = decl_len;
            index_css_rule(ss, rule);
            ss->rule_count++;
        }
    }
}

/* ---- Selector matching ---- */

static bool node_has_class_hash(const char *cls_list, css_token_hash_t target) {
    if (!cls_list || !cls_list[0]) return false;
    const char *p = cls_list;
    while (*p) {
        while (*p && c_isspace(*p)) p++;
        if (!*p) break;
        const char *st = p;
        while (*p && !c_isspace(*p)) p++;
        size_t wlen = (size_t)(p - st);
        if (wlen && css_token_hash_equal(css_hash_token(st, wlen), target)) return true;
    }
    return false;
}

static bool attr_char_eq(char a, char b, bool insensitive) {
    return insensitive ? c_tolower(a) == c_tolower(b) : a == b;
}

static bool attr_prefix_equal(const char *actual, size_t actual_len,
                              const char *pattern, size_t pattern_len,
                              bool insensitive) {
    if (pattern_len > actual_len) return false;
    for (size_t i = 0; i < pattern_len; ++i) {
        if (!attr_char_eq(actual[i], pattern[i], insensitive)) return false;
    }
    return true;
}

static bool attr_suffix_equal(const char *actual, size_t actual_len,
                              const char *pattern, size_t pattern_len,
                              bool insensitive) {
    if (pattern_len > actual_len) return false;
    return attr_prefix_equal(actual + actual_len - pattern_len, pattern_len,
                             pattern, pattern_len, insensitive);
}

static bool attr_substring_equal(const char *actual, size_t actual_len,
                                 const char *pattern, size_t pattern_len,
                                 bool insensitive) {
    if (pattern_len == 0 || pattern_len > actual_len) return false;
    for (size_t i = 0; i + pattern_len <= actual_len; ++i) {
        if (attr_prefix_equal(actual + i, actual_len - i, pattern,
                              pattern_len, insensitive)) return true;
    }
    return false;
}

static bool attr_selector_matches(const html_stylesheet_t *ss,
                                  const css_attr_selector_t *selector,
                                  const css_node_attr_t *actual) {
    if (!(selector->flags & 1U)) return true;
    bool insensitive = (selector->flags & 2U) != 0;
    uint8_t op = selector->flags >> 2;
    if (op == 0) {
        uint64_t hash = insensitive ? actual->value_hash_ci : actual->value_hash;
        return actual->has_value && hash == selector->value_hash;
    }
    if (!actual->has_value || !ss->selector_arena ||
        selector->value_off + selector->value_len > ss->selector_arena_len)
        return false;

    const char *pattern = ss->selector_arena + selector->value_off;
    size_t pattern_len = selector->value_len;
    size_t stored_len = actual->value_stored_len;
    const char *prefix = actual->value_prefix;
    const char *suffix = actual->value_suffix;
    if (op == 3) /* ^= */
        return pattern_len <= HTML_CSS_ATTR_VALUE_SNAPSHOT &&
               attr_prefix_equal(prefix, stored_len, pattern, pattern_len,
                                 insensitive);
    if (op == 4) /* $= */
        return pattern_len <= HTML_CSS_ATTR_VALUE_SNAPSHOT &&
               attr_suffix_equal(suffix, stored_len, pattern, pattern_len,
                                 insensitive);
    if (op == 2) { /* |= */
        if (attr_prefix_equal(prefix, stored_len, pattern, pattern_len,
                              insensitive) && actual->value_len == pattern_len)
            return true;
        return actual->value_len > pattern_len &&
               pattern_len < stored_len && prefix[pattern_len] == '-' &&
               attr_prefix_equal(prefix, stored_len, pattern, pattern_len,
                                 insensitive);
    }
    if (op == 5) /* *= */
        return pattern_len <= HTML_CSS_ATTR_VALUE_SNAPSHOT &&
               (attr_substring_equal(prefix, stored_len, pattern, pattern_len,
                                     insensitive) ||
                attr_substring_equal(suffix, stored_len, pattern, pattern_len,
                                     insensitive));
    if (op == 1) { /* ~= */
        const char *ranges[2] = { prefix, suffix };
        for (size_t range = 0; range < 2; ++range) {
            const char *text = ranges[range];
            size_t length = stored_len;
            size_t i = 0;
            while (i < length) {
                while (i < length && c_isspace(text[i])) ++i;
                size_t start = i;
                while (i < length && !c_isspace(text[i])) ++i;
                size_t token_len = i - start;
                if (token_len == pattern_len &&
                    (!actual->value_truncated || range == 0 || start != 0 ||
                     c_isspace(text[0])) &&
                    attr_prefix_equal(text + start, token_len, pattern,
                                      pattern_len, insensitive)) return true;
            }
        }
    }
    return false;
}

static bool match_simple(const html_stylesheet_t *ss,
                         const css_simple_sel_t *sel,
                         const css_node_info_t *node) {
    if (sel->tag[0] && sel->tag[0] != '*') {
        if (strcmp(sel->tag, node->tag) != 0) return false;
    }
    if (sel->has_id) {
        if (!node->id[0] || !css_token_hash_equal(
                sel->id, css_hash_token(node->id, strlen(node->id)))) return false;
    }
    if (sel->require_hidden_attr && !node->hidden_attr) {
        return false;
    }
    if (sel->forbid_hidden_attr && node->hidden_attr) {
        return false;
    }
    if ((node->pseudo_state & sel->require_state) != sel->require_state ||
        (node->pseudo_state & sel->forbid_state) != 0) return false;
    for (uint8_t a = 0; a < sel->attr_count; ++a) {
        const css_attr_selector_t *required = &sel->attrs[a];
        bool found = false;
        for (uint8_t n = 0; n < node->attr_count; ++n) {
            const css_node_attr_t *actual = &node->attrs[n];
            if (actual->name_hash != required->name_hash) continue;
            if (!attr_selector_matches(ss, required, actual)) continue;
            found = true;
            break;
        }
        if (!found) return false;
    }
    for (uint8_t a = 0; a < sel->forbidden_attr_count; ++a) {
        const css_attr_selector_t *forbidden = &sel->forbidden_attrs[a];
        for (uint8_t n = 0; n < node->attr_count; ++n) {
            const css_node_attr_t *actual = &node->attrs[n];
            if (actual->name_hash != forbidden->name_hash) continue;
            if (forbidden->flags & 1U) {
                uint64_t actual_hash = (forbidden->flags & 2U)
                    ? actual->value_hash_ci : actual->value_hash;
                if (!actual->has_value || actual_hash != forbidden->value_hash)
                    continue;
            }
            return false;
        }
    }
    if (sel->has_nth_child) {
        int index = sel->nth_child_reverse
            ? (int)node->sibling_count - node->child_index + 1
            : node->child_index;
        int a = sel->nth_child_a, b = sel->nth_child_b;
        if (index <= 0 || (sel->nth_child_reverse && !node->sibling_count)) return false;
        if (a == 0) {
            if (index != b) return false;
        } else {
            int delta = index - b;
            if (delta % a != 0 || delta / a < 0) return false;
        }
    }
    if (sel->has_nth_type) {
        int index = sel->nth_type_reverse
            ? (int)node->type_count - node->type_index + 1
            : node->type_index;
        int a = sel->nth_type_a, b = sel->nth_type_b;
        if (index <= 0 || (sel->nth_type_reverse && !node->type_count)) return false;
        if (a == 0) {
            if (index != b) return false;
        } else {
            int delta = index - b;
            if (delta % a != 0 || delta / a < 0) return false;
        }
    }
    if (sel->child_pseudo) {
        if (node->child_index == 0) return false;
        if (sel->child_pseudo == 1 && node->child_index != 1) return false;
        if (sel->child_pseudo == 2 &&
            (!node->sibling_count || node->child_index != node->sibling_count)) return false;
        if (sel->child_pseudo == 3 && node->sibling_count != 1) return false;
    }
    if (sel->type_pseudo) {
        if (node->type_index == 0 || node->type_count == 0) return false;
        if (sel->type_pseudo == 1 && node->type_index != 1) return false;
        if (sel->type_pseudo == 2 && node->type_index != node->type_count) return false;
        if (sel->type_pseudo == 3 && node->type_count != 1) return false;
    }
    if (sel->has_forbid_nth_child) {
        int index = sel->forbid_nth_child_reverse
            ? (int)node->sibling_count - node->child_index + 1
            : node->child_index;
        int a = sel->forbid_nth_child_a, b = sel->forbid_nth_child_b;
        if (index <= 0 || (sel->forbid_nth_child_reverse && !node->sibling_count)) return false;
        bool matched = a == 0 ? index == b
            : ((index - b) % a == 0 && (index - b) / a >= 0);
        if (matched) return false;
    }
    if (sel->has_forbid_nth_type) {
        int index = sel->forbid_nth_type_reverse
            ? (int)node->type_count - node->type_index + 1
            : node->type_index;
        int a = sel->forbid_nth_type_a, b = sel->forbid_nth_type_b;
        if (index <= 0 || (sel->forbid_nth_type_reverse && !node->type_count)) return false;
        bool matched = a == 0 ? index == b
            : ((index - b) % a == 0 && (index - b) / a >= 0);
        if (matched) return false;
    }
    if (sel->forbid_child_pseudo) {
        if (node->child_index == 0) return false;
        if (sel->forbid_child_pseudo == 1 && node->child_index == 1) return false;
        if (sel->forbid_child_pseudo == 2 &&
            (!node->sibling_count || node->child_index == node->sibling_count)) return false;
        if (sel->forbid_child_pseudo == 3 && node->sibling_count == 1) return false;
    }
    if (sel->forbid_type_pseudo) {
        if (node->type_index == 0 || node->type_count == 0) return false;
        if (sel->forbid_type_pseudo == 1 && node->type_index == 1) return false;
        if (sel->forbid_type_pseudo == 2 && node->type_index == node->type_count) return false;
        if (sel->forbid_type_pseudo == 3 && node->type_count == 1) return false;
    }
    if (sel->require_empty && !node->is_empty) return false;
    if (sel->forbid_empty && node->is_empty) return false;
    for (int i = 0; i < sel->class_count; i++) {
        if (!node_has_class_hash(node->cls, sel->classes[i])) return false;
    }
    for (int i = 0; i < sel->not_class_count; i++) {
        if (node_has_class_hash(node->cls, sel->not_classes[i])) return false;
    }
    return true;
}

static int node_ancestor_index(const css_node_info_t *node,
                               const css_node_info_t *ancestors, int depth) {
    for (int i = depth - 1; i >= 0; --i) {
        if (node == &ancestors[i]) return i;
    }
    return -1;
}

static const css_node_info_t *node_parent(const css_node_info_t *node,
                                         const css_node_info_t *ancestors,
                                         int depth) {
    if (node->parent) return node->parent;
    int index = node_ancestor_index(node, ancestors, depth);
    return index > 0 ? &ancestors[index - 1] : NULL;
}

static bool match_chain_part(const html_stylesheet_t *ss,
                             const css_rule_t *rule, int part_index,
                             const css_node_info_t *node,
                             const css_node_info_t *ancestors, int depth,
                             int recursion_budget) {
    if (!node || recursion_budget <= 0 ||
        !match_simple(ss, &CSS_RULE_PARTS(ss, rule)[part_index], node)) return false;
    if (part_index == 0) return true;

    uint8_t comb = CSS_RULE_PARTS(ss, rule)[part_index].combinator;
    const css_node_info_t *parent = node_parent(node, ancestors, depth);
    if (comb == 2) { /* Child '>' */
        return match_chain_part(ss, rule, part_index - 1, parent, ancestors, depth,
                                recursion_budget - 1);
    }
    if (comb == 3) { /* Adjacent sibling '+' */
        return match_chain_part(ss, rule, part_index - 1, node->prev_sibling,
                                ancestors, depth, recursion_budget - 1);
    }
    if (comb == 4) { /* General sibling '~' */
        int sibling_budget = HTML_CSS_MAX_SEL_CHAIN - 1;
        for (const css_node_info_t *sibling = node->prev_sibling;
             sibling && sibling_budget-- > 0; sibling = sibling->prev_sibling) {
            if (match_chain_part(ss, rule, part_index - 1, sibling, ancestors, depth,
                                 recursion_budget - 1)) return true;
        }
        return false;
    }

    /* Descendant ' ': try each parent; recursive matching backtracks across
     * multiple ancestor candidates, as required by compound selector chains. */
    for (const css_node_info_t *candidate = parent; candidate;
         candidate = node_parent(candidate, ancestors, depth)) {
        if (match_chain_part(ss, rule, part_index - 1, candidate, ancestors, depth,
                             recursion_budget - 1)) return true;
    }
    return false;
}

static bool match_rule_chain(const html_stylesheet_t *ss,
                             const css_rule_t *rule,
                             const css_node_info_t *ancestors, int depth) {
    if (depth <= 0 || rule->part_count == 0) return false;
    return match_chain_part(ss, rule, (int)rule->part_count - 1,
                            &ancestors[depth - 1], ancestors, depth,
                            (int)rule->part_count * 8);
}

typedef void (*css_candidate_fn)(const css_rule_t *rule, void *context);

static void css_visit_bucket(const html_stylesheet_t *ss, uint8_t kind,
                             uint64_t hash, css_candidate_fn visit,
                             void *context) {
    if (!ss || kind == 0 || kind > 3 || !visit) return;
    uint32_t encoded = ss->index_heads[kind - 1U][css_index_bucket(hash)];
    while (encoded != 0) {
        uint32_t index = encoded - 1U;
        if (index >= (uint32_t)ss->rule_count) return;
        const css_rule_t *rule = &ss->rules[index];
        if (rule->index_kind == kind && rule->index_hash == hash)
            visit(rule, context);
        encoded = rule->index_next;
    }
}

static void css_visit_candidates(const html_stylesheet_t *ss,
                                 const css_node_info_t *node,
                                 css_candidate_fn visit, void *context) {
    if (!ss || !node || !visit) return;
    for (uint32_t encoded = ss->generic_head; encoded != 0;) {
        uint32_t index = encoded - 1U;
        if (index >= (uint32_t)ss->rule_count) break;
        const css_rule_t *rule = &ss->rules[index];
        visit(rule, context);
        encoded = rule->index_next;
    }

    if (node->id[0]) {
        css_token_hash_t id = css_hash_token(node->id, strlen(node->id));
        css_visit_bucket(ss, 1, css_index_token_hash(id), visit, context);
    }

    const char *p = node->cls;
    while (p && *p) {
        while (*p && c_isspace(*p)) ++p;
        if (!*p) break;
        const char *start = p;
        while (*p && !c_isspace(*p)) ++p;
        css_token_hash_t token = css_hash_token(start, (size_t)(p - start));
        css_visit_bucket(ss, 2, css_index_token_hash(token), visit, context);
    }

    if (node->tag[0]) {
        uint64_t tag = css_hash_slice(node->tag, strlen(node->tag), true);
        css_visit_bucket(ss, 3, tag, visit, context);
    }
}

#ifdef HTML_CSS_HOST_TEST
static void css_count_candidate(const css_rule_t *rule, void *opaque) {
    (void)rule;
    (*(size_t *)opaque)++;
}

size_t html_css_host_candidate_count(const html_stylesheet_t *ss,
                                     const css_node_info_t *node) {
    size_t count = 0;
    css_visit_candidates(ss, node, css_count_candidate, &count);
    return count;
}

size_t html_css_host_selector_part_capacity(const html_stylesheet_t *ss) {
    return ss ? ss->selector_part_capacity : 0;
}
#endif

/* ---- Public API ---- */

html_stylesheet_t *html_css_create(int viewport_w) {
    html_stylesheet_t *ss = (html_stylesheet_t *)kmalloc(sizeof(html_stylesheet_t));
    if (!ss) return NULL;
    memset(ss, 0, sizeof(*ss));
    ss->rules = (css_rule_t *)kmalloc(
        (size_t)HTML_CSS_INITIAL_RULES * sizeof(css_rule_t));
    if (!ss->rules) {
        kfree(ss);
        return NULL;
    }
    memset(ss->rules, 0, (size_t)HTML_CSS_INITIAL_RULES * sizeof(css_rule_t));
    ss->rule_capacity = HTML_CSS_INITIAL_RULES;
    ss->viewport_w = (viewport_w > 120) ? viewport_w : 900;
    ss->viewport_h = (ss->viewport_w * 56) / 100;
    return ss;
}

void html_css_set_viewport_height(html_stylesheet_t *ss, int viewport_h) {
    if (ss && viewport_h > 0) ss->viewport_h = viewport_h;
}

void html_css_set_prefer_dark_scheme(html_stylesheet_t *ss, bool prefer_dark) {
    if (ss) ss->prefer_dark_scheme = prefer_dark;
}

bool html_css_reserve_selector_parts(html_stylesheet_t *ss, size_t capacity) {
    if (!ss) return false;
    size_t max_parts = (size_t)HTML_CSS_MAX_RULES * HTML_CSS_MAX_SEL_CHAIN;
    if (capacity > max_parts) capacity = max_parts;
    if (capacity <= ss->selector_part_capacity) return true;
    if (capacity > SIZE_MAX / sizeof(css_simple_sel_t)) return false;
    css_simple_sel_t *grown = (css_simple_sel_t *)krealloc(
        ss->selector_parts, capacity * sizeof(css_simple_sel_t));
    if (!grown) return false;
    ss->selector_parts = grown;
    ss->selector_part_capacity = capacity;
    return true;
}

void html_css_destroy(html_stylesheet_t *ss) {
    if (!ss) return;
    if (ss->arena) kfree(ss->arena);
    if (ss->selector_arena) kfree(ss->selector_arena);
    if (ss->rules) kfree(ss->rules);
    if (ss->selector_parts) kfree(ss->selector_parts);
    kfree(ss);
}

void html_css_add_stylesheet(html_stylesheet_t *ss, const char *css, size_t len) {
    if (!ss || !css || len == 0) return;
    parse_css_chunk(ss, css, len, 0);
}

void html_css_collect_matched_vars(html_stylesheet_t *ss,
                                   const css_node_info_t *ancestors, int depth) {
    if (!ss || !ancestors || depth <= 0 || !ss->arena) return;
    /* Preserve source order because later matching declarations override
     * earlier custom-property values. This path runs only for html/body. */
    for (int i = 0; i < ss->rule_count; i++) {
        const css_rule_t *rule = &ss->rules[i];
        if (rule->decl_len == 0 ||
            !match_rule_chain(ss, rule, ancestors, depth)) continue;
        collect_vars_from_block(ss, ss->arena + rule->decl_off,
                                rule->decl_len);
    }
}

void html_css_style_init(css_style_t *st, const css_style_t *parent) {
    memset(st, 0, sizeof(*st));
    st->grid_col_span = 1;
    for (int i = 0; i < 4; i++) {
        st->border_w[i] = HTML_CSS_UNSET;
        st->pad[i] = HTML_CSS_UNSET;
        st->margin[i] = HTML_CSS_UNSET;
    }
    st->radius = HTML_CSS_UNSET;
    st->width_px = HTML_CSS_UNSET;
    st->width_pct = HTML_CSS_UNSET;
    st->max_width_px = HTML_CSS_UNSET;
    st->max_width_pct = HTML_CSS_UNSET;
    st->min_width_px = HTML_CSS_UNSET;
    st->height_px = HTML_CSS_UNSET;
    st->min_height_px = HTML_CSS_UNSET;
    st->gap = HTML_CSS_UNSET;
    st->row_gap = HTML_CSS_UNSET;
    st->column_gap = HTML_CSS_UNSET;
    st->flex_basis_pct = HTML_CSS_UNSET;
    st->flex_basis_zero = false;

    if (parent) {
        st->has_color = parent->has_color;
        st->color = parent->color;
        st->font_px = parent->font_px;
        st->has_bold = parent->has_bold;
        st->bold = parent->bold;
        st->has_italic = parent->has_italic;
        st->italic = parent->italic;
        st->has_underline = parent->has_underline;
        st->underline = parent->underline;
        st->has_strike = parent->has_strike;
        st->strike = parent->strike;
        st->text_align = parent->text_align;
        st->has_preformatted = parent->has_preformatted;
        st->preformatted = parent->preformatted;
        st->visibility = parent->visibility;
        st->scoped_var_count = parent->scoped_var_count;
        memcpy(st->scoped_vars, parent->scoped_vars,
               (size_t)st->scoped_var_count * sizeof(st->scoped_vars[0]));
    }
}

typedef struct {
    uint64_t score;
    uint16_t rule_idx;
} css_match_t;

#define HTML_CSS_MAX_MATCHES 256

static void css_match_insert(css_match_t *matches, int *count,
                             css_match_t candidate) {
    if (*count == HTML_CSS_MAX_MATCHES) {
        /* Keep the strongest/later matches. Appending the first 72 matches
         * silently discarded later declarations on dense real-world sheets. */
        if (candidate.score <= matches[0].score) return;
        for (int i = 1; i < *count; ++i) matches[i - 1] = matches[i];
        (*count)--;
    }

    int position = *count;
    while (position > 0 && matches[position - 1].score > candidate.score) {
        matches[position] = matches[position - 1];
        --position;
    }
    matches[position] = candidate;
    (*count)++;
}

typedef struct {
    const html_stylesheet_t *sheet;
    const css_node_info_t *ancestors;
    int depth;
    css_match_t *matches;
    int match_count;
} css_match_context_t;

static void css_match_candidate(const css_rule_t *rule, void *opaque) {
    css_match_context_t *context = (css_match_context_t *)opaque;
    if (!match_rule_chain(context->sheet, rule,
                          context->ancestors, context->depth)) return;
    uint64_t score = ((uint64_t)rule->specificity << 32) | rule->order;
    css_match_insert(context->matches, &context->match_count,
                     (css_match_t){ .score = score,
                                    .rule_idx = (uint16_t)(rule -
                                        context->sheet->rules) });
}

void html_css_compute(const html_stylesheet_t *ss,
                      const css_node_info_t *ancestors, int depth,
                      const char *inline_style,
                      const css_style_t *parent_style,
                      css_style_t *out_style) {
    const css_style_t *previous_var_scope = g_css_var_scope;
    html_css_style_init(out_style, parent_style);
    g_css_var_scope = out_style;
    sr_track_t sr;
    memset(&sr, 0, sizeof(sr));

    /* The HTML hidden attribute supplies the default presentation, but CSS
     * author rules may override it (for example `[hidden]{display:block}`).
     * Keep evaluating selectors and declarations after setting the default. */
    if (ancestors && depth > 0 && ancestors[depth - 1].hidden_attr)
        out_style->display = CSS_DISP_NONE;

    css_match_t matches[HTML_CSS_MAX_MATCHES];
    int match_count = 0;

    if (ss && ancestors && depth > 0 && ss->arena) {
        css_match_context_t context = {
            .sheet = ss,
            .ancestors = ancestors,
            .depth = depth,
            .matches = matches,
            .match_count = 0
        };
        css_visit_candidates(ss, &ancestors[depth - 1],
                             css_match_candidate, &context);
        match_count = context.match_count;

        for (int i = 0; i < match_count; i++) {
            const css_rule_t *r = &ss->rules[matches[i].rule_idx];
            apply_decl_block(ss, ss->arena + r->decl_off, r->decl_len,
                             out_style, &sr, false, true);
        }
    }

    if (inline_style && inline_style[0]) {
        apply_decl_block(ss, inline_style, strlen(inline_style), out_style,
                         &sr, false, true);
    }

    /* Resolve the complete custom-property cascade before consuming var().
     * A regular declaration can reference a variable overridden with
     * !important on the same element. */
    for (int i = 0; i < match_count; i++) {
        const css_rule_t *r = &ss->rules[matches[i].rule_idx];
        apply_decl_block(ss, ss->arena + r->decl_off, r->decl_len,
                         out_style, &sr, true, true);
    }
    if (inline_style && inline_style[0]) {
        apply_decl_block(ss, inline_style, strlen(inline_style), out_style,
                         &sr, true, true);
    }

    if (ss && ancestors && depth > 0 && ss->arena) {
        for (int i = 0; i < match_count; i++) {
            const css_rule_t *r = &ss->rules[matches[i].rule_idx];
            apply_decl_block(ss, ss->arena + r->decl_off, r->decl_len,
                             out_style, &sr, false, false);
        }
    }
    if (inline_style && inline_style[0]) {
        apply_decl_block(ss, inline_style, strlen(inline_style), out_style,
                         &sr, false, false);
    }

    /* Second pass: !important declarations override normal specificity */
    for (int i = 0; i < match_count; i++) {
        const css_rule_t *r = &ss->rules[matches[i].rule_idx];
        apply_decl_block(ss, ss->arena + r->decl_off, r->decl_len,
                         out_style, &sr, true, false);
    }
    if (inline_style && inline_style[0]) {
        apply_decl_block(ss, inline_style, strlen(inline_style), out_style,
                         &sr, true, false);
    }

    if (sr.seen_pos_abs && sr.seen_overflow_hidden && (sr.seen_1px_w || sr.seen_1px_h)) {
        out_style->is_sr_only = true;
    }
    if (out_style->opacity_zero &&
        (out_style->position_mode == CSS_POS_ABSOLUTE ||
         out_style->position_mode == CSS_POS_FIXED ||
         sr.seen_zero_h)) {
        out_style->offscreen_hidden = true;
    }
    g_css_var_scope = previous_var_scope;
}
