# Memory Management for GUI

## Objective

Define the memory ownership, allocation strategy, and lifecycle model for all GUI subsystem data in LiwusOS. Because the GUI runs entirely in the kernel address space with no user-space heap, all allocations come from the kernel heap (`kmalloc`/`kfree`). The design must prevent leaks, dangling pointers, and double-frees while keeping per-frame allocation overhead near zero.

## Problems Solved

- **No garbage collection**: C kernel has no GC. Explicit ownership and deterministic teardown via `node_destroy()`.
- **Kernel heap fragmentation**: All GUI allocations go through `kmalloc`. Without care, frequent widget create/destroy cycles fragment the heap. Memory pools mitigate this.
- **Pointer safety**: Inter-module references use raw `node_t*` pointers. Future user-space will use opaque `uint32_t` IDs.
- **No floating point**: All sizes use `int` or fixed-point (`int32_t` with `TRANSFORM_SCALE=65536`, `CAMERA_POS_SCALE=256`, `CAMERA_ZOOM_SCALE=1024`).

## Architecture

### Ownership Hierarchy

```
gui_main.c (singletons)
 ├── scene_graph_t (g_scene)
 │    └── node_t* root (canvas)
 │         ├── node_t* window
 │         │    ├── node_t* label
 │         │    └── node_t* button
 │         └── node_t* panel
 ├── compositor_t (s_comp)
 │    ├── gui_renderer_t* renderer   → fb_state_t (backbuf)
 │    ├── camera_t* camera
 │    ├── gui_event_bus_t* bus
 │    └── input_manager_t* input
 ├── tool_manager_t (s_tools)
 │    └── tool_t* [select, move, pan]
 ├── focus_manager_t (s_focus)
 └── window_manager_t (s_wm)
```

- `gui_main.c` owns all top-level singletons.
- The **scene graph** (`scene_graph_t`) owns all `node_t` instances via `g_scene->root`. Destroying the root cascades to all descendants.
- The **compositor** owns the renderer, camera, bus, and input — but NOT the scene graph. It holds a `node_t*` reference only.
- **Widgets** allocate `widget_data` (e.g. `button_data_t`, `label_data_t`) via `kmalloc` and attach it to `node_t::userdata`. The node's `vtable->destroy` callback is responsible for freeing it.
- **Tools** are owned by the `tool_manager_t`.

### Memory Layout

```
Kernel Heap
├── scene_graph_t          (≈ 16 bytes)
├── node_t instances       (≈ 200 bytes each, up to scene-wide)
│    └── widget_data       (variable: button_data_t ≈ 56 bytes, label_data_t ≈ 24 bytes)
├── compositor_t           (≈ 2,200 bytes inc. dirty rects + cursor save buf)
├── camera_t               (≈ 32 bytes)
├── gui_renderer_t         (≈ 32 bytes)
│    └── fb_state_t        (≈ 40 bytes)
│         └── backbuf      (W × H × 4 bytes, e.g. 1920×1080×4 = 8,294,400 bytes)
├── gui_event_bus_t        (≈ 256 × 48 + 64 × 32 ≈ 14,336 bytes)
├── input_manager_t        (≈ 400 bytes)
├── tool_manager_t         (≈ 32 bytes)
├── tool_t instances       (≈ 48 bytes each)
├── focus_manager_t        (≈ 24 bytes)
└── window_manager_t       (≈ 24 bytes)
```

Key sizes (approximate, 64-bit):

| Type | Size | Notes |
|------|------|-------|
| `node_t` | 200 bytes | `NODE_MAX_CHILDREN=64` pointers contribute ~512 bytes, total ~712 bytes |
| `button_data_t` | ~56 bytes | text ptr, hover/pressed state, callbacks, font ptr, color |
| `label_data_t` | ~24 bytes | text ptr, color, font ptr |
| `panel_data_t` | ~24 bytes | bg_color, border_color, thickness |
| `window_node_data_t` | ~32 bytes | title ptr, font ptr, pid |
| `camera_t` | 32 bytes | 7 × int32_t + 2 × int + bool |
| `gui_event_t` | 48 bytes | type(4) + priority(4) + target_id(4) + bool + union(32) |
| `gui_transform_t` | 24 bytes | 6 × int32_t |
| `gui_rect_t` | 16 bytes | 4 × int |

### Allocation Points

**Frame-time allocations**: **NONE**. All allocations happen at init time or during widget creation. The compositor frame loop (`compositor_frame`) performs zero heap allocations.

### Deallocation

- `node_destroy()`: Recursively destroys children (depth-first), calls `vtable->destroy`, calls `kfree(node)`.
- `compositor_destroy()`: Frees compositor struct. Does NOT destroy owned subsystems (caller must destroy renderer, camera, etc. separately).
- `scene_graph_destroy()`: Destroys root node, frees `g_scene`.
- Widget destructors: Free `userdata`, then any sub-allocations (e.g., `kfree(d->text)`).

## APIs

### Public

```c
// Scene graph lifecycle
void scene_graph_init(void);
void scene_graph_destroy(void);

// Node lifecycle
node_t *node_create(node_type_t type, const char *name);    // kmalloc + memset
void    node_destroy(node_t *node);                          // recursive destroy + kfree

// Hierarchy
bool    node_add_child(node_t *parent, node_t *child);
void    node_remove_child(node_t *parent, node_t *child);
```

### Private / Widget-specific

```c
// Widget data allocations inside *_create() functions:
button_data_t *d = kmalloc(sizeof(button_data_t));
d->text = kmalloc(strlen(text) + 1);  // if text provided

// Destroy callbacks free their own allocations:
static void button_destroy(node_t *self) {
    kfree(d->text);
    kfree(d);
}
```

## Dependencies

- `kheap.h` — `kmalloc`, `kfree` (kernel heap allocator)
- `string.h` — `memset`, `strlen`, `strcpy`
- No dependency on user-space heap or page allocator

## Limitations / Trade-offs

| Trade-off | Rationale |
|-----------|-----------|
| Raw pointers everywhere | Kernel-space only. No MMU separation between GUI components. Simple, fast, but unsafe for user-space. |
| No reference counting | Tree ownership is unambiguous — each node has exactly one parent. No shared ownership needed yet. |
| No memory pools (yet) | Current allocation volume is low. Pools will be added for `node_t` and small widget data to reduce fragmentation. |
| Backbuffer is single large allocation | 8 MB for 1920×1080. Fragments the heap but is allocated once. |
| `NODE_MAX_CHILDREN = 64` | Fixed-size children array in `node_t`. Prevents dynamic allocation per add/remove. Wastes ~512 bytes per node when most windows have 1-5 children. |

## Performance / Memory Optimizations

- **Object Pool (future)**: Pre-allocate a slab of `node_t` structures. `node_create` returns from the slab; `node_destroy` returns to the slab. Eliminates `kmalloc` overhead for hot widget creation (e.g., dropdown menus, tooltips).
- **Small allocation pool**: Widget data structs (`button_data_t`, etc.) are all < 64 bytes. A dedicated slab reduces fragmentation.
- **String interning**: Widget title and label strings are often repeated. Intern common strings to reduce allocation overhead.
- **Backbuffer reuse**: The single `kmalloc` for the backbuffer at init time is never freed until `fb_destroy`. Zero allocation pressure at runtime.
- **Static arrays**: `s_palette[THEME_COLOR_MAX]`, `s_animations[MAX_ANIMATIONS]`, `s_default_font[256]` are static globals — no heap overhead.

## Future Extensions

| Feature | Approach |
|---------|----------|
| Memory pools | `slab_allocator_create(sizeof(node_t), 128)` — fast O(1) alloc/free |
| User-space handle system | `uint32_t` handles with lookup table in kernel, preventing direct pointer exposure |
| Reference-counted assets | Font glyphs and images loaded once, shared via ref-counted handles |
| Double-buffered scene graph | Clone-on-write for transactional updates |
| GPU buffer allocation | `kmalloc` for staging buffers, DMA for GPU-visible memory |

## Usage Examples

```c
// Create a scene and populate it (all kmalloc'd)
scene_graph_init();
node_t *root = node_create(NODE_CANVAS, "canvas_root");
g_scene->root = root;

node_t *btn = button_create("ok_btn", 10, 10, 80, 30, "OK");
node_add_child(root, btn);

// Destruction: one call frees the entire tree
node_destroy(root);  // recursively frees btn and its button_data_t

// Scene teardown
scene_graph_destroy();  // frees g_scene itself
```
