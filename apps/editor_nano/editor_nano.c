#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libliw.h>
#include "font.h"

#define FW 8
#define FH 16
#define LN_W 5
#define MAX_LINES 8192
#define MAX_LINE 4096
#define TAB_W 4
#define MAX_FILE_SIZE (4 * 1024 * 1024)

typedef struct {
    uint8_t scancode;
    int pressed;
} key_event_t;

static liw_fb_info_t fb;
static int scrn_w, scrn_h;
static uint32_t *buf;

static char **lines;
static int nlines;
static int cx, cy, scx, scy;
static int modified;
static char filename[256];
static int running;

static void draw_c(int x, int y, unsigned char c, uint32_t fg, uint32_t bg) {
    unsigned char *g = &font_data[c * 16];
    for (int row = 0; row < 16 && y + row < scrn_h; row++) {
        unsigned char bits = g[row];
        for (int col = 0; col < 8 && x + col < scrn_w; col++)
            buf[(y + row) * scrn_w + (x + col)] = (bits & (0x80 >> col)) ? fg : bg;
    }
}

static void draw_s(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    for (int i = 0; s[i]; i++) {
        if (x + FW * (i + 1) > scrn_w) break;
        draw_c(x + i * FW, y, (unsigned char)s[i], fg, bg);
    }
}

static void clear_rect(int x, int y, int w, int h, uint32_t color) {
    for (int r = 0; r < h && y + r < scrn_h; r++)
        for (int c = 0; c < w && x + c < scrn_w; c++)
            buf[(y + r) * scrn_w + (x + c)] = color;
}

static void editor_load(const char *path) {
    if (path) {
        strncpy(filename, path, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = 0;
    }
    for (int i = 0; i < nlines; i++) free(lines[i]);
    nlines = 0;
    lines[0] = strdup("");
    nlines = 1;
    cx = cy = scx = scy = 0;
    modified = 0;
    if (!path) return;

    int fd = open(path, O_RDONLY);
    if (fd < 0) return;
    char *tmp = malloc(MAX_FILE_SIZE);
    if (!tmp) return;
    int n = read(fd, tmp, MAX_FILE_SIZE - 1);
    close(fd);
    if (n <= 0) { free(tmp); return; }
    tmp[n] = 0;

    for (int i = 0; i < nlines; i++) free(lines[i]);
    nlines = 0;

    char *p = tmp;
    while (*p && nlines < MAX_LINES) {
        char *nl = strchr(p, '\n');
        int len = nl ? (int)(nl - p) : (int)strlen(p);
        if (len > MAX_LINE) len = MAX_LINE;
        lines[nlines] = malloc(len + 1);
        memcpy(lines[nlines], p, len);
        lines[nlines][len] = 0;
        nlines++;
        if (!nl) break;
        p = nl + 1;
    }
    free(tmp);
    if (nlines == 0) {
        lines[0] = strdup("");
        nlines = 1;
    }
    modified = 0;
}

extern int __liw_sys_save_file(const char *name, void *buffer, uint32_t size);

static void editor_save(void) {
    if (!filename[0]) return;

    uint32_t total = 0;
    for (int i = 0; i < nlines; i++)
        total += strlen(lines[i]) + 1;

    char *content = malloc(total + 1);
    if (!content) return;
    int pos = 0;
    for (int i = 0; i < nlines; i++) {
        int len = strlen(lines[i]);
        memcpy(content + pos, lines[i], len);
        pos += len;
        content[pos++] = '\n';
    }
    if (total > 0) content[total - 1] = 0;
    else content[0] = 0;

    int ret = __liw_sys_save_file(filename, content, total > 0 ? total - 1 : 0);
    free(content);
    if (ret >= 0) modified = 0;
}

static void ins_c(int c) {
    int len = strlen(lines[cy]);
    if (len >= MAX_LINE - 1) return;
    memmove(lines[cy] + cx + 1, lines[cy] + cx, len - cx + 1);
    lines[cy][cx] = c;
    cx++;
    modified = 1;
}

static void ins_nl(void) {
    if (nlines >= MAX_LINES) return;
    int len = strlen(lines[cy]);
    int rest = len - cx;
    char *next = malloc(rest + 1);
    memcpy(next, lines[cy] + cx, rest + 1);
    lines[cy][cx] = 0;
    for (int i = nlines; i > cy + 1; i--)
        lines[i] = lines[i - 1];
    lines[cy + 1] = next;
    nlines++;
    cy++;
    cx = 0;
    modified = 1;
}

static void del_c(void) {
    int len = strlen(lines[cy]);
    if (cx < len) {
        memmove(lines[cy] + cx, lines[cy] + cx + 1, len - cx);
        modified = 1;
    } else if (cy < nlines - 1) {
        char *next = lines[cy + 1];
        int nlen = strlen(next);
        if (len + nlen >= MAX_LINE) return;
        memcpy(lines[cy] + len, next, nlen + 1);
        free(next);
        for (int i = cy + 1; i < nlines - 1; i++)
            lines[i] = lines[i + 1];
        nlines--;
        modified = 1;
    }
}

static void bs_c(void) {
    if (cx > 0) {
        cx--;
        memmove(lines[cy] + cx, lines[cy] + cx + 1, strlen(lines[cy]) - cx);
        modified = 1;
    } else if (cy > 0) {
        int plen = strlen(lines[cy - 1]);
        int len = strlen(lines[cy]);
        if (plen + len >= MAX_LINE) return;
        memcpy(lines[cy - 1] + plen, lines[cy], len + 1);
        free(lines[cy]);
        for (int i = cy; i < nlines - 1; i++)
            lines[i] = lines[i + 1];
        nlines--;
        cy--;
        cx = plen;
        modified = 1;
    }
}

static void render(void) {
    uint32_t bg       = 0x001E1E1E;
    uint32_t fg       = 0x00D4D4D4;
    uint32_t ln_bg    = 0x00252525;
    uint32_t ln_fg    = 0x00585858;
    uint32_t tbar_bg  = 0x00333333;
    uint32_t tbar_fg  = 0x00CCCCCC;
    uint32_t sbar_bg  = 0x00007ACC;
    uint32_t sbar_fg  = 0x00FFFFFF;
    uint32_t cur_col  = 0x00FFFFFF;
    uint32_t mod_col  = 0x00E0A000;

    int tbar_h = FH + 4;
    int sbar_h = FH + 4;
    int text_y = tbar_h;
    int text_h = scrn_h - tbar_h - sbar_h;
    int vis_rows = text_h / FH;
    int ln_w = LN_W * FW;

    clear_rect(0, 0, scrn_w, scrn_h, bg);
    clear_rect(0, 0, scrn_w, tbar_h, tbar_bg);

    char title[320];
    snprintf(title, sizeof(title), " LiwusEdit  -  %s%s",
             filename[0] ? filename : "(Novo Arquivo)",
             modified ? " [MODIFICADO]" : "");
    draw_s(4, 2, title, tbar_fg, tbar_bg);

    if (scy > cy) scy = cy;
    if (scy > 0 && cy >= scy + vis_rows) scy = cy - vis_rows + 1;

    for (int r = 0; r < vis_rows && scy + r < nlines; r++) {
        int ly = text_y + r * FH;
        int li = scy + r;
        int line_len = strlen(lines[li]);

        char ln[16];
        snprintf(ln, sizeof(ln), "%*d", LN_W - 1, li + 1);
        clear_rect(0, ly, ln_w, FH, ln_bg);
        draw_s(2, ly, ln, ln_fg, ln_bg);

        int tx = ln_w;
        int start = scx;
        int avail = (scrn_w - tx) / FW;
        if (avail > line_len - start) avail = line_len - start;

        for (int c = 0; c < avail; c++) {
            unsigned char ch = (unsigned char)lines[li][start + c];
            if (ch < 32) ch = ' ';
            draw_c(tx + c * FW, ly, ch, fg, bg);
        }
        int rem_w = scrn_w - tx - avail * FW;
        if (rem_w > 0) clear_rect(tx + avail * FW, ly, rem_w, FH, bg);
    }

    int cur_x = ln_w + (cx - scx) * FW;
    int cur_y = text_y + (cy - scy) * FH;
    if (cy >= scy && cy < scy + vis_rows && cur_x < scrn_w)
        draw_c(cur_x, cur_y, ' ', cur_col, cur_col);

    clear_rect(0, scrn_h - sbar_h, scrn_w, sbar_h, sbar_bg);
    char status[320];
    snprintf(status, sizeof(status), " ^S Salvar   ^Q Sair     Lin: %d  Col: %d  (%d linhas)",
             cy + 1, cx + 1, nlines);
    draw_s(4, scrn_h - sbar_h + 2, status, sbar_fg, sbar_bg);

    if (modified)
        draw_s(scrn_w - 80, scrn_h - sbar_h + 2, "[MODIFICADO]", mod_col, sbar_bg);
}

static int is_shift(void) { return liw_key_down(0x2A) || liw_key_down(0x36); }
static int is_ctrl(void)  { return liw_key_down(0x1D); }

static int sc_to_c(int sc, int shifted_flag) {
    static const char tbl_norm[128] = {
        0,27,'1','2','3','4','5','6','7','8','9','0','-','=',8,9,
        'q','w','e','r','t','y','u','i','o','p','[',']',13,0,
        'a','s','d','f','g','h','j','k','l',';','\'','`',0,
        '\\','z','x','c','v','b','n','m',',','.','/',0,'*',0,' '
    };
    static const char tbl_sh[128] = {
        0,27,'!','@','#','$','%','^','&','*','(',')','_','+',8,9,
        'Q','W','E','R','T','Y','U','I','O','P','{','}',13,0,
        'A','S','D','F','G','H','J','K','L',':','"','~',0,
        '|','Z','X','C','V','B','N','M','<','>','?',0,'*',0,' '
    };
    if (sc < 0 || sc >= 128) return 0;
    return shifted_flag ? tbl_sh[sc] : tbl_norm[sc];
}

static void h_key(int sc, int pressed) {
    if (!pressed) return;

    if (is_ctrl()) {
        if (sc == 0x1F) { editor_save(); return; }
        if (sc == 0x10) { running = 0; return; }
        return;
    }

    if (sc == 0x1C) { ins_nl(); return; }
    if (sc == 0x0E) { bs_c(); return; }
    if (sc == 0x53) { del_c(); return; }

    if (sc == 0x48) { if (cy > 0) { cy--; if (cx > (int)strlen(lines[cy])) cx = strlen(lines[cy]); } return; }
    if (sc == 0x50) { if (cy < nlines - 1) { cy++; if (cx > (int)strlen(lines[cy])) cx = strlen(lines[cy]); } return; }
    if (sc == 0x4B) { if (cx > 0) cx--; return; }
    if (sc == 0x4D) { if (cx < (int)strlen(lines[cy])) cx++; return; }

    if (sc == 0x47) { cx = 0; return; }
    if (sc == 0x4F) { cx = strlen(lines[cy]); return; }
    if (sc == 0x49) { cy -= 20; if (cy < 0) cy = 0; return; }
    if (sc == 0x51) { cy += 20; if (cy >= nlines) cy = nlines - 1; return; }
    if (sc == 0x0F) { for (int i = 0; i < TAB_W; i++) ins_c(' '); return; }

    int ch = sc_to_c(sc, is_shift());
    if (ch >= 32) ins_c(ch);
}

static void wait_confirm(void) {
    clear_rect(0, scrn_h / 2 - FH, scrn_w, FH * 3, 0x00330000);
    draw_s(20, scrn_h / 2, "Arquivo nao salvo! Ctrl+S salva, ESC para ignorar",
             0x00FF6666, 0x00330000);
    while (1) {
        key_event_t ev;
        if (liw_get_key_event(&ev) && ev.pressed) {
            if (ev.scancode == 0x01) return;
            if (is_ctrl() && ev.scancode == 0x1F) { editor_save(); return; }
        }
    }
}

int main(int argc, char **argv) {
    liw_get_fb_info(&fb);
    scrn_w = fb.width;
    scrn_h = fb.height;
    buf = fb.address;

    lines = calloc(MAX_LINES, sizeof(char *));
    lines[0] = strdup("");

    if (argc > 1) editor_load(argv[1]);
    running = 1;
    int dirty = 1;

    while (running) {
        if (dirty) {
            render();
            dirty = 0;
        }
        key_event_t ev;
        if (liw_get_key_event(&ev)) {
            h_key(ev.scancode, ev.pressed);
            dirty = 1;
        }
    }

    if (modified) wait_confirm();

    for (int i = 0; i < nlines; i++) free(lines[i]);
    free(lines);
    return 0;
}
