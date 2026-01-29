# Camera System — Fixed-Point Infinite Canvas Camera

## Objective

Provide a **zoomable, pannable, inertia-driven camera** over the infinite world-space canvas. The camera translates between two coordinate spaces:

- **World space**: the logical infinite coordinate system where scene graph nodes live.
- **Screen space**: pixel coordinates on the physical display.

All arithmetic uses **fixed-point integers** — no floating-point — because the kernel is compiled with `-mno-sse` and has no hardware FPU available in ring 0.

## Problems Solved

- **Floating-point prohibition**: zoom, position, velocity, and all conversions use fixed-point scaling (`CAMERA_ZOOM_SCALE = 1024`, `CAMERA_POS_SCALE = 256`) to achieve sub-pixel precision with only integer arithmetic.
- **Infinite canvas**: no bounds on world coordinates; the camera can pan arbitrarily far. The viewport is dynamically computed each frame.
- **Inertia (kinetic scrolling)**: when the user pans and releases, velocity decays exponentially (~85% per frame) via `CAMERA_FRICTION_NUM / CAMERA_FRICTION_DEN`.
- **Zoom-to-cursor**: `camera_zoom_at()` preserves the world point under a screen-space pivot so zooming feels anchored to the cursor position.
- **Fit-to-content**: `camera_fit()` computes a zoom level and center point that makes a set of world-space rectangles fully visible.
- **Coordinate conversion**: inline helpers convert world↔screen positions, world rects to screen rects, and compute the visible viewport frustum — all without function call overhead.

## Architecture

```
                    World Space
                  (infinite canvas)
                        │
                        │  node at (world_x, world_y)
                        │
                        ▼
            ┌─────────────────────┐
            │      camera_t       │
            │                     │
            │  pos_x_fp, pos_y_fp │  ← position scaled by ×256
            │  zoom_fp            │  ← zoom scaled by ×1024
            │  screen_w, screen_h │
            │  vel_x_fp, vel_y_fp │
            │  dirty              │
            │                     │
            │  camera_world_to_   │
            │  screen_x(y)()      │
            │                     │
            │  camera_screen_to_  │
            │  world_x(y)()       │
            └─────────────────────┘
                        │
                        ▼
                    Screen Space
                  (pixel coords 0..W-1, 0..H-1)
                        │
                        ▼
              ┌─────────────────┐
              │   Compositor    │
              │   draw nodes at │
              │   screen pos    │
              └─────────────────┘

Camera projection (world → screen):
  screen_x = (world_x - pos_x) * zoom_fp / 1024
  pos_x = pos_x_fp / 256
```

## Lifecycle

### Creation
```c
camera_t *camera_create(int screen_w, int screen_h);
```
Allocates from kernel heap. Initialises `zoom_fp = 1024` (1.0×), position at (0,0), velocity zero, `dirty = true`.

### Per-Frame Update
```c
void camera_update(camera_t *cam);
```
Called each frame by `compositor_frame()`:
1. If velocity magnitude exceeds threshold (`POS_SCALE / 4 ≈ 0.25 px`), apply friction and update position.
2. Otherwise clamp velocity to zero (halt inertia).
3. Sets `dirty = true` when position changes.

### Destruction
```c
void camera_destroy(camera_t *cam);
```
Frees the camera struct.

## Navigation API

```c
void camera_pan(camera_t *cam, int dx, int dy);
```
- Translates by `(dx * POS_SCALE, dy * POS_SCALE)` world pixels.
- Sets velocity to the pan delta (feeds inertia).
- Sets `dirty = true`.

```c
void camera_center_on(camera_t *cam, int world_x, int world_y);
```
- Repositions so that `(world_x, world_y)` is at the centre of the screen.
- `pos_x_fp = wx * POS_SCALE - (screen_w * ZOOM_SCALE * POS_SCALE) / (2 * zoom_fp)`
- Sets `dirty = true`.

```c
void camera_zoom_at(camera_t *cam, int new_zoom_fp, int pivot_sx, int pivot_sy);
```
1. Clamp `new_zoom_fp` to `[CAMERA_ZOOM_MIN_FP, CAMERA_ZOOM_MAX_FP]`.
2. Compute world point under `(pivot_sx, pivot_sy)` using current zoom.
3. Set `zoom_fp = new_zoom_fp`.
4. Reposition so the same world point maps back to `(pivot_sx, pivot_sy)` under the new zoom.

```c
void camera_reset(camera_t *cam);
```
- Zeroes position and velocity.
- Resets zoom to 1.0×.
- Sets `dirty = true`.

```c
void camera_fit(camera_t *cam, const gui_rect_t *rects, uint32_t count);
```
1. Compute bounding box of all `rects` (with 80px padding).
2. Compute `zoom_x = screen_w * ZOOM_SCALE / bounds_w`, `zoom_y` similarly.
3. Pick the smaller zoom, clamp to valid range.
4. Center camera on bounding box midpoint.

## Coordinate Conversion API

All conversions are **`static inline`** — zero call overhead, expanded at the call site.

```c
/* World → Screen */
static inline int camera_world_to_screen_x(const camera_t *c, int wx);
static inline int camera_world_to_screen_y(const camera_t *c, int wy);
```
```
screen_x = ((wx * POS_SCALE - pos_x_fp) * zoom_fp) / (ZOOM_SCALE * POS_SCALE)
```

```c
/* Screen → World */
static inline int camera_screen_to_world_x(const camera_t *c, int sx);
static inline int camera_screen_to_world_y(const camera_t *c, int sy);
```
```
world_x = ((sx * ZOOM_SCALE * POS_SCALE) / zoom_fp + pos_x_fp) / POS_SCALE
```

```c
/* World rect → screen rect (AABB) */
static inline gui_rect_t camera_world_rect_to_screen(const camera_t *c, gui_rect_t wr);

/* Viewport frustum in world space */
static inline gui_rect_t camera_viewport_in_world(const camera_t *c);

/* Scale a world dimension to screen pixels */
static inline int camera_scale(const camera_t *c, int world_dim);
```

### Viewport in World Space
```
viewport_x = screen_to_world_x(0)
viewport_y = screen_to_world_y(0)
viewport_w = screen_to_world_x(screen_w) - viewport_x
viewport_h = screen_to_world_y(screen_h) - viewport_y
```

## Fixed-Point Constants

```c
#define CAMERA_ZOOM_SCALE  1024          /* zoom_fp multiplier           */
#define CAMERA_POS_SCALE   256           /* position fixed-point (8 frac) */

#define CAMERA_ZOOM_MIN_FP  (1024 / 10)     /* 0.1×  = 102             */
#define CAMERA_ZOOM_MAX_FP  (1024 * 8)      /* 8.0×  = 8192            */
#define CAMERA_ZOOM_DEF_FP  (1024)          /* 1.0×  = 1024            */

#define CAMERA_FRICTION_NUM  870            /* velocity decay numerator  */
#define CAMERA_FRICTION_DEN  1024           /* velocity decay denominator*/
                                            /* 870/1024 ≈ 0.85          */

#define CAMERA_ZOOM_STEP_FP  82             /* ≈0.08× per key press     */

#define CAMERA_POS_SCALE     256            /* 8 fractional bits         */
                                            /* 1 world px = 256 fp units */
```

## Internal State

```c
typedef struct {
    int32_t  pos_x_fp;     /* world x × 256  */
    int32_t  pos_y_fp;     /* world y × 256  */
    int32_t  zoom_fp;      /* zoom × 1024    */
    int32_t  vel_x_fp;     /* velocity × 256 */
    int32_t  vel_y_fp;

    int      screen_w;     /* display width in pixels  */
    int      screen_h;     /* display height in pixels */

    bool     dirty;        /* view_transform must be rebuilt */
} camera_t;
```

## Dependencies

- `rect.h` — `gui_rect_t`, `rect_make()` used by `camera_world_rect_to_screen()` and `camera_viewport_in_world()`
- `kheap.h` — `kmalloc()` / `kfree()`
- `string.h` — `memset()`

## Limitations / Trade-offs

| Limitation | Rationale |
|---|---|
| 8-bit fractional position precision | `POS_SCALE = 256` gives 1/256 pixel precision; more than adequate for a 1920×1080 display. Extra bits would overflow `int64_t` intermediates. |
| 10-bit fractional zoom precision | `ZOOM_SCALE = 1024` gives ~0.1% zoom granularity. Finer steps not perceptible. |
| No rotation or shear | Camera is strictly orthographic (translate + uniform scale). Rotation would require SSE or software fp. |
| Velocity friction is framerate-dependent | `CAMERA_FRICTION_NUM/DEN` assumes 60 FPS. At variable frame rates, inertia feels different. Future: frame-rate independent decay. |
| `int64_t` intermediates required | `world_to_screen_x()` uses `int64_t` to avoid overflow when multiplying zoom × position. On x86-64 this is a single instruction. |
| No acceleration curve | Pan velocity is set directly from mouse delta; no smooth acceleration/deceleration ramp. |
| `dirty` flag unused externally | Currently `dirty` is set but no consumer queries it. Reserved for compositor optimisation. |

## Performance / Memory Optimisations

- `camera_t` is **32 bytes** — fits in two cache lines.
- All coordinate conversions are single-line `static inline` functions — no call overhead.
- Integer arithmetic only: all `int64_t` intermediates are compiled to single `imul` / `idiv` instructions on x86-64.
- No dynamic memory during navigation: `camera_fit()` stack-allocates the rect array (up to 64 rects × 16 bytes = 1 KB).
- `camera_update()` early-exits when velocity is below threshold, avoiding multiplication for stationary cameras.

## Future Extensions

1. **Frame-rate-independent inertia**: decay factor computed from elapsed time (`factor^dt`) instead of per-frame decay.
2. **Smooth animations**: `camera_animate_to()` for programmatic fly-throughs using the animation engine.
3. **Rotation camera**: added as a separate `angle` field with trig lookup tables (no `sin`/`cos` from libm).
4. **Layer parallax**: separate scroll factors for background grid vs nodes.
5. **Camera constraints**: bounding box min/max to prevent panning outside a defined map boundary.
6. **Perspective projection**: optional field for 3D-like effects (requires matrix stack).

## Usage Examples

### World-to-Screen (Draw a node at its screen position)
```c
static void my_node_draw(node_t *self, gui_renderer_t *r) {
    camera_t *cam = g_compositor->camera;
    int sx = camera_world_to_screen_x(cam, self->local_x);
    int sy = camera_world_to_screen_y(cam, self->local_y);
    int sw = camera_scale(cam, self->width);
    int sh = camera_scale(cam, self->height);
    renderer_fill_rect(r, sx, sy, sw, sh, 0xFF3B82F6);
}
```

### Screen-to-World (Hit-test from mouse click)
```c
static void select_event(tool_t *self, const gui_event_t *e) {
    camera_t *cam = self->camera;
    int wx = camera_screen_to_world_x(cam, e->mouse.x);
    int wy = camera_screen_to_world_y(cam, e->mouse.y);
    /* now check world-space bounds */
}
```

### Zoom at Cursor (PanTool Scroll Handler)
```c
case GUI_EVENT_MOUSE_SCROLL: {
    int step = e->mouse.dy > 0 ? CAMERA_ZOOM_STEP_FP : -CAMERA_ZOOM_STEP_FP;
    int new_zoom = cam->zoom_fp + step;
    camera_zoom_at(cam, new_zoom, e->mouse.x, e->mouse.y);
    return true;
}
```

### Pan with Inertia (PanTool Drag)
```c
case GUI_EVENT_MOUSE_MOVE:
    if (s->dragging) {
        int ddx = e->mouse.x - s->drag_start_mx;
        int ddy = e->mouse.y - s->drag_start_my;
        int world_dx = -(int)((int64_t)ddx * CAMERA_ZOOM_SCALE / cam->zoom_fp);
        int world_dy = -(int)((int64_t)ddy * CAMERA_ZOOM_SCALE / cam->zoom_fp);
        cam->pos_x_fp = s->drag_start_px + world_dx * CAMERA_POS_SCALE;
        cam->pos_y_fp = s->drag_start_py + world_dy * CAMERA_POS_SCALE;
        cam->dirty = true;
        return true;
    }
    break;
```

### Fit to Screen (PanTool F Key)
```c
gui_rect_t rects[64];
uint32_t count = 0;
for (uint32_t i = 0; i < scene_root->child_count && count < 64; i++) {
    node_t *n = scene_root->children[i];
    rects[count++] = rect_make(n->local_x, n->local_y, n->width, n->height);
}
camera_fit(cam, rects, count);
```

---

*Document v1.0 — reflects kernel/gui/scene/camera.{h,c} as of LiwusOS build.*
