# Compositor — The Frame Loop

## Objective

Drive the entire GUI frame loop: poll input, dispatch events, update camera/physics, tick animations, rebuild transforms, clear the background, draw the scene graph, overlay the cursor, and present the finished frame to the display — all at a target of ~60 FPS from a single kernel task.

## Problems Solved

- **Deterministic frame ordering**: Every frame runs the exact same pipeline, eliminating race conditions between input, animation, and rendering.
- **Cursor ghost elimination**: A save/restore mechanism preserves pixels under the cursor before redrawing, preventing trail artifacts.
- **Full repaint simplicity**: Currently every frame is a full redraw (`full_redraw = true`). The dirty-rect accumulation infrastructure (`COMPOSITOR_MAX_DIRTY_RECTS=64`) is designed and wired but not yet activated.
- **Global singleton access**: `g_compositor` allows widgets (buttons, panels, window frames) to reach the camera for world→screen coordinate conversion.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                  compositor_frame()                         │
│                                                             │
│  ┌─────────┐   ┌──────────┐   ┌───────────┐   ┌─────────┐ │
│  │ Input   │──▶│ Event    │──▶│ Camera   │──▶│ Anim.  │ │
│  │ Poll    │   │ Dispatch │   │ Update   │   │ Tick   │ │
│  └─────────┘   └──────────┘   └───────────┘   └─────────┘ │
│                                                             │
│  ┌───────────┐   ┌────────────┐   ┌──────────────┐        │
│  │ Transform │──▶│ Cursor    │──▶│ Background   │        │
│  │ Pass      │   │ Restore   │   │ Draw         │        │
│  └───────────┘   └────────────┘   └──────────────┘        │
│                                                             │
│  ┌──────────┐   ┌────────────┐   ┌──────────┐   ┌──────┐ │
│  │ Nodes    │──▶│ Cursor    │──▶│ Present  │──▶│Yield │ │
│  │ Draw     │   │ Draw      │   │ (Flip)   │   │      │ │
│  └──────────┘   └────────────┘   └──────────┘   └──────┘ │
└─────────────────────────────────────────────────────────────┘
```

**Data flow across a frame:**

```
Input HW (mouse/kbd)
    │
    ▼
input_manager_poll()
    │  posts gui_event_t to ring buffer
    ▼
event_bus_dispatch()
    │  sorts by priority, notifies subscribers
    ▼
camera_update()         ── friction-based inertia decay
animation_engine_tick() ── interpolate node properties
node_update_transforms()── rebuild world_transform for dirty nodes
cursor_restore()        ── write saved pixels under old cursor
draw_background()       ── slate-900 solid fill + dot grid
node_draw_recursive()   ── walk scene graph, call vtable->draw
cursor_draw()           ── blit cursor sprite at mouse (x,y)
renderer_present()      ── memcpy backbuffer → VRAM
switch_task()           ── yield to other kernel tasks
```

## Lifecycle

```
                           gui_init()
                              │
                              ▼
compositor_create(renderer, camera, bus, input, scene_root)
    │  allocates compositor_t, zeroes state, sets g_compositor
    ▼
while (1) {
    compositor_frame(c);      ← kernel task (gui_compositor_task)
}
    │
    ▼
compositor_destroy(c)
    │  kfree(c), clears g_compositor
    ▼
```

## APIs

### Public (in `compositor.h`)

```c
// Create the compositor singleton. All arguments are owned by the caller.
compositor_t *compositor_create(gui_renderer_t  *renderer,
                                  camera_t        *camera,
                                  gui_event_bus_t *bus,
                                  input_manager_t *input,
                                  node_t          *scene_root);

void compositor_destroy(compositor_t *c);

// Change cursor sprite type (arrow, hand, ibeam, etc.)
void compositor_set_cursor(compositor_t *c, gui_cursor_t type);

// Per-frame entry point — call in a tight loop.
void compositor_frame(compositor_t *c);

// Dirty-rect accumulation
void compositor_invalidate(compositor_t *c, const gui_rect_t *rect);
void compositor_invalidate_full(compositor_t *c);

// Global singleton
extern compositor_t *g_compositor;
```

### Private (in `compositor.c`)

```c
// Internal helpers (static):
static void draw_background(compositor_t *c);     // solid fill + dot grid
static void cursor_restore(compositor_t *c);      // restore saved pixels
static void cursor_draw(compositor_t *c, int mx, int my);  // draw cursor sprite
```

### Data structures

```c
typedef enum {
    CURSOR_ARROW   = 0,
    CURSOR_HAND    = 1,
    CURSOR_IBEAM   = 2,
    CURSOR_RESIZE_NS = 3,
    CURSOR_RESIZE_EW = 4
} gui_cursor_t;

#define COMPOSITOR_MAX_DIRTY_RECTS 64

typedef struct {
    gui_renderer_t  *renderer;
    camera_t        *camera;
    gui_event_bus_t *bus;
    input_manager_t *input;
    node_t          *scene_root;

    gui_rect_t       dirty_rects[COMPOSITOR_MAX_DIRTY_RECTS];
    uint32_t         dirty_count;
    bool             full_redraw;

    int              cursor_x;
    int              cursor_y;
    bool             cursor_saved;
    uint32_t         cursor_save_buf[16 * 16];
    gui_cursor_t     cursor_type;

    uint64_t         frame_number;
} compositor_t;
```

### Exact `compositor_frame()` step sequence (from `compositor.c:198`)

```
1.  input_manager_poll(c->input);              // read HW, post events
2.  event_bus_dispatch(c->bus);                 // process queued events
3.  camera_update(c->camera);                   // friction/inertia
4.  animation_engine_tick();                     // tween node properties
5.  node_update_transforms(c->scene_root, transform_identity());  // rebuild
6.  cursor_restore(c);                          // erase old cursor
7.  draw_background(c);                         // slate-900 + dots
8.  renderer_set_clip(c->renderer, rect_zero());// full-screen clip
9.  node_draw_recursive(c->scene_root, c->renderer);  // scene graph
10. cursor_draw(c, mx, my);                     // overlay cursor
11. renderer_present(c->renderer);              // memcpy to VRAM
12. c->frame_number++;
13. switch_task();                              // yield
```

## Dependencies

- `renderer.h` — `gui_renderer_t`, `renderer_set_clip`, `renderer_present`
- `scene/node.h` — `node_t`, `node_update_transforms`, `node_draw_recursive`
- `scene/camera.h` — `camera_t`, `camera_update`, `camera_world_to_screen_x/y`
- `input/input_manager.h` — `input_manager_poll`, `input_mouse_x/y`
- `core/event_bus.h` — `gui_event_bus_t`, `event_bus_dispatch`
- `core/animation_engine.h` — `animation_engine_tick`
- `core/theme_engine.h` — `theme_engine_get_color`
- `fb_renderer.h` — `fb_renderer_backbuf` (for direct pixel access)
- Kernel heap (`kheap.h`), string (`string.h`), task (`task.h`)

## Limitations & Trade-offs

| Limitation | Impact |
|---|---|
| Full redraw every frame | Every frame clears and redraws the entire screen. Fill-rate bound at high resolutions. |
| Cursor is not a scene node | Cannot be transformed, scaled, or parented. Always raster at mouse (screen) position. |
| `g_compositor` global | Convenient for widgets but creates hidden coupling. Singleton pattern limits testing. |
| No frame timing | No vsync, no frame delta measurement. Runs as fast as possible then yields. |
| Cursor sprite size hardcoded | `CURSOR_W=16`, `CURSOR_H=16`. Larger cursors need a code change. |

## Performance & Memory Optimizations

- **Cursor save buffer**: `cursor_save_buf[256]` (16×16) on the compositor stack — no heap allocation for cursor state.
- **Full-screen memset** for background: the inner loop in `draw_background` is O(W×H) but trivially vectorizable. Future: use `fast_memcpy` (SSE2 non-temporal stores) for the solid fill.
- **Dirty rect infrastructure**: `dirty_rects[64]` and `full_redraw` flag are already present. Enabling partial redraw only requires removing the `full_redraw = true` assignment in `compositor_invalidate()` and using the dirty rects for scissored rendering.
- **`switch_task()`**: Yields the CPU after each frame so the compositor does not starve other kernel tasks.

## Future Extensions

- **Partial dirty-rect rendering**: Track per-node `screen_bounds`, accumulate into `dirty_rects`, only clear+redraw within those rects, using renderer clip.
- **Triple buffering**: Alternate between three backbuffers to avoid stalls.
- **Frame pacing**: Measure elapsed time, sleep to maintain exactly 16.67ms per frame.
- **Compositing layers**: Render windows to individual textures, composite with GPU shaders.
- **Cursor as scene node**: Support animated, themed, hardware cursors.
- **Per-window buffers**: User-space apps render to their own buffer; compositor blits them.

## Usage Examples

```c
// Main compositor task (kernel thread entry point)
void gui_compositor_task(void) {
    while (1) {
        compositor_frame(g_compositor);
    }
}

// Invalidate a region when a node changes
void on_button_click(node_t *btn, void *ud) {
    compositor_invalidate(g_compositor, &btn->screen_bounds);
}

// Change cursor on hover
void my_tool_on_hover(node_t *target) {
    if (target->type == NODE_BUTTON)
        compositor_set_cursor(g_compositor, CURSOR_HAND);
    else
        compositor_set_cursor(g_compositor, CURSOR_ARROW);
}
```
