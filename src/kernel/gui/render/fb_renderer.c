/*
 * gui/render/fb_renderer.c
 *
 * Software Framebuffer backend for gui_renderer_t.
 *
 * Reads from the VRAM globals set up by vga.c / multiboot2, allocates a
 * private back-buffer, and does a fast memcpy on present().
 *
 * This is the ONLY file that touches raw VRAM pointers.
 * All higher-level code goes through renderer_*.
 */
#include "renderer.h"
#include "fb_renderer.h"
#include "kheap.h"
#include "string.h"

/* Globals exposed by vga.c */
extern uint64_t  vga_fb_addr;
extern uint32_t  vga_fb_width;
extern uint32_t  vga_fb_height;
extern uint32_t  vga_fb_pitch;

/* --------------------------------------------------------------------------
 * Backend state
 * -------------------------------------------------------------------------- */

typedef struct {
    uint32_t *vram;         /* physical framebuffer */
    uint32_t *backbuf;      /* our back buffer      */
    uint32_t  pitch_px;     /* pitch in uint32_t units */
    uint32_t  width;
    uint32_t  height;
    uint32_t  buf_bytes;
} fb_state_t;

/* --------------------------------------------------------------------------
 * Alpha blend helper
 * -------------------------------------------------------------------------- */

static inline uint32_t alpha_blend(uint32_t bg, uint32_t fg) {
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 0xFF) return fg;
    if (a == 0x00) return bg;
    uint32_t inv = 255 - a;
    uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv) >> 8;
    uint32_t g = (((fg >>  8) & 0xFF) * a + ((bg >>  8) & 0xFF) * inv) >> 8;
    uint32_t b = (( fg        & 0xFF) * a + ( bg        & 0xFF) * inv) >> 8;
    return (0xFF000000) | (r << 16) | (g << 8) | b;
}

/* --------------------------------------------------------------------------
 * Clip helper
 * -------------------------------------------------------------------------- */

static inline gui_rect_t fb_clip(fb_state_t *s, gui_renderer_t *r, gui_rect_t rect) {
    gui_rect_t screen = rect_make(0, 0, (int)s->width, (int)s->height);
    gui_rect_t clipped = rect_intersection(rect, screen);
    if (!rect_is_empty(r->clip)) {
        clipped = rect_intersection(clipped, r->clip);
    }
    return clipped;
}

/* --------------------------------------------------------------------------
 * ops implementations
 * -------------------------------------------------------------------------- */

static void fb_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color) {
    fb_state_t *s = (fb_state_t *)r->backend;
    gui_rect_t c = fb_clip(s, r, rect);
    if (rect_is_empty(c)) return;

    uint8_t a = (color >> 24) & 0xFF;
    for (int y = c.y; y < c.y + c.height; y++) {
        uint32_t *row = s->backbuf + y * s->pitch_px;
        if (a == 0xFF) {
            for (int x = c.x; x < c.x + c.width; x++) row[x] = color;
        } else {
            for (int x = c.x; x < c.x + c.width; x++)
                row[x] = alpha_blend(row[x], color);
        }
    }
}

static void fb_draw_rect(gui_renderer_t *r, gui_rect_t rect,
                           uint32_t color, int thickness) {
    if (thickness <= 0) return;
    /* Top, bottom, left, right strips */
    fb_fill_rect(r, rect_make(rect.x, rect.y, rect.width, thickness), color);
    fb_fill_rect(r, rect_make(rect.x, rect.y + rect.height - thickness,
                                rect.width, thickness), color);
    fb_fill_rect(r, rect_make(rect.x, rect.y, thickness, rect.height), color);
    fb_fill_rect(r, rect_make(rect.x + rect.width - thickness, rect.y,
                                thickness, rect.height), color);
}

static void fb_blit(gui_renderer_t *r,
                     int dest_x, int dest_y,
                     const uint32_t *src, int src_w, int src_h, int src_pitch,
                     int sx, int sy, int cw, int ch) {
    fb_state_t *s = (fb_state_t *)r->backend;

    /* Clip dest rect to back-buffer (and optional clip rect) */
    gui_rect_t dest_rect = fb_clip(s, r, rect_make(dest_x, dest_y, cw, ch));
    if (rect_is_empty(dest_rect)) return;

    int dx_off = dest_rect.x - dest_x;
    int dy_off = dest_rect.y - dest_y;

    int src_pitch_px = src_pitch / (int)sizeof(uint32_t);

    for (int y = 0; y < dest_rect.height; y++) {
        const uint32_t *src_row = src + (sy + dy_off + y) * src_pitch_px + sx + dx_off;
        uint32_t       *dst_row = s->backbuf + (dest_rect.y + y) * s->pitch_px + dest_rect.x;
        for (int x = 0; x < dest_rect.width; x++) {
            uint32_t p = src_row[x];
            uint8_t  a = p >> 24;
            if (a == 0xFF) dst_row[x] = p;
            else if (a > 0) dst_row[x] = alpha_blend(dst_row[x], p);
        }
    }
    (void)src_w; (void)src_h;
}

static void fb_blit_scaled(gui_renderer_t *r,
                             int dest_x, int dest_y,
                             const uint32_t *src, int src_w, int src_h,
                             float scale_x, float scale_y) {
    fb_state_t *s = (fb_state_t *)r->backend;

    int scaled_w = (int)((float)src_w * scale_x);
    int scaled_h = (int)((float)src_h * scale_y);

    gui_rect_t clip = fb_clip(s, r, rect_make(dest_x, dest_y, scaled_w, scaled_h));
    if (rect_is_empty(clip)) return;

    for (int y = clip.y; y < clip.y + clip.height; y++) {
        int sy = (int)((float)(y - dest_y) / scale_y);
        if (sy < 0 || sy >= src_h) continue;
        const uint32_t *src_row = src + sy * src_w;
        uint32_t       *dst_row = s->backbuf + y * s->pitch_px;
        for (int x = clip.x; x < clip.x + clip.width; x++) {
            int sx = (int)((float)(x - dest_x) / scale_x);
            if (sx < 0 || sx >= src_w) continue;
            uint32_t p = src_row[sx];
            uint8_t  a = p >> 24;
            if (a == 0xFF) dst_row[x] = p;
            else if (a > 0) dst_row[x] = alpha_blend(dst_row[x], p);
        }
    }
}

static void fb_draw_glyph(gui_renderer_t *r, int x, int y,
                            uint32_t fg, uint32_t bg, const glyph_t *g) {
    fb_state_t *s = (fb_state_t *)r->backend;
    if (!g || !g->bitmap) return;

    for (int row = 0; row < g->cell_h; row++) {
        uint8_t bits = g->bitmap[row];
        uint32_t *dst_row = s->backbuf + (y + row) * s->pitch_px;
        for (int col = 0; col < g->cell_w && col < 8; col++) {
            int px = x + col;
            if (px < 0 || px >= (int)s->width) continue;
            if (y + row < 0 || y + row >= (int)s->height) continue;
            if (bits & (0x80 >> col)) {
                dst_row[px] = fg;
            } else if (bg) {
                dst_row[px] = bg;
            }
        }
    }
}

static void fb_set_clip(gui_renderer_t *r, gui_rect_t clip) {
    r->clip = clip;
}

static void fb_set_opacity(gui_renderer_t *r, float opacity) {
    r->opacity = opacity;
}

static void fb_present(gui_renderer_t *r) {
    fb_state_t *s = (fb_state_t *)r->backend;
    /* Fast copy: back-buffer → physical VRAM */
    uint32_t bytes = vga_fb_pitch * s->height;
    
    extern void fast_memcpy(void *dst, const void *src, uint64_t n);
    fast_memcpy(s->vram, s->backbuf, bytes);
}

static void fb_destroy(gui_renderer_t *r) {
    fb_state_t *s = (fb_state_t *)r->backend;
    if (s) {
        if (s->backbuf) kfree(s->backbuf);
        kfree(s);
    }
}

/* --------------------------------------------------------------------------
 * Ops table (one shared instance for all fb renderers)
 * -------------------------------------------------------------------------- */

static const renderer_ops_t fb_ops = {
    .fill_rect   = fb_fill_rect,
    .draw_rect   = fb_draw_rect,
    .blit        = fb_blit,
    .blit_scaled = fb_blit_scaled,
    .draw_glyph  = fb_draw_glyph,
    .set_clip    = fb_set_clip,
    .set_opacity = fb_set_opacity,
    .present     = fb_present,
    .destroy     = fb_destroy,
};

/* --------------------------------------------------------------------------
 * Public constructor
 * -------------------------------------------------------------------------- */

gui_renderer_t *fb_renderer_create(void) {
    if (!vga_fb_addr) return NULL;

    fb_state_t *s = (fb_state_t *)kmalloc(sizeof(fb_state_t));
    if (!s) return NULL;
    memset(s, 0, sizeof(fb_state_t));

    s->vram      = (uint32_t *)vga_fb_addr;
    s->width     = vga_fb_width;
    s->height    = vga_fb_height;
    s->pitch_px  = vga_fb_pitch / sizeof(uint32_t);
    s->buf_bytes = vga_fb_pitch * s->height;

    s->backbuf = (uint32_t *)kmalloc(s->buf_bytes);
    if (!s->backbuf) { kfree(s); return NULL; }
    memset(s->backbuf, 0, s->buf_bytes);

    return renderer_create(&fb_ops, s, (int)s->width, (int)s->height);
}

uint32_t *fb_renderer_backbuf(gui_renderer_t *r) {
    if (!r) return NULL;
    return ((fb_state_t *)r->backend)->backbuf;
}
