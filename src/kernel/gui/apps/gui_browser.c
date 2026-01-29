/*
 * gui/apps/gui_browser.c
 *
 * Simple in-kernel web browser (text / reader mode).
 *
 *  - Address bar: type a URL (e.g. "wikipedia.org" or "https://...")
 *    and press Enter.
 *  - Uses the kernel HTTP/TCP/IP stack (http_get_url), with HTTPS via
 *    BearSSL (TLS 1.2).
 *  - Strips HTML tags, numbers every link ([n]), renders the text in a
 *    scrollable monospace grid (PgUp/PgDn/Arrows to scroll).
 *  - Navigation: type a link number (single or double digit) and press
 *    Enter; Backspace (empty bar) or Esc goes back in history.
 */
#include "gui_browser.h"
#include "../scene/node.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "../assets/asset_manager.h"
#include "../widgets/window_node.h"
#include "../window/focus_manager.h"
#include "../window/window_manager.h"
#include "../core/event_bus.h"
#include "../layout/layout_engine.h"
#include "kheap.h"
#include "string.h"
#include "vga.h"
#include "net.h"
#include "netstack.h"
#include "http.h"

/* Forward declarations from GUI core */
extern scene_graph_t *g_scene;
extern gui_event_bus_t   *g_event_bus;
extern focus_manager_t   *g_focus_manager;

extern char *itoa(int value, char *str, int base);

/* ---- Browser state ---- */

typedef struct browser_link {
    char url[BROWSER_ADDR];
} browser_link_t;

typedef struct gui_browser {
    char          addr[BROWSER_ADDR];
    char          loaded_url[BROWSER_ADDR];
    int           addr_len;

    int           scroll;      /* top visible document line */

    char          status[96];

    int           pending_sel; /* partially typed link number (0 = none) */
    int           pending_len;

    char          history[BROWSER_HISTORY][BROWSER_ADDR];
    int           hist_count;

    const glyph_t *font;
    node_t       *win_node;
} gui_browser_t;

static const char BROWSER_ADDR_CLR[] = "URL: ";

/* ---- Document buffers (static: single browser instance) ---- */

static char s_lines[BROWSER_LINES][BROWSER_COLS + 1];
static int  s_line_count;
static char s_html[BROWSER_RESP];
static char s_text[BROWSER_RESP];

static browser_link_t s_links[BROWSER_MAX_LINKS];
static int  s_link_count;

/* --------------------------------------------------------------------------
 * HTML helpers
 * -------------------------------------------------------------------------- */

static char browser_lower(char ch) {
    if (ch >= 'A' && ch <= 'Z') return (char)(ch - 'A' + 'a');
    return ch;
}

static int browser_tag_name(const char *tag, int taglen, char *out, int outsz) {
    int i = 0;
    if (taglen > 0 && tag[0] == '/') i = 1; /* closing tag */
    int n = 0;
    while (i < taglen && n < outsz - 1) {
        char ch = tag[i];
        if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'))) break;
        out[n++] = ch;
        i++;
    }
    out[n] = '\0';
    return n;
}

static int browser_block_tag(const char *name) {
    static const char *blocks[] = {
        "p","div","br","li","h1","h2","h3","h4","h5","h6","ul","ol",
        "table","tr","td","th","blockquote","hr","dl","dt","dd",
        "form","fieldset","header","footer","section","article","pre"
    };
    for (size_t i = 0; i < sizeof(blocks) / sizeof(blocks[0]); i++) {
        if (strcmp(name, blocks[i]) == 0) return 1;
    }
    return 0;
}

static void browser_emit(char *out, int out_size, int *o, char c) {
    if (*o < out_size - 1) out[(*o)++] = c;
}

/* Decode a single HTML entity at '&' and return #bytes consumed (or 0). */
static int browser_decode_entity(const char *p, char *out) {
    if (p[0] != '&') return 0;
    if (p[1] == '#') {
        int num = 0;
        int i = 2;
        if (p[i] == 'x' || p[i] == 'X') {
            i++;
            while (p[i] && p[i] != ';') {
                char c = p[i];
                int v;
                if (c >= '0' && c <= '9') v = c - '0';
                else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
                else break;
                num = num * 16 + v;
                i++;
            }
        } else {
            while (p[i] && p[i] != ';') {
                if (p[i] < '0' || p[i] > '9') break;
                num = num * 10 + (p[i] - '0');
                i++;
            }
        }
        if (p[i] == ';') {
            if (num == 160) num = 32; /* nbsp -> space */
            if (num >= 32 && num <= 126) {
                *out = (char)num;
                return i + 1;
            }
            *out = ' ';
            return i + 1;
        }
        return 0;
    }
    struct { const char *name; char repl; } map[] = {
        { "amp;", '&' }, { "lt;", '<' }, { "gt;", '>' },
        { "quot;", '"' }, { "apos;", '\'' }, { "nbsp;", ' ' }
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        size_t len = strlen(map[i].name);
        if (strncmp(p + 1, map[i].name, len) == 0) {
            *out = map[i].repl;
            return (int)len + 1;
        }
    }
    return 0;
}

/* Extract href from an <a ...> tag body. Returns 1 if found. */
static int browser_anchor_href(const char *tag, int taglen, char *out, int outsz) {
    int i = 0;
    while (i + 4 <= taglen) {
        if (browser_lower(tag[i]) == 'h' &&
            browser_lower(tag[i + 1]) == 'r' &&
            browser_lower(tag[i + 2]) == 'e' &&
            browser_lower(tag[i + 3]) == 'f') {
            int j = i + 4;
            while (j < taglen && (tag[j] == ' ' || tag[j] == '\t')) j++;
            if (j >= taglen || tag[j] != '=') { i += 4; continue; }
            j++;
            while (j < taglen && (tag[j] == ' ' || tag[j] == '\t')) j++;
            char q = 0;
            if (j < taglen && (tag[j] == '"' || tag[j] == '\'')) q = tag[j++];
            int n = 0;
            while (j < taglen) {
                if (q) {
                    if (tag[j] == q) break;
                } else {
                    if (tag[j] == '>' || tag[j] == ' ' || tag[j] == '\t') break;
                }
                if (n < outsz - 1) out[n++] = tag[j];
                j++;
            }
            out[n] = '\0';
            return (n > 0) ? 1 : 0;
        }
        i++;
    }
    out[0] = '\0';
    return 0;
}

/* Strip HTML into plain text in 'out'; also collects numbered links. */
static void browser_extract_text(const char *html, char *out, int out_size) {
    int o = 0;
    int skip_depth = 0;      /* inside <script>/<style> */
    int last_nl = 1;         /* start of line */
    int mark_pending = 0;    /* [n] marker not yet emitted */
    const char *p = html;

    s_link_count = 0;

    while (*p && o < out_size - 1) {
        char c = *p;

        if (c == '<') {
            const char *end = p;
            while (*end && *end != '>') end++;
            if (!*end) break;
            if (end - p > 1) {
                char name[16];
                int real = browser_tag_name(p + 1, (int)(end - p) - 1,
                                            name, sizeof(name));
                if (real == 0 || name[0] == '\0') {
                    browser_emit(out, out_size, &o, '<');
                    p++;
                    continue;
                }
                int closing = (p[1] == '/');

                if (strcmp(name, "a") == 0) {
                    if (!closing) {
                        char href[BROWSER_ADDR];
                        if (browser_anchor_href(p + 1, (int)(end - p) - 1,
                                                href, sizeof(href)) &&
                            s_link_count < BROWSER_MAX_LINKS) {
                            strcpy(s_links[s_link_count].url, href);
                            s_link_count++;
                            mark_pending = 1;
                        }
                    } else {
                        mark_pending = 0;
                    }
                } else if (strcmp(name, "script") == 0 ||
                           strcmp(name, "style") == 0) {
                    if (!closing) skip_depth++;
                    else if (skip_depth > 0) skip_depth--;
                } else if (browser_block_tag(name)) {
                    if (!last_nl && o > 0) {
                        browser_emit(out, out_size, &o, '\n');
                        last_nl = 1;
                    }
                }
            }
            p = end + 1;
            continue;
        }

        if (skip_depth > 0) {
            p++;
            continue;
        }

        if (c == '&') {
            char dec;
            int n = browser_decode_entity(p, &dec);
            if (n > 0) {
                if (dec != ' ' && !(dec == ' ' && last_nl)) {
                    if (mark_pending) {
                        browser_emit(out, out_size, &o, '[');
                        {
                            char num[8];
                            itoa(s_link_count, num, 10);
                            for (char *s = num; *s; s++)
                                browser_emit(out, out_size, &o, *s);
                        }
                        browser_emit(out, out_size, &o, ']');
                        browser_emit(out, out_size, &o, ' ');
                        mark_pending = 0;
                    }
                    browser_emit(out, out_size, &o, dec);
                }
                if (dec != ' ') last_nl = 0;
                p += n;
                continue;
            }
        }

        if (c == '\r' || c == '\t') c = ' ';
        if (c == '\n') {
            if (!last_nl) {
                browser_emit(out, out_size, &o, '\n');
                last_nl = 1;
            }
            p++;
            continue;
        }
        if (c == ' ' && last_nl) {
            p++;
            continue; /* skip leading spaces on a line */
        }
        if (c == ' ' && o > 0 && out[o - 1] == ' ') {
            p++;
            continue; /* collapse runs of spaces */
        }

        if (mark_pending && c != ' ') {
            browser_emit(out, out_size, &o, '[');
            {
                char num[8];
                itoa(s_link_count, num, 10);
                for (char *s = num; *s; s++)
                    browser_emit(out, out_size, &o, *s);
            }
            browser_emit(out, out_size, &o, ']');
            browser_emit(out, out_size, &o, ' ');
            mark_pending = 0;
        }

        browser_emit(out, out_size, &o, c);
        last_nl = 0;
        p++;
    }
    out[o] = '\0';
}

/* Wrap the plain text into fixed-width lines. */
static void browser_wrap(const char *text) {
    int li = 0;
    int ci = 0;
    const char *p = text;

    while (*p && li < BROWSER_LINES) {
        if (*p == '\n') {
            if (ci > 0) {
                s_lines[li][ci] = '\0';
                li++;
                ci = 0;
            }
            p++;
            continue;
        }
        if (*p == '\t') {
            p++;
            continue;
        }
        if (ci >= BROWSER_COLS) {
            s_lines[li][ci] = '\0';
            li++;
            ci = 0;
            if (li >= BROWSER_LINES) break;
        }
        s_lines[li][ci++] = *p;
        p++;
    }
    if (ci > 0 && li < BROWSER_LINES) {
        s_lines[li][ci] = '\0';
        li++;
    }
    if (li == 0) {
        s_lines[0][0] = '\0';
        li = 1;
    }
    s_line_count = li;
}

/* --------------------------------------------------------------------------
 * URL helpers
 * -------------------------------------------------------------------------- */

static void browser_normalize_addr(char *addr) {
    if (strstr(addr, "http://") != addr && strstr(addr, "https://") != addr) {
        char tmp[BROWSER_ADDR];
        strcpy(tmp, "http://");
        strcat(tmp, addr);
        strcpy(addr, tmp);
    }
}

/* Resolve possibly-relative 'rel' against absolute 'base'. Returns 0 ok. */
static int browser_abs_url(const char *base, const char *rel, char *out, int outsz) {
    if (strstr(rel, "http://") == rel || strstr(rel, "https://") == rel) {
        if ((int)strlen(rel) >= outsz) return -1;
        strcpy(out, rel);
        return 0;
    }
    if (rel[0] == '#') return -1; /* same-page anchor */

    char scheme[8];
    const char *p = strstr(base, "://");
    int slen = p ? (int)(p - base) : 4;
    if (slen > 7) slen = 7;
    memcpy(scheme, base, slen);
    scheme[slen] = '\0';

    const char *h = base;
    const char *hend = h;
    while (*hend && *hend != '/') hend++;

    if (rel[0] == '/' && rel[1] == '/') {
        /* scheme-relative */
        if ((int)(strlen(scheme) + 1 + strlen(rel)) >= outsz) return -1;
        strcpy(out, scheme);
        strcat(out, ":");
        strcat(out, rel);
        return 0;
    }
    if (rel[0] == '/') {
        int plen = (int)(hend - h);
        if ((int)(plen + strlen(rel)) >= outsz) return -1;
        memcpy(out, h, plen);
        out[plen] = '\0';
        strcat(out, rel);
        return 0;
    }

    /* Relative to the directory of the base path */
    char dir[256];
    dir[0] = '\0';
    const char *path = hend;
    const char *last_slash = path + strlen(path);
    for (const char *q = path; *q; q++) {
        if (*q == '/') last_slash = q;
    }
    if (last_slash > path) {
        int dlen = (int)(last_slash - path) + 1;
        if (dlen >= (int)sizeof(dir)) dlen = (int)sizeof(dir) - 1;
        memcpy(dir, path, dlen);
        dir[dlen] = '\0';
    }
    int plen = (int)(hend - h);
    if ((int)(plen + strlen(dir) + strlen(rel)) >= outsz) return -1;
    memcpy(out, h, plen);
    out[plen] = '\0';
    strcat(out, dir);
    strcat(out, rel);
    return 0;
}

/* --------------------------------------------------------------------------
 * Fetch + render text
 * -------------------------------------------------------------------------- */

static void browser_load(gui_browser_t *b);

/* --------------------------------------------------------------------------
 * Navigation
 * -------------------------------------------------------------------------- */

static void browser_history_push(gui_browser_t *b, const char *url) {
    if (b->hist_count >= BROWSER_HISTORY) {
        /* drop oldest */
        for (int i = 1; i < BROWSER_HISTORY; i++)
            strcpy(b->history[i - 1], b->history[i]);
        b->hist_count = BROWSER_HISTORY - 1;
    }
    if (b->hist_count < BROWSER_HISTORY)
        strcpy(b->history[b->hist_count++], url);
}

static void browser_goto(gui_browser_t *b, const char *url) {
    browser_history_push(b, b->addr);
    strcpy(b->addr, url);
    browser_load(b);
}

static void browser_back(gui_browser_t *b) {
    if (b->hist_count <= 0) return;
    b->hist_count--;
    strcpy(b->addr, b->history[b->hist_count]);
    browser_load(b);
}

static void browser_follow_link(gui_browser_t *b, int idx) {
    if (idx <= 0 || idx > s_link_count) return;
    const char *raw = s_links[idx - 1].url;
    if (raw[0] == '#' || raw[0] == '\0') return;

    char resolved[BROWSER_ADDR];
    if (browser_abs_url(b->addr, raw, resolved, sizeof(resolved)) < 0) return;
    browser_goto(b, resolved);
}

/* --------------------------------------------------------------------------
 * Fetch + render text
 * -------------------------------------------------------------------------- */

static void browser_load(gui_browser_t *b) {
    if (!net_get_list()) {
        strcpy(b->status, "Rede indisponivel.");
        node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);
        return;
    }

    browser_normalize_addr(b->addr);

    strcpy(b->status, "Baixando pagina...");
    node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);

    int got = http_get_url(b->addr, s_html, BROWSER_RESP - 1);
    if (got < 0) {
        strcpy(b->status, "Falha ao baixar a pagina (rede, host ou timeout).");
        node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);
        return;
    }

    s_html[got] = '\0';
    browser_extract_text(s_html, s_text, BROWSER_RESP);
    browser_wrap(s_text);
    b->scroll = 0;
    strcpy(b->loaded_url, b->addr);

    strcpy(b->status, "OK: ");
    {
        char num[16];
        itoa(got, num, 10);
        strcat(b->status, num);
        strcat(b->status, " bytes. Links: ");
        itoa(s_link_count, num, 10);
        strcat(b->status, num);
        strcat(b->status, ". PgDn rola; digite numero do link.");
    }
    node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);
}

/* --------------------------------------------------------------------------
 * Key handlers
 * -------------------------------------------------------------------------- */

static bool browser_key_down(node_t *self, uint8_t sc, void *ctx) {
    (void)self;
    gui_browser_t *b = (gui_browser_t *)ctx;
    if (!b) return true;

    if (sc == 0x0E) { /* Backspace */
        b->pending_sel = 0;
        b->pending_len = 0;
        if (b->addr_len > 0) {
            b->addr_len--;
            b->addr[b->addr_len] = '\0';
            node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);
        } else {
            browser_back(b);
        }
    } else if (sc == 0x1C) { /* Enter */
        if (b->pending_sel > 0) {
            browser_follow_link(b, b->pending_sel);
        } else if (strcmp(b->addr, b->loaded_url) != 0 ||
                   b->loaded_url[0] == '\0') {
            browser_goto(b, b->addr);
        } else {
            browser_load(b); /* reload current page */
        }
    } else if (sc == 0x01) { /* Esc -> back */
        browser_back(b);
    } else if (sc == 0x49) { /* PgUp */
        b->scroll -= 20;
    } else if (sc == 0x51) { /* PgDn */
        b->scroll += 20;
    } else if (sc == 128) {  /* Up */
        b->scroll--;
    } else if (sc == 129) {  /* Down */
        b->scroll++;
    }

    int max_scroll = s_line_count - 1;
    if (b->scroll < 0) b->scroll = 0;
    if (b->scroll > max_scroll) b->scroll = max_scroll;

    node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);
    return true;
}

static bool browser_key_char(node_t *self, char c, void *ctx) {
    (void)self;
    gui_browser_t *b = (gui_browser_t *)ctx;
    if (!b) return true;

    /* Digits select links only when the address bar holds the loaded page.
     * While typing/editing a URL, digits are part of the address. */
    int browsing = (strcmp(b->addr, b->loaded_url) == 0 &&
                    b->loaded_url[0] != '\0');

    if (c >= '0' && c <= '9') {
        if (!browsing) {
            /* typing a URL: digit goes to the address bar */
            if (b->addr_len < BROWSER_ADDR - 1) {
                b->pending_sel = 0;
                b->pending_len = 0;
                b->addr[b->addr_len++] = c;
                b->addr[b->addr_len] = '\0';
                node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);
            }
            return true;
        }

        int d = c - '0';
        if (b->pending_len >= 2) {
            b->pending_sel = d;
            b->pending_len = 1;
        } else {
            b->pending_sel = b->pending_sel * 10 + d;
            b->pending_len++;
        }

        if (b->pending_sel > 0 && b->pending_sel <= s_link_count) {
            int navigate = 0;
            if (b->pending_len >= 2) {
                navigate = 1;
            } else if (b->pending_sel * 10 > s_link_count) {
                navigate = 1; /* no two-digit target starts with this */
            }
            if (navigate) {
                browser_follow_link(b, b->pending_sel);
                b->pending_sel = 0;
                b->pending_len = 0;
                return true;
            }
        } else if (b->pending_sel > s_link_count) {
            b->pending_sel = 0;
            b->pending_len = 0;
        }

        strcpy(b->status, "Link: ");
        {
            char num[16];
            itoa(b->pending_sel, num, 10);
            strcat(b->status, num);
            strcat(b->status, "? (Enter confirma)");
        }
        node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);
        return true;
    }

    if (c >= 32 && c <= 126 && b->addr_len < BROWSER_ADDR - 1) {
        b->pending_sel = 0;
        b->pending_len = 0;
        b->addr[b->addr_len++] = c;
        b->addr[b->addr_len] = '\0';
        node_mark_dirty(b->win_node, NODE_DIRTY_PAINT);
    }
    return true;
}

static gui_browser_t *g_browser_active = NULL;

/* --------------------------------------------------------------------------
 * Draw
 * -------------------------------------------------------------------------- */

#define BROWSER_CHAR_W  8
#define BROWSER_CHAR_H  16
#define BROWSER_PAD_X   6
#define BROWSER_PAD_Y   4

static void browser_draw(node_t *self, struct gui_renderer *r) {
    gui_browser_t *b = g_browser_active;
    if (!b) return;

    if (!b->font) b->font = asset_manager_get_font(NULL);

    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    /* Window chrome (titlebar, borders) */
    extern void window_node_draw(node_t *self, struct gui_renderer *r);
    window_node_draw(self, r);

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int screen_x = camera_world_to_screen_x(cam, pt.x);
    int screen_y = camera_world_to_screen_y(cam, pt.y);
    int screen_w = camera_scale(cam, self->width);
    int screen_h = camera_scale(cam, self->height);

    if (!b->font) return;
    self->screen_bounds = rect_make(screen_x, screen_y, screen_w, screen_h);

    int top_margin = camera_scale(cam, 24);

    gui_rect_t content = rect_make(screen_x, screen_y + top_margin,
                                    screen_w, screen_h - top_margin);
    renderer_fill_rect(r, content, 0xFF1E2229);

    int avail_w = screen_w - BROWSER_PAD_X * 2;
    int avail_h = screen_h - top_margin - BROWSER_PAD_Y * 2;
    int cols_vis = avail_w / BROWSER_CHAR_W;
    int rows_vis = avail_h / BROWSER_CHAR_H;
    if (cols_vis > BROWSER_COLS) cols_vis = BROWSER_COLS;

    int base_x = screen_x + BROWSER_PAD_X;
    int base_y = screen_y + top_margin + BROWSER_PAD_Y;

    /* --- Row 0: address bar --- */
    int py = base_y;
    int px = base_x;
    renderer_fill_rect(r, rect_make(screen_x, py - 2, screen_w, BROWSER_CHAR_H + 4),
                       0xFF1B1E24);
    for (const char *s = BROWSER_ADDR_CLR; *s; s++, px += BROWSER_CHAR_W) {
        renderer_draw_glyph(r, px, py, 0xFF4A9EFF, 0x00000000,
                            &b->font[(unsigned char)*s]);
    }
    for (int i = 0; b->addr[i] && i < cols_vis - 5; i++, px += BROWSER_CHAR_W) {
        char ch = b->addr[i];
        if (ch < 32 || ch > 126) ch = ' ';
        renderer_draw_glyph(r, px, py, 0xFFE6E8EB, 0x00000000,
                            &b->font[(unsigned char)ch]);
    }

    if (rows_vis < 3) return;

    /* --- Row 1: status --- */
    py += BROWSER_CHAR_H;
    px = base_x;
    for (int i = 0; b->status[i] && i < cols_vis; i++, px += BROWSER_CHAR_W) {
        char ch = b->status[i];
        if (ch < 32 || ch > 126) ch = ' ';
        renderer_draw_glyph(r, px, py, 0xFF99A1AF, 0x00000000,
                            &b->font[(unsigned char)ch]);
    }

    /* --- Rows 2+: document --- */
    int first_doc_row = 2;
    int doc_rows = rows_vis - first_doc_row;
    if (doc_rows <= 0) return;

    int mline_hi = s_line_count;
    if (mline_hi > BROWSER_LINES) mline_hi = BROWSER_LINES;

    int top = b->scroll;
    if (top < 0) top = 0;
    if (top > mline_hi - 1) top = mline_hi - 1;

    for (int row = 0; row < doc_rows; row++) {
        int li = top + row;
        if (li >= mline_hi) break;

        py = base_y + (row + first_doc_row) * BROWSER_CHAR_H;
        px = base_x;
        const char *line = s_lines[li];
        int in_link = 0;
        for (int col = 0; col < cols_vis && line[col]; col++, px += BROWSER_CHAR_W) {
            char ch = line[col];
            if (ch < 32 || ch > 126) ch = ' ';
            uint32_t color = 0xFFC9CED6;
            if (ch == '[') in_link = 1;
            if (in_link) color = 0xFF4A9EFF;
            if (ch == ']') in_link = 0;
            renderer_draw_glyph(r, px, py, color, 0x00000000,
                                &b->font[(unsigned char)ch]);
        }
    }
}

/* --------------------------------------------------------------------------
 * Combined vtable
 * -------------------------------------------------------------------------- */

extern bool window_node_on_event(node_t *self, const gui_event_t *e);

static const node_vtable_t browser_vtable = {
    .draw     = browser_draw,
    .on_event = window_node_on_event,
    .layout   = NULL,
    .destroy  = NULL,
};

/* --------------------------------------------------------------------------
 * Public constructor
 * -------------------------------------------------------------------------- */

node_t *gui_browser_create(const char *win_name, int x, int y, int w, int h) {
    node_t *win = window_node_create(win_name, x, y, w, h, "Browser");
    if (!win) return NULL;

    gui_browser_t *b = (gui_browser_t *)kmalloc(sizeof(gui_browser_t));
    if (!b) return win;
    memset(b, 0, sizeof(gui_browser_t));
    b->win_node = win;

    /* Default address */
    strcpy(b->addr, "https://tls.test:4430/");
    b->addr_len = (int)strlen(b->addr);
    b->status[0] = '\0';

    /* Initial document placeholder */
    strcpy(s_text, "Digite uma URL na barra acima e pressione Enter.\n");
    browser_wrap(s_text);

    /* Store state in the singleton used by draw() */
    win->vtable = &browser_vtable;
    g_browser_active = b;

    window_node_set_key_handler(win, browser_key_down, browser_key_char, b);

    strcpy(b->status, "Carregando pagina inicial...");

    /* Auto-load the default page. The fetch runs as a task: blocking on
     * DNS/TCP/TLS inside gui_init would freeze boot, and the NIC RX ring is
     * only serviced reliably once other tasks are scheduled. */
    if (net_get_list()) {
        extern int create_task_named(void (*entry_point)(), const char *name);
        static int boot_fetch_spawned = 0;
        if (!boot_fetch_spawned) {
            boot_fetch_spawned = 1;
            extern void browser_boot_fetch_task(void);
            create_task_named(browser_boot_fetch_task, "webboot");
        }
    } else {
        strcpy(b->status, "Rede indisponivel.");
    }

    return win;
}

/* Task body: fetch the start page once the scheduler is up (RX servicing
 * works), then idle forever. */
void browser_boot_fetch_task(void) {
    extern void switch_task(void);
    extern void task_sleep_ms(uint32_t ms);

    gui_browser_t *b = g_browser_active;
    if (b) browser_load(b);

    for (;;) task_sleep_ms(60000);
}

/* --------------------------------------------------------------------------
 * App entry
 * -------------------------------------------------------------------------- */

static void browser_app_start(void) {
    if (!g_scene || !g_scene->root) return;

    node_t *win = gui_browser_create("browser_win", 120, 80, 800, 500);
    if (win) {
        node_add_child(g_scene->root, win);
        layout_engine_compute(win);

        if (g_focus_manager) focus_manager_set_focus(g_focus_manager, win);
        window_manager_bring_to_front(win);
    }
}

void app_browser_init(void) {
    extern void app_registry_add(const char *name, const char *icon,
                                 void (*start)(void));
    app_registry_add("Browser", NULL, browser_app_start);
}

/* Auto-open the browser as the OS start page when a NIC is present. */
void browser_auto_start(void) {
    if (!net_get_list()) return;
    browser_app_start();
}