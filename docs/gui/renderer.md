# Renderer — Abstract Renderer Interface

## Objective

Provide a backend-agnostic rendering abstraction that decouples all GUI drawing code (compositor, widgets, scene graph) from the concrete pixel-pushing implementation. The compositor and all widgets call **only** through `renderer_ops_t` function pointers; zero code knows about VRAM, backbuffers, or GPU command buffers.

## Problems Solved

- **Backend independence**: Adding a new backend (Vulkan, Metal, software) requires zero changes to compositor or widget code.
- **Testability**: A mock renderer backend can be injected for unit tests.
- **Isolation**: VRAM pointer arithmetic, alpha blending, and pitch calculations are confined to backend files.
- **Extensibility**: New draw primitives can be added to the vtable without touching any existing backend.

## Architecture

```
Widget Draw
    │
    ▼
renderer_fill_rect() / renderer_blit() / renderer_draw_glyph()
    │
    ▼
renderer_ops_t vtable dispatch
    │
    ├──→ fb_renderer (current)       ──→ Backbuffer (heap) ──→ VRAM (VBE LFB)
    │
    └──→ vulkan_renderer (future)    ──→ Command Buffer   ──→ GPU Queue
```

**Struct diagram:**

```
gui_renderer_t
├── ops:      const renderer_ops_t*   (vtable pointer)
├── backend:  void*                   (opaque backend state)
├── clip:     gui_rect_t              (current clip rect)
├── opacity:  float                   (global opacity multiplier)
├── screen_w: int
└── screen_h: int

renderer_ops_t (vtable)
├── fill_rect    (solid fill, alpha-blended)
├── draw_rect    (1px border via 4 fill_rect calls)
├── blit         (source-over alpha compositing, sub-rect)
├── blit_scaled  (nearest-neighbour scaling)
├── draw_glyph   (PSF1 bitmap font unpacking)
├── set_clip     (push clip rectangle)
├── set_opacity  (set global opacity)
├── present      (flip backbuffer → screen)
└── destroy      (backend teardown)
```

## Lifecycle

```
renderer_create(ops, backend_state, w, h)
    │
    ▼
[alloc gui_renderer_t, store ops/backend/clip/opacity/dims]
    │
    ▼
renderer_fill_rect(...)
renderer_blit(...)
renderer_draw_glyph(...)
    ...
    │
    ▼
renderer_present()
    │
    ▼
renderer_destroy()
    │
    ▼
[ops->destroy() → kfree(backend) → kfree(renderer)]
```

## APIs

### Public (in `renderer.h`)

```c
// Create a renderer instance. backend_state is the opaque backend data.
gui_renderer_t *renderer_create(const renderer_ops_t *ops,
                                 void *backend_state,
                                 int screen_w, int screen_h);

// Destroy renderer. Calls ops->destroy() then frees the struct.
void renderer_destroy(gui_renderer_t *r);

// Convenience inline wrappers (dispatch through ops):
static inline void renderer_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color);
static inline void renderer_draw_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color, int thickness);
static inline void renderer_blit(gui_renderer_t *r, int dest_x, int dest_y,
                                  const uint32_t *src, int src_w, int src_h, int src_pitch,
                                  int src_x, int src_y, int copy_w, int copy_h);
static inline void renderer_draw_glyph(gui_renderer_t *r, int x, int y,
                                        uint32_t fg, uint32_t bg, const glyph_t *g);
static inline void renderer_set_clip(gui_renderer_t *r, gui_rect_t clip);
static inline void renderer_present(gui_renderer_t *r);
```

### Private (backend implements in `renderer_ops_t`)

```c
typedef struct {
    void (*fill_rect)(gui_renderer_t *r, gui_rect_t rect, uint32_t color);
    void (*draw_rect)(gui_renderer_t *r, gui_rect_t rect, uint32_t color, int thickness);
    void (*blit)(gui_renderer_t *r, int dest_x, int dest_y,
                 const uint32_t *src, int src_w, int src_h, int src_pitch,
                 int src_x, int src_y, int copy_w, int copy_h);
    void (*blit_scaled)(gui_renderer_t *r, int dest_x, int dest_y,
                        const uint32_t *src, int src_w, int src_h,
                        float scale_x, float scale_y);
    void (*draw_glyph)(gui_renderer_t *r, int x, int y,
                       uint32_t fg, uint32_t bg, const glyph_t *g);
    void (*set_clip)(gui_renderer_t *r, gui_rect_t clip);
    void (*set_opacity)(gui_renderer_t *r, float opacity);
    void (*present)(gui_renderer_t *r);
    void (*destroy)(gui_renderer_t *r);
} renderer_ops_t;
```

### Data types

```c
typedef struct {
    const uint8_t *bitmap;  // 16 rows × 1 byte each (PSF1)
    int cell_w, cell_h;     // typically 8×16
} glyph_t;

struct gui_renderer {
    const renderer_ops_t *ops;
    void                 *backend;
    gui_rect_t            clip;
    float                 opacity;
    int                   screen_w;
    int                   screen_h;
};
```

## Dependencies

- `stdint.h` — integer types
- `gui/math/rect.h` — `gui_rect_t`, `rect_make`, `rect_zero`, `rect_is_empty`
- Kernel heap (`kheap.h`) — `kmalloc`/`kfree` for object allocation

## Limitations & Trade-offs

| Limitation | Impact |
|---|---|
| Virtual call overhead | Each draw call is an indirect function call through the vtable. Negligible vs. per-pixel work. |
| No batching | Each draw primitive is issued independently. Future backends may buffer commands. |
| Float in API | `blit_scaled` and `opacity` use `float`. The kernel is built `-mno-sse`, so GCC emits soft-float calls. Impact is minor since these ops are infrequent. |
| Single clip rect | No clip rect stack. Widgets must save/restore manually if nesting. |

## Performance & Memory Optimizations

- The `set_clip` wrapper also updates `r->clip` in the generic struct, so backends that clip via `fb_clip()` can read it without another indirection.
- `renderer_fill_rect` has an inline `if (r && r->ops->fill_rect)` guard so NULL ops are silently ignored.
- Backend state is stored as `void*` to avoid any coupling; no type-punning overhead.

## Future Extensions

- **Clip rect stack** (`push_clip`/`pop_clip`) for nested widget hierarchy culling.
- **Command list batching**: queue draw calls and replay them sorted by backend.
- **GPU backend** (see `gpu_backend.md`): translate vtable calls into Vulkan/DirectX command buffers.
- **Deferred rendering**: render to off-screen targets then composite.

## Usage Examples

```c
// Compositor initialisation
gui_renderer_t *r = renderer_create(&fb_ops, fb_state, 1024, 768);

// Widget drawing (called from node_vtable.draw)
void my_widget_draw(node_t *self, gui_renderer_t *r) {
    // Fill background
    renderer_fill_rect(r, rect_make(10, 10, 200, 100), 0xFF1E293B);

    // Draw border
    renderer_draw_rect(r, rect_make(10, 10, 200, 100), 0xFF475569, 2);

    // Blit an image
    renderer_blit(r, 50, 50, image_data, 64, 64, 64*4, 0, 0, 64, 64);

    // Draw text
    renderer_draw_glyph(r, 80, 80, 0xFFFFFFFF, 0x00000000, &font['A']);

    // Set clip for children
    renderer_set_clip(r, rect_make(10, 10, 200, 100));
    node_draw_recursive(self->children[0], r);
    renderer_set_clip(r, rect_zero()); // clear clip
}

// Destroy
renderer_destroy(r);
```
