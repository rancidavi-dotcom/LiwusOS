# Coordinate System — Local, World, Canvas, and Screen Spaces

## Objective

Define the **four coordinate spaces** used throughout the LiwusOS GUI, the **transform chain** that relates them, and the **fixed-point affine matrix** that drives all spatial calculations. This document covers the mathematical foundation for layout, rendering, hit-testing, and camera projection — all without floating-point arithmetic in the kernel.

## Problems Solved

- **Four clearly defined spaces** prevent confusion between local offsets, world positions, and screen pixels.
- **Cached world transforms** avoid recomputing the full chain of ancestor transforms for every node every frame.
- **Dirty flag propagation** (`NODE_DIRTY_TRANSFORM`) ensures transforms are recomputed lazily only when a node or its ancestors move.
- **Screen-space bounds** enable fast culling and hit-testing without walking the tree each frame.
- **Fixed-point 16.16 transform matrix** (`gui_transform_t`) provides affine transformations (translation + scale) using integer arithmetic, compatible with the `-mno-sse` kernel build.
- **AABB computation** via `transform_apply_rect()` gives conservative screen bounds for any world rect, enabling efficient dirty-region accumulation.

## Coordinate Spaces

```
+------------------------------------------------------------------+
|                        WORLD SPACE                                |
|  (infinite canvas, logical coordinates)                           |
|                                                                   |
|   +-----------------------------------------------------------+  |
|   |                     LOCAL SPACE                            |  |
|   |  (relative to parent node's origin)                       |  |
|   |                                                           |  |
|   |   node_t                                                  |  |
|   |     local_x = 100     local_y = 50                        |  |
|   |     width  = 200     height = 150                         |  |
|   |                                                           |  |
|   |   child node                                              |  |
|   |     local_x = 10      local_y = 10   ← relative to parent |  |
|   +-----------------------------------------------------------+  |
|                                                                   |
|   world_transform = local_translate ∘ parent_world_transform     |
|                                                                   |
+------------------------------------------------------------------+
          │
          │  Camera Projection
          ▼
+------------------------------------------------------------------+
|                        SCREEN SPACE                               |
|  (pixel coordinates on display)                                   |
|                                                                   |
|   screen_x = camera_world_to_screen_x(world_x)                   |
|   screen_y = camera_world_to_screen_y(world_y)                   |
|                                                                   |
|   screen_bounds = camera_world_rect_to_screen(world_rect)        |
|                                                                   |
+------------------------------------------------------------------+
```

### LOCAL Space

- `node->local_x`, `node->local_y`: position relative to the parent node's origin.
- `node->width`, `node->height`: dimensions in world-logical units.
- Layout engines (VBOX, HBOX) assign local positions to children.
- The local transform is `transform_translation(local_x, local_y)`.

### WORLD Space (also called CANVAS space)

- The root canvas node's world space == local space (it has no parent).
- `node->world_transform`: cached affine matrix from the canvas root to this node.
- Computed by `node_update_transforms()`:
  ```c
  world_transform = local ∘ parent_world
  ```
  where `local = transform_translation(node->local_x, node->local_y)`.
- All nodes share the same infinite canvas; WORLD and CANVAS refer to the same space.

### SCREEN Space

- Pixel coordinates after camera projection.
- Computed per node by widgets during `draw()` using the camera API:
  ```c
  int sx = camera_world_to_screen_x(cam, world_origin.x);
  int sy = camera_world_to_screen_y(cam, world_origin.y);
  int sw = camera_scale(cam, node->width);
  int sh = camera_scale(cam, node->height);
  ```
- `node->screen_bounds`: AABB cached after transform update, used for hit-testing and culling.

## Transform Matrix (`gui_transform_t`)

```c
typedef struct {
    int32_t a, b;   /* column 0: scale-x, shear-y     */
    int32_t c, d;   /* column 1: shear-x, scale-y     */
    int32_t tx, ty; /* column 2: translation (pixels) */
} gui_transform_t;

#define TRANSFORM_SCALE 65536    /* 16.16 fixed point */
```

### Matrix Layout (Column-Major)

```
| a  c  tx |     a,d = scale (× 65536)
| b  d  ty |     c,b = shear (currently 0)
| 0  0   1  |     tx,ty = translation (integer pixels)
```

A point `(px, py)` is transformed as:

```
x' = (a * px + c * py) / 65536 + tx
y' = (b * px + d * py) / 65536 + ty
```

### Current Usage Note

All current transforms are **pure translations** (a = d = 65536, b = c = 0). Scale/shear elements are present in the type and used by `transform_scale()` / `transform_concat()`, but the scene graph currently uses only `transform_translation()` for node positioning. The camera handles all scaling through its own fixed-point `camera_scale()` function rather than through the transform matrix.

## Transform Operations

```c
gui_transform_t transform_identity(void);
// a=d=65536, b=c=0, tx=ty=0

gui_transform_t transform_translation(int32_t tx, int32_t ty);
// identity + translation

gui_transform_t transform_scale(int32_t sx, int32_t sy);
// identity + scale (a=sx, d=sy)

gui_transform_t transform_uniform_scale(int32_t s);
// transform_scale(s, s)

gui_transform_t transform_concat(gui_transform_t a, gui_transform_t b);
// result = B ∘ A  (apply A first, then B)

gui_pointi_t transform_apply(gui_transform_t t, int32_t px, int32_t py);
gui_pointi_t transform_apply_i(gui_transform_t t, int px, int py);
// (px', py') = t × (px, py)

gui_rect_t transform_apply_rect(gui_transform_t t, gui_rect_t r);
// All 4 corners transformed, then AABB computed

gui_transform_t transform_invert_simple(gui_transform_t t);
// Invert for non-sheared transforms only (uses float!)
```

### Transform Composition Detail

```c
gui_transform_t transform_concat(gui_transform_t a, gui_transform_t b) {
    r.a  = (b.a * a.a + b.c * a.b) / 65536;
    r.b  = (b.b * a.a + b.d * a.b) / 65536;
    r.c  = (b.a * a.c + b.c * a.d) / 65536;
    r.d  = (b.b * a.c + b.d * a.d) / 65536;
    r.tx = (b.a * a.tx + b.c * a.ty) / 65536 + b.tx;
    r.ty = (b.b * a.tx + b.d * a.ty) / 65536 + b.ty;
}
```

All intermediates use `int64_t` to prevent overflow when multiplying two 16.16 values.

## Transform Chain (Scene Graph Walk)

```
canvas_root (world_transform = identity)
    └─ group_node (local=(10,10))
         world = translate(10,10) ∘ identity
         └─ window_node (local=(100,50))
              world = translate(100,50) ∘ parent_world
              └─ button_node (local=(8,30))
                   world = translate(8,30) ∘ parent_world
```

### `node_update_transforms()` Algorithm

```c
void node_update_transforms(node_t *node, gui_transform_t parent_world) {
    if (node->dirty & NODE_DIRTY_TRANSFORM) {
        gui_transform_t local = transform_translation(node->local_x, node->local_y);
        node->world_transform = transform_concat(local, parent_world);
        node->dirty &= ~NODE_DIRTY_TRANSFORM;
    }
    for (uint32_t i = 0; i < node->child_count; i++) {
        node_update_transforms(node->children[i], node->world_transform);
    }
}
```

Called from `compositor_frame()`:
```c
node_update_transforms(scene_root, transform_identity());
```

## Dirty Flag Propagation

```c
#define NODE_DIRTY_TRANSFORM  (1u << 0)  /* world_transform needs recompute */
#define NODE_DIRTY_LAYOUT     (1u << 1)  /* layout pass needed              */
#define NODE_DIRTY_PAINT      (1u << 2)  /* content changed, repaint needed */
```

When `node_mark_dirty(node, NODE_DIRTY_TRANSFORM)` is called:
1. Sets `node->dirty |= NODE_DIRTY_TRANSFORM`.
2. Recursively propagates to **all children** (transform dirtiness is inherited).

This ensures that when a parent moves, every descendant has its `world_transform` recomputed on the next frame.

## Screen Bounds Computation

Screen bounds are computed by each widget during its `draw()` callback using the camera:

```c
// In a typical widget draw function:
int sx = camera_world_to_screen_x(cam, node->world_transform.tx);
int sy = camera_world_to_screen_y(cam, node->world_transform.ty);
int sw = camera_scale(cam, node->width);
int sh = camera_scale(cam, node->height);

// Cache for hit-testing:
node->screen_bounds = rect_make(sx, sy, sw, sh);
```

**Important**: screen bounds are currently computed lazily during drawing, not during the transform pass. This means `node->screen_bounds` may be stale until the node is drawn. Hit-testing (`node_hit_test()`) relies on up-to-date `screen_bounds`, so a `node_update_transforms()` + draw pass must precede hit-testing.

## Hit Testing

```c
node_t *node_hit_test(node_t *root, int screen_x, int screen_y);
```

Algorithm:
1. Iterate children in **reverse order** (highest z-order first — last child is drawn on top).
2. Recursively hit-test each child — if any child hits, return it immediately.
3. If no child hits, test this node: return it if `interactive && rect_contains_point(screen_bounds, screen_x, screen_y)`.
4. Return NULL if nothing hit.

```
Scenario: clicking screen position (300, 200)

  canvas_root  ─── children reversed ──▶  terminal_node
     ├─ window           (screen.x=100..400, y=50..250)
     │   ├─ button       (screen.x=108..228, y=80..116)  ◄── HIT
     │   └─ label        (screen.x=108..368, y=122..146)
     └─ terminal         (screen.x=10..300, y=400..500)

  node_hit_test walks: terminal → window → window.button → window.label
  Returns: button (first hit in reverse z-order)
```

## Complete Data Flow (One Frame)

```
  compositor_frame()
    │
    ├─ 1. input_manager_poll()             ── mouse at (sx, sy)
    ├─ 2. event_bus_dispatch()             ── MOUSE_DOWN at (sx, sy)
    │      └─ ToolManager.on_event()
    │           └─ SelectTool: node_hit_test(root, sx, sy)
    │               Uses node->screen_bounds (from previous frame draw)
    │
    ├─ 3. camera_update()                  ── inertia decay
    ├─ 4. node_update_transforms(root, identity)
    │      └─ recompute world_transform for dirty nodes
    │
    ├─ 5. draw_background()                ── dot grid at camera (0,0)
    ├─ 6. node_draw_recursive(root, r)     ── each widget:
    │      └─ computes screen bounds via camera
    │      └─ paints at screen position
    │
    └─ 7. cursor_draw(mx, my)             ── always on top
```

## Dependencies

- `rect.h` — `gui_rect_t`, `gui_pointi_t`, `rect_contains_point()`, `rect_make()`
- `camera.h` — `camera_world_to_screen_*()`, `camera_screen_to_world_*()`, `camera_scale()`
- `transform.h` — `gui_transform_t`, `transform_*()` functions
- `node.h` — `node_t`, `node_update_transforms()`, `node_hit_test()`, `node_mark_dirty()`

## Limitations / Trade-offs

| Limitation | Rationale |
|---|---|
| **`transform_invert_simple()` uses `float`** | This function casts to `float` and divides — a known violation of the `-mno-sse` policy. Currently unused in the hot path; exists for future camera-via-matrix approach. Must be replaced with fixed-point division. |
| **Screen bounds computed during draw, not transform** | `screen_bounds` may be one frame stale for hit-testing. Fix: compute in a separate pass after camera update and before event dispatch. |
| **No shear or rotation in the transform chain** | Current scene graph only translates. Scale/shear matrix entries are present but unused. Rotation would require SSE or fixed-point trig. |
| **`int32_t` overflow risk** | `transform_concat()` uses `int64_t` intermediates, but `a, d` (scale) exceeding ~2.0 at large translations could overflow the 16.16 representation. Mitigated by clamping zoom range to 0.1×–8.0×. |
| **World space is unbounded** | `int` positions can overflow if nodes are placed at extreme coordinates ( > 2^31 pixels from origin). Canvas is "infinite" but practically limited to ±1M pixels. |
| **`NODE_DIRTY_TRANSFORM` propagation is O(n)** | Propagating dirt to all children on every position change is O(subtree). Acceptable because UI trees are shallow (< 5 levels, < 100 nodes). |

## Performance / Memory Optimisations

- **Lazy transform recompute**: `NODE_DIRTY_TRANSFORM` is cleared only when `node_update_transforms()` runs. Static subtrees pay no cost.
- **`int64_t` intermediates**: All multiplications use 64-bit to prevent overflow; on x86-64 this compiles to single `imul` instructions.
- **No separate screen_bounds pass**: Reusing the draw pass to set `screen_bounds` avoids an extra tree walk. This is a correctness trade-off (see limitations) that saves CPU time.
- **Hit-test short-circuits**: `node_hit_test()` walks children in reverse and returns immediately on first hit — no unnecessary recursion.
- **`gui_transform_t` is 24 bytes**: Fits in 3 cache lines, copied by value (small enough for register passing on x86-64 ABI).

## Coordinate Conversion Summary

| Operation | From | To | Method |
|---|---|---|---|
| Local → World | `(local_x, local_y)` | `world_transform` | `transform_concat(translate, parent_world)` |
| World → Screen | `world_transform.tx` | screen pixel | `camera_world_to_screen_x(cam, tx)` |
| Screen → World | screen pixel | world coordinate | `camera_screen_to_world_x(cam, sx)` |
| World rect → Screen AABB | `gui_rect_t` in world | `gui_rect_t` in screen | `camera_world_rect_to_screen(cam, rect)` |
| Screen hit → World target | `(sx, sy)` | node_t* | `node_hit_test(root, sx, sy)` |
| World dim → Screen size | `width` | `camera_scale(cam, width)` | `width * zoom_fp / 1024` |

## Macros in `transform.h`

```c
#define SCREEN_TO_WORLD(cam_transform, sx, sy) \
    transform_apply(transform_invert_simple(cam_transform), (float)(sx), (float)(sy))

#define WORLD_TO_SCREEN(cam_transform, wx, wy) \
    transform_apply((cam_transform), (float)(wx), (float)(wy))
```

These macros exist for a future architecture where the camera is represented as a `gui_transform_t` instead of the current dedicated `camera_t` with fixed-point functions. **Currently unused** — all coordinate conversion goes through `camera.h` inlines.

## Future Extensions

1. **Shear-aware invert**: implement `transform_invert_full()` using Cramer's rule with `int64_t` fixed-point, removing the `float` dependency from `transform_invert_simple()`.
2. **Screen-bounds pass**: a dedicated tree walk after `camera_update()` that recomputes `node->screen_bounds` from `world_transform.tx/ty` + camera, decoupling bounds from drawing.
3. **Camera as a transform**: replace the `camera_t` struct with a `gui_transform_t` for the projection, unifying the two systems.
4. **Non-uniform node scaling**: wire `transform_scale()` into the node transform chain for per-node zoom effects.
5. **Node-local rotation**: add `rotation_degrees` to `node_t`, computed via fixed-point lookup table.

## Usage Examples

### World Position from Mouse Click
```c
// In select_tool.c:
if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
    node_t *hit = node_hit_test(self->scene_root, e->mouse.x, e->mouse.y);
    // e->mouse.x, e->mouse.y are in SCREEN space
    // node_hit_test checks screen_bounds (also screen space)
    if (hit && hit->type == NODE_WINDOW) {
        // Convert screen → world for the node position
        int wx = camera_screen_to_world_x(self->camera, e->mouse.x);
        int wy = camera_screen_to_world_y(self->camera, e->mouse.y);
        // wx, wy are in WORLD/CANVAS space
    }
}
```

### Setting Node Position (World Space)
```c
// In move_tool.c:
int world_dx = (int)((int64_t)ddx * CAMERA_ZOOM_SCALE / cam->zoom_fp);
int world_dy = (int)((int64_t)ddy * CAMERA_ZOOM_SCALE / cam->zoom_fp);
node_set_position(sel,
    s->start_node_x + world_dx,   // start in LOCAL space
    s->start_node_y + world_dy);  // delta in WORLD space → applied as local offset
```

### Transform Chain Update
```c
// compositor_frame():
node_update_transforms(c->scene_root, transform_identity());
// After this, every visible node has a valid world_transform
// node->world_transform.tx/ty = absolute position in world space
```

### Screen Bounds for Culling
```c
// Compositor culling (future optimisation):
gui_rect_t viewport = camera_viewport_in_world(cam);
for (each node) {
    gui_rect_t world_bounds = rect_make(
        node->world_transform.tx,
        node->world_transform.ty,
        node->width,
        node->height);
    if (rect_intersects(viewport, world_bounds)) {
        node->draw(node, renderer);
    }
}
```

---

*Document v1.0 — reflects kernel/gui/math/transform.h, scene/node.{h,c}, scene/camera.h as of LiwusOS build.*
