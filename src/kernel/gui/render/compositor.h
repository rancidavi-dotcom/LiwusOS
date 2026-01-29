/*
 * gui/render/compositor.h
 *
 * The Compositor — the frame loop of the LiwusOS GUI.
 *
 * Responsibilities:
 *   1. Drive input_manager_poll() each frame.
 *   2. Dispatch event bus.
 *   3. Walk the scene graph — update transforms, run layout.
 *   4. Collect dirty rectangles from nodes that changed.
 *   5. Set render clip to each dirty region and invoke node_draw_recursive().
 *   6. Draw cursor overlay (not a node — always on top).
 *   7. Call renderer_present() to flip the back-buffer.
 *
 * Dirty-rect tracking:
 *   When a node calls node_mark_dirty(NODE_DIRTY_PAINT), the compositor
 *   accumulates its screen_bounds into a dirty-rect list.  Only those
 *   regions are re-drawn, saving substantial fill-rate on large canvases.
 *
 * Target frame budget: 16 ms (60 FPS) on the host CPU.
 */
#ifndef GUI_COMPOSITOR_H
#define GUI_COMPOSITOR_H

#include <stdint.h>
#include "../scene/node.h"
#include "../scene/camera.h"
#include "../render/renderer.h"
#include "../input/input_manager.h"
#include "../core/event_bus.h"

#define COMPOSITOR_MAX_DIRTY_RECTS 64

typedef enum {
    CURSOR_ARROW = 0,
    CURSOR_HAND,
    CURSOR_IBEAM,
    CURSOR_RESIZE_NS,
    CURSOR_RESIZE_EW,
    CURSOR_NO,
    CURSOR_WAIT,
    CURSOR_RESIZE_NWSE,
    CURSOR_RESIZE_NESW,
    CURSOR_MOVE,
    CURSOR_COUNT
} gui_cursor_t;

typedef struct {
    /* Core components */
    gui_renderer_t  *renderer;
    camera_t        *camera;
    gui_event_bus_t *bus;
    input_manager_t *input;
    node_t          *scene_root;

    /* Dirty rectangle list */
    gui_rect_t       dirty_rects[COMPOSITOR_MAX_DIRTY_RECTS];
    uint32_t         dirty_count;
    bool             full_redraw;

    /* Cursor sprite — save pixels under cursor to avoid ghost trails.
     * Max 32x32 (Chicago95 cursors). */
    int              cursor_x;
    int              cursor_y;
    bool             cursor_saved;
    uint32_t         cursor_save_buf[32 * 32];
    int              cursor_save_w;
    int              cursor_save_h;
    gui_cursor_t     cursor_type;

    /* Frame counter */
    uint64_t         frame_number;
} compositor_t;

/* --------------------------------------------------------------------------
 * Lifecycle
 * -------------------------------------------------------------------------- */

compositor_t *compositor_create(gui_renderer_t  *renderer,
                                  camera_t        *camera,
                                  gui_event_bus_t *bus,
                                  input_manager_t *input,
                                  node_t          *scene_root);

void          compositor_destroy(compositor_t *c);

void compositor_set_cursor(compositor_t *c, gui_cursor_t type);

/* --------------------------------------------------------------------------
 * Per-frame entry point  (call in a tight loop from the kernel task)
 * -------------------------------------------------------------------------- */

void compositor_frame(compositor_t *c);

/* --------------------------------------------------------------------------
 * Dirty-rect API  (called by nodes when their content changes)
 * -------------------------------------------------------------------------- */

void compositor_invalidate(compositor_t *c, const gui_rect_t *rect);
void compositor_invalidate_full(compositor_t *c);

/* --------------------------------------------------------------------------
 * Convenience: the global compositor singleton (set by compositor_create) */
extern compositor_t *g_compositor;

/* --------------------------------------------------------------------------
 * CRT Scanlines toggle
 * -------------------------------------------------------------------------- */

void compositor_set_scanlines(bool enabled);
bool compositor_get_scanlines(void);

/* Debug overlays (minimap + profiler). Off by default for a clean desktop. */
void compositor_set_debug_overlays(bool enabled);

#endif /* GUI_COMPOSITOR_H */
