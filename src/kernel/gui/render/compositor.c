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
compositor_t *g_compositor = NULL;

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
 * Background (dot grid on slate-900)
 * -------------------------------------------------------------------------- */

static void draw_background(compositor_t *c) {
    uint32_t *backbuf = fb_renderer_backbuf(c->renderer);
    if (!backbuf) return;

    int W = c->renderer->screen_w;
    int H = c->renderer->screen_h;
    int total = W * H;

    uint32_t bg_color = theme_engine_get_color(THEME_COLOR_BACKGROUND);
    /* Fill solid background */
    for (int i = 0; i < total; i++) backbuf[i] = bg_color;

    /* Dot grid — spacing scales with zoom */
    int dot_spacing = (int)((int64_t)40 * c->camera->zoom_fp / CAMERA_ZOOM_SCALE);
    if (dot_spacing < 4) dot_spacing = 4;

    int origin_sx = camera_world_to_screen_x(c->camera, 0);
    int origin_sy = camera_world_to_screen_y(c->camera, 0);
    int off_x = origin_sx % dot_spacing;
    int off_y = origin_sy % dot_spacing;
    if (off_x < 0) off_x += dot_spacing;
    if (off_y < 0) off_y += dot_spacing;

    for (int y = off_y; y < H; y += dot_spacing) {
        for (int x = off_x; x < W; x += dot_spacing) {
            backbuf[y * W + x] = theme_engine_get_color(THEME_COLOR_BUTTON_BG);
        }
    }
}

/* --------------------------------------------------------------------------
 * Cursor sprites (up to 16x16)
 * -------------------------------------------------------------------------- */

#define CURSOR_W 16
#define CURSOR_H 16

/* Standard arrow (12x8) */
static const uint8_t cursor_arrow[CURSOR_H][CURSOR_W/8] = {
    {0x80, 0x00}, {0xC0, 0x00}, {0xE0, 0x00}, {0xF0, 0x00}, 
    {0xF8, 0x00}, {0xFC, 0x00}, {0xFE, 0x00}, {0xFF, 0x00},
    {0xF8, 0x00}, {0xB8, 0x00}, {0x18, 0x00}, {0x18, 0x00},
    {0x00, 0x00}, {0x00, 0x00}, {0x00, 0x00}, {0x00, 0x00}
};

/* Hand cursor (hover interactive) */
static const uint8_t cursor_hand[CURSOR_H][CURSOR_W/8] = {
    {0x0C, 0x00}, {0x0C, 0x00}, {0x0C, 0x00}, {0x0D, 0x80}, 
    {0x0D, 0xB0}, {0x0D, 0xB0}, {0x7D, 0xB0}, {0xFF, 0xF0},
    {0xFF, 0xF0}, {0xFF, 0xF0}, {0xFF, 0xF0}, {0xFF, 0xF0},
    {0x7F, 0xE0}, {0x3F, 0xC0}, {0x1F, 0x80}, {0x00, 0x00}
};

static void cursor_restore(compositor_t *c) {
    if (!c->cursor_saved || c->cursor_x < 0) return;
    uint32_t *backbuf = fb_renderer_backbuf(c->renderer);
    if (!backbuf) return;
    int W = c->renderer->screen_w;
    int H = c->renderer->screen_h;

    int i = 0;
    for (int row = 0; row < CURSOR_H; row++) {
        int py = c->cursor_y + row;
        if (py < 0 || py >= H) { i += CURSOR_W; continue; }
        for (int col = 0; col < CURSOR_W; col++) {
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

    /* Save pixels under new cursor position */
    c->cursor_x = mx;
    c->cursor_y = my;
    int i = 0;
    for (int row = 0; row < CURSOR_H; row++) {
        int py = my + row;
        for (int col = 0; col < CURSOR_W; col++) {
            int px = mx + col;
            if (py >= 0 && py < H && px >= 0 && px < W) {
                c->cursor_save_buf[i] = backbuf[py * W + px];
            } else {
                c->cursor_save_buf[i] = 0;
            }
            i++;
        }
    }
    c->cursor_saved = true;

    /* Paint cursor */
    const uint8_t (*pattern)[CURSOR_W/8] = cursor_arrow;
    if (c->cursor_type == CURSOR_HAND) pattern = cursor_hand;

    for (int row = 0; row < CURSOR_H; row++) {
        int py = my + row;
        if (py < 0 || py >= H) continue;
        for (int col = 0; col < CURSOR_W; col++) {
            int px = mx + col;
            if (px < 0 || px >= W) continue;
            
            uint8_t bits = pattern[row][col / 8];
            if (bits & (0x80 >> (col % 8))) {
                backbuf[py * W + px] = 0xFFFFFFFF;
            }
        }
    }
}

void compositor_set_cursor(compositor_t *c, gui_cursor_t type) {
    if (c) c->cursor_type = type;
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
    renderer_fill_rect(c->renderer, bg, 0x88000000);
    renderer_draw_rect(c->renderer, bg, 0xFFFFFFFF, 1);
    
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
    renderer_fill_rect(c->renderer, dot, 0xFFFF0000); // Red dot
}

/* --------------------------------------------------------------------------
 * Frame
 * -------------------------------------------------------------------------- */

void compositor_frame(compositor_t *c) {
    if (!c) return;

    /* 1. Poll input → post events */
    input_manager_poll(c->input);

    /* 2. Dispatch events to all subscribers */
    event_bus_dispatch(c->bus);

    /* 3. Camera inertia */
    camera_update(c->camera);

    /* 3.5. Animations */
    animation_engine_tick();

    /* 4. Transform pass */
    node_update_transforms(c->scene_root, transform_identity());

    /* 5. Full repaint every frame (dirty-rect opt comes in Phase 4).
     *    This is what the old lgx.c did and is perfectly correct. */

    /* Restore saved pixels under old cursor before repaint */
    cursor_restore(c);

    /* Repaint background */
    draw_background(c);

    /* Draw all nodes */
    renderer_set_clip(c->renderer, rect_zero());
    node_draw_recursive(c->scene_root, c->renderer);

    /* 5.5 Draw minimap overlay */
    draw_minimap(c);

    /* 6. Cursor — always on top, after all node painting */
    int mx = input_mouse_x(c->input);
    int my = input_mouse_y(c->input);
    cursor_draw(c, mx, my);

    /* 7. Flip back-buffer → VRAM */
    renderer_present(c->renderer);

    c->frame_number++;

    /* Yield to other kernel tasks */
    switch_task();
}
