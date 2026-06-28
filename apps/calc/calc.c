#include <libliw.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../editor_nano/font.h"

#define FW 8
#define FH 16

typedef struct { uint8_t sc; int pr; } key_ev_t;

static liw_fb_info_t fb;
static int W, H;
static uint32_t *buf;

static char disp[32];
static double acc, cur;
static char op;
static int fresh;

typedef struct { const char *lbl; int x, y, w, h; uint32_t col, colh; } btn_t;

static btn_t btns[19];
static int nbtns, sel;

static void ftoa(double v, char *out, int max) {
    if (v < 0) { *out++ = '-'; v = -v; }
    long long ip = (long long)v;
    double fp = v - (double)ip;
    char buf[32]; int i = 0;
    if (ip == 0) buf[i++] = '0';
    else { while (ip > 0) { buf[i++] = '0' + (ip % 10); ip /= 10; } }
    int j;
    for (j = 0; j < i && j < max - 1; j++) out[j] = buf[i - 1 - j];
    int p = j;
    if (fp > 0.0000001 && p < max - 2) {
        out[p++] = '.';
        for (int k = 0; k < 6 && p < max - 1; k++) {
            fp *= 10; int d = (int)fp;
            out[p++] = '0' + d; fp -= d;
            if (fp < 0.0000001) break;
        }
    }
    out[p] = '\0';
}

static void pushc(char c) {
    size_t len = strlen(disp);
    if (len < 27) { memmove(disp + 1, disp, len + 1); disp[0] = c; }
}

static void click_num(int n) {
    if (fresh || disp[0] == '0') { disp[0] = '0' + n; disp[1] = '\0'; fresh = 0; }
    else pushc('0' + n);
}

static void click_dot(void) {
    for (size_t i = 0; disp[i]; i++) if (disp[i] == '.') return;
    if (fresh) { disp[0] = '0'; disp[1] = '.'; disp[2] = '\0'; fresh = 0; }
    else pushc('.');
}

static void click_eq(void);

static void click_op(char c) {
    if (op) click_eq();
    acc = atof(disp);
    op = c; fresh = 1;
}

static void click_eq(void) {
    if (!op) return;
    cur = atof(disp);
    switch (op) {
        case '+': acc += cur; break;
        case '-': acc -= cur; break;
        case '*': acc *= cur; break;
        case '/': acc = (cur != 0) ? acc / cur : 0; break;
    }
    op = 0; memset(disp, 0, 32); ftoa(acc, disp, 30); fresh = 1;
}

static void click_clr(void) {
    memset(disp, 0, 32); disp[0] = '0';
    acc = 0; cur = 0; op = 0; fresh = 1;
}

static void click_bs(void) {
    size_t len = strlen(disp);
    if (len > 1) disp[len - 1] = '\0';
    else { disp[0] = '0'; disp[1] = '\0'; fresh = 1; }
}

static void click_btn(int i) {
    const char *l = btns[i].lbl;
    if (!l || !l[0]) return;
    char c = l[0];
    if (c >= '0' && c <= '9') click_num(c - '0');
    else if (c == '.') click_dot();
    else if (c == '+' || c == '-' || c == '*' || c == '/') click_op(c);
    else if (c == '=') click_eq();
    else if (c == 'C') click_clr();
    else if (c == '<' || c == 'B') click_bs();
}

static void btn_setup(void) {
    int bx = 20, by = 80, bw = 55, bh = 40, g = 5;
    int i = 0;
    nbtns = 19;

    #define ADDB(tl,xx,yy,ww,hh,cc,ch) do{ btns[i].lbl=(tl); btns[i].x=(xx); btns[i].y=(yy); btns[i].w=(ww); btns[i].h=(hh); btns[i].col=(cc); btns[i].colh=(ch); i++; }while(0)

    ADDB("C",  bx+0*(bw+g), by, bw, bh, 0xCC3333,0xEE5555);
    ADDB("BS", bx+1*(bw+g), by, bw, bh, 0xCC8800,0xEEAA00);
    ADDB("%",  bx+2*(bw+g), by, bw, bh, 0xCC8800,0xEEAA00);
    ADDB("/",  bx+3*(bw+g), by, bw, bh, 0xCC8800,0xEEAA00);

    by += bh + g;
    ADDB("7",  bx+0*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("8",  bx+1*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("9",  bx+2*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("*",  bx+3*(bw+g), by, bw, bh, 0xCC8800,0xEEAA00);

    by += bh + g;
    ADDB("4",  bx+0*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("5",  bx+1*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("6",  bx+2*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("-",  bx+3*(bw+g), by, bw, bh, 0xCC8800,0xEEAA00);

    by += bh + g;
    ADDB("1",  bx+0*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("2",  bx+1*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("3",  bx+2*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("+",  bx+3*(bw+g), by, bw, bh, 0xCC8800,0xEEAA00);

    by += bh + g;
    ADDB("0",  bx+0*(bw+g), by, bw*2+g, bh, 0x555555,0x777777);
    ADDB(".",  bx+2*(bw+g), by, bw, bh, 0x555555,0x777777);
    ADDB("=",  bx+3*(bw+g), by, bw, bh, 0x0066CC,0x0088EE);
}

static void draw_c(int x, int y, unsigned char c, uint32_t fg, uint32_t bg) {
    unsigned char *g = &font_data[c * 16];
    for (int r = 0; r < 16 && y + r < H; r++) {
        unsigned char bits = g[r];
        for (int cl = 0; cl < 8 && x + cl < W; cl++)
            buf[(y + r) * W + (x + cl)] = (bits & (0x80 >> cl)) ? fg : bg;
    }
}

static void draw_s(int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    for (int i = 0; s[i]; i++) {
        if (x + FW * (i + 1) > W) break;
        draw_c(x + i * FW, y, (unsigned char)s[i], fg, bg);
    }
}

static void draw_rect(int x, int y, int w, int h, uint32_t c) {
    if (x < 0) { w += x; x = 0; } if (y < 0) { h += y; y = 0; }
    if (x >= W || y >= H) return;
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    for (int r = y; r < y + h; r++)
        for (int cl = x; cl < x + w; cl++)
            buf[r * W + cl] = c;
}

static void render(void) {
    int i;
    for (i = 0; i < W * H; i++) buf[i] = 0xEE333333;

    draw_rect(15, 15, W - 30, 55, 0x111111);
    char tmp[64]; int n = 0;
    size_t dlen = strlen(disp);
    if (op && fresh) {
        char af[32]; ftoa(acc, af, 30);
        int z = strlen(af);
        for (i = z - 1; i >= 0; i--) tmp[n++] = af[i];
        tmp[n++] = op;
    }
    if (!fresh || !op) {
        for (i = (int)dlen - 1; i >= 0; i--) tmp[n++] = disp[i];
    }
    tmp[n] = '\0';

    int tw = n * FW;
    int dx = (W - 30 - tw > 20) ? W - 30 - tw - 5 : 20;
    draw_s(dx, 25, tmp, 0xFFFFFF, 0x111111);

    for (i = 0; i < nbtns; i++) {
        uint32_t c = (i == sel) ? btns[i].colh : btns[i].col;
        draw_rect(btns[i].x, btns[i].y, btns[i].w, btns[i].h, c);
        int lw = strlen(btns[i].lbl) * FW;
        int lx = btns[i].x + (btns[i].w - lw) / 2;
        int ly = btns[i].y + (btns[i].h - FH) / 2;
        draw_s(lx, ly, btns[i].lbl, 0xFFFFFF, c);
    }

    liw_present_frame(buf, W, H);
}

static int key_to_btn(int sc) {
    switch (sc) {
        case 0x0B: return 0;
        case 0x02: return 4; case 0x03: return 5; case 0x04: return 6;
        case 0x05: return 7; case 0x06: return 8; case 0x07: return 9;
        case 0x08: return 10; case 0x09: return 11; case 0x0A: return 12;
        case 0x0C: return 15;
        case 0x0D: return 1;
        case 0x1C: return -10;
        case 0x39: return -10;
        case 0x0E: return -2;
        case 0xCB: return -3; case 0xCD: return -4;
        case 0xC8: return -5; case 0xD0: return -6;
        default: return -1;
    }
}

int main(void) {
    liw_get_fb_info(&fb);
    W = fb.width; H = fb.height;
    buf = malloc(W * H * 4);
    if (!buf) return 1;

    disp[0] = '0'; disp[1] = '\0';
    fresh = 1;
    btn_setup();

    int ctrl = 0;
    render();
    while (1) {
        key_ev_t ev;
        if (liw_get_key_event(&ev) && ev.pr) {
            if (ev.sc == 0x1D) { ctrl = 1; continue; }
            if (ev.sc == 0x9D) { ctrl = 0; continue; }
            if (ctrl && ev.sc == 0x2E) break;
            int bi = key_to_btn(ev.sc);
            if (bi >= 0 && bi < nbtns) { sel = bi; click_btn(bi); }
            else if (bi == -10) { click_btn(sel); }
            else if (bi == -2) click_bs();
            else if (bi == -3 && sel > 0) sel--;
            else if (bi == -4 && sel < nbtns - 1) sel++;
            else if (bi == -5 && sel >= 4) sel -= 4;
            else if (bi == -6 && sel + 4 < nbtns) sel += 4;
            render();
        }
    }
    return 0;
}
