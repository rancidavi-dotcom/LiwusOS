/*
 * gui/render/compositor.c  —  Compositor corrigido
 *
 * Correções desta versão:
 *  1. Ghost cursors: backbuffer é limpo totalmente antes de cada frame.
 *     (Full repaint toda frame — dirty-rect otimization fica para a Fase 4)
 *  2. Cursor sprite: pixels originais são salvos e restaurados antes de
 *     reposicionar o cursor, eliminando rastros.
 */
#include "compositor.h"
#include "kheap.h"
#include "string.h"
#include "task.h"
#include "fb_renderer.h"
#include "../core/theme_engine.h"
#include "../core/animation_engine.h"
#include "../core/taskbar.h"
#include "../assets/asset_manager.h"
#include <drivers/serial.h>
#include "vga.h"

compositor_t *g_compositor = NULL;

/* ---- Profiling Metrics (Cycles) ---- */
static uint64_t perf_input_cycles  = 0;
static uint64_t perf_event_cycles  = 0;
static uint64_t perf_render_cycles = 0;
static uint64_t perf_blit_cycles   = 0;
static uint64_t perf_total_cycles  = 0;

static inline uint64_t rdtsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

compositor_t *compositor_create(gui_renderer_t  *renderer,
                                  camera_t        *camera,
                                  gui_event_bus_t *bus,
                                  input_manager_t *input,
                                  node_t          *scene_root) {
    compositor_t *c = (compositor_t *)kmalloc(sizeof(compositor_t));
    if (!c) return NULL;
    memset(c, 0, sizeof(compositor_t));

    c->renderer    = renderer;
    c->camera      = camera;
    c->bus         = bus;
    c->input       = input;
    c->scene_root  = scene_root;
    c->full_redraw = true;

    /* cursor sprite state */
    c->cursor_x      = -1;
    c->cursor_y      = -1;
    c->cursor_saved  = false;

    g_compositor = c;
    return c;
}

void compositor_destroy(compositor_t *c) {
    if (!c) return;
    if (g_compositor == c) g_compositor = NULL;
    kfree(c);
}

/* --------------------------------------------------------------------------
 * Dirty rects
 * -------------------------------------------------------------------------- */

void compositor_invalidate(compositor_t *c, const gui_rect_t *rect) {
    if (!c || !rect || rect_is_empty(*rect)) return;
    /* Phase 1: we always do full redraws, so just set the flag. */
    c->full_redraw = true;
}

void compositor_invalidate_full(compositor_t *c) {
    if (c) c->full_redraw = true;
}

/* --------------------------------------------------------------------------
 * Background (solid teal — Windows 95 desktop)
 * -------------------------------------------------------------------------- */

static void draw_background(compositor_t *c) {
    uint32_t *backbuf = fb_renderer_backbuf(c->renderer);
    if (!backbuf) return;

    int total = c->renderer->screen_w * c->renderer->screen_h;
    uint32_t bg_color = theme_engine_get_color(THEME_COLOR_BACKGROUND);

    for (int i = 0; i < total; i++) backbuf[i] = bg_color;
}

/* --------------------------------------------------------------------------
 * Cursor sprites — Chicago95 pixel cursors (32x32, with hotspot)
 * -------------------------------------------------------------------------- */

#include "../assets/pixel_cursors.h"

static void cursor_restore(compositor_t *c) {
    if (!c->cursor_saved || c->cursor_x < 0) return;
    uint32_t *backbuf = fb_renderer_backbuf(c->renderer);
    if (!backbuf) return;
    int W = c->renderer->screen_w;
    int H = c->renderer->screen_h;

    int i = 0;
    for (int row = 0; row < c->cursor_save_h; row++) {
        int py = c->cursor_y + row;
        if (py < 0 || py >= H) { i += c->cursor_save_w; continue; }
        for (int col = 0; col < c->cursor_save_w; col++) {
            int px = c->cursor_x + col;
            if (px >= 0 && px < W) {
                backbuf[py * W + px] = c->cursor_save_buf[i];
            }
            i++;
        }
    }
    c->cursor_saved = false;
}

static void cursor_draw(compositor_t *c, int mx, int my) {
    uint32_t *backbuf = fb_renderer_backbuf(c->renderer);
    if (!backbuf) return;
    int W = c->renderer->screen_w;
    int H = c->renderer->screen_h;

    if (c->cursor_type < 0 || c->cursor_type >= (gui_cursor_t)CURSOR_COUNT) {
        c->cursor_type = CURSOR_ARROW;
    }
    const pixel_cursor_t *s = &pixel_cursors[c->cursor_type];

    /* Native cursor coordinates (excluding hotspot) */
    int ox = mx - s->x_hot;
    int oy = my - s->y_hot;

    /* Save pixels under new cursor position */
    c->cursor_x = ox;
    c->cursor_y = oy;
    c->cursor_save_w = s->w;
    c->cursor_save_h = s->h;
    int i = 0;
    for (int row = 0; row < s->h; row++) {
        int py = oy + row;
        for (int col = 0; col < s->w; col++) {
            int px = ox + col;
            if (py >= 0 && py < H && px >= 0 && px < W) {
                c->cursor_save_buf[i] = backbuf[py * W + px];
            } else {
                c->cursor_save_buf[i] = 0;
            }
            i++;
        }
    }
    c->cursor_saved = true;

    /* Paint cursor (opaque pixels only) */
    for (int row = 0; row < s->h; row++) {
        int py = oy + row;
        if (py < 0 || py >= H) continue;
        for (int col = 0; col < s->w; col++) {
            int px = ox + col;
            if (px < 0 || px >= W) continue;
            uint32_t p = s->data[row * s->w + col];
            if ((p >> 24) != 0) {
                backbuf[py * W + px] = p;
            }
        }
    }
}

void compositor_set_cursor(compositor_t *c, gui_cursor_t type) {
    if (c && type >= 0 && type < CURSOR_COUNT) c->cursor_type = type;
}

static void draw_minimap(compositor_t *c) {
    int W = c->renderer->screen_w;
    int H = c->renderer->screen_h;
    
    // Minimap size and position
    int mm_w = 150;
    int mm_h = 100;
    int mm_x = W - mm_w - 20;
    int mm_y = H - mm_h - 20;
    
    // Background (semi-transparent)
    gui_rect_t bg = rect_make(mm_x, mm_y, mm_w, mm_h);
    renderer_fill_rect(c->renderer, bg, 0xCC252A33);
    renderer_draw_rect(c->renderer, bg, 0xFF3D4450, 1);
    
    // Map the current camera position to the minimap
    // Let's assume the "world" we care about is roughly -5000 to +5000 in both axes.
    int world_bounds = 5000; 
    
    // Convert camera world pos (fixed point) to integer
    int cam_world_x = c->camera->pos_x_fp / CAMERA_POS_SCALE;
    int cam_world_y = c->camera->pos_y_fp / CAMERA_POS_SCALE;
    
    // Normalize using integer math (* 1000 for precision)
    // Map -world_bounds..+world_bounds to 0..1000
    int nx = ((cam_world_x + world_bounds) * 1000) / (world_bounds * 2);
    int ny = ((cam_world_y + world_bounds) * 1000) / (world_bounds * 2);
    
    // Calculate dot position
    int dot_x = mm_x + (nx * mm_w) / 1000;
    int dot_y = mm_y + (ny * mm_h) / 1000;
    
    // Clamp to minimap bounds
    if (dot_x < mm_x) dot_x = mm_x;
    if (dot_x > mm_x + mm_w - 4) dot_x = mm_x + mm_w - 4;
    if (dot_y < mm_y) dot_y = mm_y;
    if (dot_y > mm_y + mm_h - 4) dot_y = mm_y + mm_h - 4;
    
    // Draw camera dot
    gui_rect_t dot = rect_make(dot_x, dot_y, 4, 4);
    renderer_fill_rect(c->renderer, dot, 0xFF4A9EFF); // Blue dot
}

/* --------------------------------------------------------------------------
 * CRT Scanlines Effect
 * -------------------------------------------------------------------------- */

static bool s_scanlines_enabled = false; /* off by default for max performance */

void compositor_set_scanlines(bool enabled) {
    s_scanlines_enabled = enabled;
}

bool compositor_get_scanlines(void) {
    return s_scanlines_enabled;
}

/* Precomputed darkening table: lut[v] = v * 4 / 5 (0..255 -> 0..204).
 * Avoids per-pixel multiply/divide in the scanline pass. */
static uint32_t s_scan_dark[256];

static void apply_scanlines(compositor_t *c) {
    if (!s_scanlines_enabled) return;
    uint32_t *backbuf = fb_renderer_backbuf(c->renderer);
    if (!backbuf) return;
    int W = c->renderer->screen_w;
    int H = c->renderer->screen_h;

    s_scan_dark[0] = 0;
    for (int i = 1; i < 256; i++) s_scan_dark[i] = (uint32_t)(i * 4 / 5);

    /* Every other line darkened by 20% via a precomputed LUT.
     * Pointer-walk + 4x unroll on X for the fastest scanline pass. */
    const uint32_t *lut = s_scan_dark;
    for (int y = 0; y < H; y += 2) {
        uint32_t *row = backbuf + y * W;
        int x = 0;
        for (; x + 4 <= W; x += 4) {
            uint32_t p0 = row[x];
            uint32_t p1 = row[x + 1];
            uint32_t p2 = row[x + 2];
            uint32_t p3 = row[x + 3];
            row[x]     = 0xFF000000 | (lut[(p0 >> 16) & 0xFF] << 16) | (lut[(p0 >> 8) & 0xFF] << 8) | lut[p0 & 0xFF];
            row[x + 1] = 0xFF000000 | (lut[(p1 >> 16) & 0xFF] << 16) | (lut[(p1 >> 8) & 0xFF] << 8) | lut[p1 & 0xFF];
            row[x + 2] = 0xFF000000 | (lut[(p2 >> 16) & 0xFF] << 16) | (lut[(p2 >> 8) & 0xFF] << 8) | lut[p2 & 0xFF];
            row[x + 3] = 0xFF000000 | (lut[(p3 >> 16) & 0xFF] << 16) | (lut[(p3 >> 8) & 0xFF] << 8) | lut[p3 & 0xFF];
        }
        for (; x < W; x++) {
            uint32_t p = row[x];
            row[x] = 0xFF000000 | (lut[(p >> 16) & 0xFF] << 16) | (lut[(p >> 8) & 0xFF] << 8) | lut[p & 0xFF];
        }
    }
}

/* --------------------------------------------------------------------------
 * Profiler Overlay
 * -------------------------------------------------------------------------- */

static void draw_text_simple(compositor_t *c, int x, int y, const char *str, uint32_t color) {
    const glyph_t *font = asset_manager_get_font(NULL);
    if (!font) return;
    int cur_x = x;
    while (*str) {
        char ch = *str++;
        if (ch >= 32 && ch <= 126) {
            /* 10 bytes per glyph in this basic font assuming 8x10 or so.
               Actually the renderer_draw_glyph handles it by passing the pointer.
               We just need a temporary glyph_t with the correct bitmap. */
            glyph_t g;
            g.bitmap = font->bitmap + (ch * font->cell_h);
            g.cell_w = font->cell_w;
            g.cell_h = font->cell_h;
            renderer_draw_glyph(c->renderer, cur_x, y, color, 0, &g);
        }
        cur_x += font->cell_w;
    }
}

static void draw_profiler_overlay(compositor_t *c) {
    int sx = 10, sy = 10;
    int line_h = 16;
    
    gui_rect_t bg = rect_make(sx - 5, sy - 5, 250, 100);
    renderer_fill_rect(c->renderer, bg, 0xCC1E2229);
    renderer_draw_rect(c->renderer, bg, 0xFF3D4450, 1);

    char buf[64];
    
    strcpy(buf, "PROFILER (CPU CYCLES)");
    draw_text_simple(c, sx, sy, buf, 0xFFE6E8EB);
    sy += line_h;

    strcpy(buf, "Input:  ");
    int_to_str(perf_input_cycles, buf + 8);
    draw_text_simple(c, sx, sy, buf, 0xFF99A1AF);
    sy += line_h;

    strcpy(buf, "Events: ");
    int_to_str(perf_event_cycles, buf + 8);
    draw_text_simple(c, sx, sy, buf, 0xFF99A1AF);
    sy += line_h;

    strcpy(buf, "Render: ");
    int_to_str(perf_render_cycles, buf + 8);
    draw_text_simple(c, sx, sy, buf, 0xFFE6E8EB);
    sy += line_h;

    strcpy(buf, "Blit:   ");
    int_to_str(perf_blit_cycles, buf + 8);
    draw_text_simple(c, sx, sy, buf, 0xFFFF5F57);
    sy += line_h;

    strcpy(buf, "Total:  ");
    int_to_str(perf_total_cycles, buf + 8);
    draw_text_simple(c, sx, sy, buf, 0xFFE6E8EB);
}

/* --------------------------------------------------------------------------
 * Frame
 * -------------------------------------------------------------------------- */

static bool s_show_debug_overlays = false;

void compositor_set_debug_overlays(bool enabled) {
    s_show_debug_overlays = enabled;
}

void compositor_frame(compositor_t *c) {
    if (!c) return;

    static int frame_count = 0;
    if (frame_count == 0) {
        vga_puts("\n[COMPOSITOR] Starting frame loop\n");
    }
    frame_count++;

    uint64_t t_start = rdtsc();

    /* 0. Taskbar: sincroniza apps abertos + relógio */
    taskbar_refresh();
    switch_task(); /* yield - reduce peak stack */

    /* 1. Poll input → post events */
    input_manager_poll(c->input);
    uint64_t t_in = rdtsc();
    perf_input_cycles = t_in - t_start;
    switch_task(); /* yield */

    /* 2. Dispatch events to all subscribers */
    event_bus_dispatch(c->bus);
    uint64_t t_ev = rdtsc();
    perf_event_cycles = t_ev - t_in;
    switch_task(); /* yield */

    /* 3. Camera inertia */
    camera_update(c->camera);
    switch_task(); /* yield */

    /* 3.5. Animations */
    animation_engine_tick();
    switch_task(); /* yield */

    /* 4. Transform pass */
    node_update_transforms(c->scene_root, transform_identity());
    switch_task(); /* yield */

    /* 5. Full repaint every frame */
    cursor_restore(c);
    draw_background(c);
    renderer_set_clip(c->renderer, rect_zero());
    taskbar_refresh();
    switch_task(); /* yield */

    node_draw_recursive(c->scene_root, c->renderer);
    switch_task(); /* yield */

    if (s_show_debug_overlays) {
        draw_minimap(c);
        draw_profiler_overlay(c);
        switch_task(); /* yield */
    }

    /* 6. Cursor */
    int mx = input_mouse_x(c->input);
    int my = input_mouse_y(c->input);
    cursor_draw(c, mx, my);
    switch_task(); /* yield */

    uint64_t t_render = rdtsc();
    perf_render_cycles = t_render - t_ev;

    /* Apply CRT scanlines effect */
    apply_scanlines(c);
    switch_task(); /* yield */

    /* 7. Flip back-buffer → VRAM */
    renderer_present(c->renderer);
    switch_task(); /* yield */

    uint64_t t_blit = rdtsc();
    perf_blit_cycles = t_blit - t_render;
    perf_total_cycles = t_blit - t_start;

    c->frame_number++;

    /* Final yield */
    switch_task();
}

void compositor_draw_image(compositor_t *comp, int x, int y, int width, int height, const uint32_t *buffer) {
    if (!comp || !buffer) return;
    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            uint32_t c = buffer[j * width + i];
            uint8_t a = (c >> 24) & 0xFF;
            if (a > 0) { // Simple alpha test
                fb_renderer_draw_pixel(comp->renderer, x + i, y + j, c);
            }
        }
    }
}
