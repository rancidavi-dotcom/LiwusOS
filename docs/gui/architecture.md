# GUI Architecture — LiwusOS

## 1. Objective and Responsibilities

The LiwusOS GUI architecture is a **module-passing, event-driven, scene-graph-based** compositing system that runs as a single kernel task. Its objective is to provide a complete graphical user interface — windows, widgets, input handling, animations, layout — on top of a software framebuffer, using no floating-point in the kernel, no memory-mapped GPU, and no user-space display server.

**Responsibilities:**
- Own the frame loop (`compositor_frame()`)
- Manage the scene graph (a tree of `node_t` objects)
- Dispatch input events through the Event Bus
- Render the scene to a software backbuffer
- Present the backbuffer to the VBE LFB framebuffer
- Provide widget toolkit primitives (button, label, panel, window)
- Support animated transitions (tweening engine)
- Manage camera navigation (pan, zoom, inertia)
- Handle layout (flexbox-like VBOX/HBOX)
- Route keyboard focus

## 2. Problems Solved

- **Monolithic kernel GUI without user-space**: All rendering, input, and event processing lives in the kernel. There is no Wayland/X11 server, no display socket, no IPC for GUI operations. This eliminates context switches for every draw operation.
- **No FPU in kernel**: Fixed-point arithmetic throughout. The camera, transforms, and layout all use integer math with configurable fractional precision.
- **Deterministic frame timing**: The compositor is a single task that yields voluntarily (`switch_task()`) after each frame. No preemption during compositing.
- **No double-buffering complexity**: A single backbuffer is filled each frame and memcpy'd to VRAM. The cursor is saved/restored around the backbuffer to avoid ghost trails.
- **Decoupled input from widgets**: Hardware drivers are read only by the Input Manager. Widgets and tools receive events through the Event Bus — they never call `get_mouse_x()` directly.

## 3. Architecture

### 3.1 Module Map

```
┌─────────────────────────────────────────────────────────────────────┐
│                        gui_main.c                                    │
│              Bootstrap & Task Entry Point                            │
│  gui_init() → ordered module init → gui_compositor_task()            │
└──────┬────────────────────────────────────────────────────┬──────────┘
       │                                                    │
       ▼                                                    ▼
┌───────────────────────┐                    ┌─────────────────────────┐
│   scene/              │                    │   render/               │
│   ┌─────────────────┐ │                    │   ┌───────────────────┐ │
│   │ node.c          │ │                    │   │ renderer.h/.c     │ │
│   │  (scene graph)  │ │                    │   │  (abstract ops)   │ │
│   └────────┬────────┘ │                    │   └────────┬──────────┘ │
│   ┌────────┴────────┐ │                    │   ┌────────┴──────────┐ │
│   │ camera.c        │ │                    │   │ fb_renderer.c     │ │
│   │  (viewport)     │ │                    │   │  (software fb)    │ │
│   └─────────────────┘ │                    │   └───────────────────┘ │
│   math/               │                    │   ┌───────────────────┐ │
│   ┌─────────────────┐ │                    │   │ compositor.c      │ │
│   │ transform.h     │ │                    │   │  (frame loop)     │ │
│   │ rect.h           │◄┼──────────────────────►│                   │ │
│   └─────────────────┘ │                    │   └───────────────────┘ │
└───────────────────────┘                    └─────────────────────────┘
         ▲                                                  ▲
         │                                                  │
         ▼                                                  ▼
┌───────────────────────┐                    ┌─────────────────────────┐
│   core/               │                    │   input/                │
│   ┌─────────────────┐ │                    │   ┌───────────────────┐ │
│   │ event_bus.c     │ │                    │   │ input_manager.c   │ │
│   │  (pub/sub)      │◄├───────────────────────►│  (hardware poll)  │ │
│   └─────────────────┘ │                    │   └───────────────────┘ │
│   ┌─────────────────┐ │                    │   tools/                │
│   │ animation_engine│ │                    │   ┌───────────────────┐ │
│   │  (tweening)     │ │                    │   │ tool_manager.c    │ │
│   └────────┬────────┘ │                    │   │  (event routing)  │ │
│   ┌────────┴────────┐ │                    │   ├───────────────────┤ │
│   │ theme_engine.c  │ │                    │   │ pan_tool.c        │ │
│   │  (palette)      │ │                    │   │ select_tool.c     │ │
│   └─────────────────┘ │                    │   │ move_tool.c       │ │
└───────────────────────┘                    │   └───────────────────┘ │
         ▲                                  └─────────────────────────┘
         │
         ▼
┌───────────────────────┐
│   widgets/            │
│   ┌─────────────────┐ │
│   │ window_node.c   │ │
│   │ button.c        │ │
│   │ label.c         │ │
│   │ panel.c         │ │
│   └─────────────────┘ │
│   layout/             │
│   ┌─────────────────┐ │
│   │ layout_engine.c │ │
│   └─────────────────┘ │
│   window/             │
│   ┌─────────────────┐ │
│   │ window_manager.c│ │
│   │ focus_manager.c │ │
│   └─────────────────┘ │
│   assets/             │
│   ┌─────────────────┐ │
│   │ asset_manager.c │ │
│   └─────────────────┘ │
└───────────────────────┘
```

### 3.2 Frame Loop Data Flow

```
┌─────────────────────────────────────────────────────────────────────┐
│                  compositor_frame() — Single Frame                  │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐     │
│  │  Input   │    │  Event   │    │  Camera  │    │  Anim    │     │
│  │  Poll    │───▶│  Dispatch│───▶│  Update  │───▶│  Tick    │     │
│  └──────────┘    └──────────┘    └──────────┘    └──────────┘     │
│       │               │               │               │            │
│       │    Post to    │  Subscribers: │  Decay        │  Advance   │
│       │  Event Bus:   │  ToolManager  │  inertia      │  active    │
│       │  MOUSE_MOVE   │  FocusManager │  (friction)   │  tweens    │
│       │  MOUSE_DOWN   │  WindowMgr    │               │            │
│       │  KEY_DOWN     │  Widgets      │               │            │
│       │  ...          │               │               │            │
│       ▼               ▼               ▼               ▼            │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                 Transform Pass                               │  │
│  │  node_update_transforms(root, identity)                      │  │
│  │  Walks tree depth-first, concatenates local→world transforms │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│                              ▼                                      │
│  ┌──────────────────────────────────────────────────────────────┐  │
│  │                 Render Pass                                  │  │
│  │  1. Restore saved cursor pixels (if any)                     │  │
│  │  2. draw_background() — dot grid with slate-900 fill         │  │
│  │  3. node_draw_recursive(root, renderer) — all visible nodes  │  │
│  │     Each node: transform_apply → camera_world_to_screen →    │  │
│  │                renderer_fill_rect / blit / draw_glyph        │  │
│  │  4. cursor_draw(mx, my) — save pixels under cursor, paint    │  │
│  │  5. renderer_present() — memcpy backbuffer → VRAM            │  │
│  └──────────────────────────────────────────────────────────────┘  │
│                              │                                      │
│                              ▼                                      │
│                     switch_task() — yield                          │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.3 Initialization Order

```
gui_init()
  │
  ├─ 1. scene_graph_init()          ← allocate singleton g_scene
  ├─ 2. theme_engine_init()         ← set default color palette
  ├─ 3. animation_engine_init()     ← clear animation table
  ├─ 4. event_bus_create()          ← allocate ring buffer + subscriber table
  ├─ 5. input_manager_create(bus)   ← alloc state, store bus ref
  ├─ 6. camera_create(w, h)         ← alloc camera, set default zoom=1.0
  ├─ 7. fb_renderer_create()        ← alloc backbuffer, point to VRAM
  ├─ 8. Build scene tree:
  │     ├─ node_create(NODE_CANVAS, "canvas")  → g_scene->root
  │     └─ window_node_create("demo_win", ...)  → children
  │         ├─ label_create()
  │         ├─ button_create()
  │         └─ panel_create()
  │         └─ layout_engine_compute(win)
  ├─ 9. tool_manager_create(bus, cam, root)
  │     ├─ select_tool_create()
  │     ├─ move_tool_create()
  │     └─ pan_tool_create()
  ├─10. focus_manager_create(bus, root)
  ├─11. window_manager_create(bus, root)
  └─12. compositor_create(renderer, cam, bus, input, root)

gui_compositor_task():
  while (1) compositor_frame(compositor);
```

### 3.4 Module Dependency Graph

```
                  gui_main
                 /    |    \
                /     |     \
               /      |      \
         compositor   |   tool_manager
         /  |  \      |      |
    renderer |   \    |   tool (base)
       |     |    \   |   /  |  \
  fb_renderer|   node  pan  select  move
       |     |   / \      |       |
       vga   event_bus  camera   scene_root (node)
                |    \
         input_manager \
                |        \
            mouse/keyboard focus_manager
                        window_manager
                      animation_engine
                      theme_engine
                      layout_engine
                      asset_manager
```

### 3.5 A note on key_module_pairs

```
 Module Pair       | Producer   | Consumer   | Coupling
───────────────────┼────────────┼────────────┼────────────────
 Input → EventBus  | manager.c  | bus        | Post-only via API
 EventBus → Tools  | bus        | tool_mgr   | Subscribe callback
 Node → Compositor | node.c     | compositor | node_update/Recurse
 Compositor → Rend | compositor | renderer   | ops->fill_rect etc.
 Camera → Widget   | camera.c   | widget     | world↔screen via API
 Layout → Node     | layout.c   | node_set   | Calls set_position
 Animation → Node  | anim.c     | node_set   | Mutates node fields
```

## 4. Module Descriptions

### 4.1 Scene Graph (`scene/`)

**Files**: `node.h`, `node.c`, `camera.h`, `camera.c`, `math/rect.h`, `math/transform.h`

The scene graph is a rooted tree of `node_t` objects. Each node has a local position, a cached world transform (3×3 affine matrix), a bounding box in screen space, and a vtable for polymorphic draw/event/layout/destroy. The root is always `NODE_CANVAS`.

`node_update_transforms()` walks the tree once per frame, concatenating local translations into `world_transform`. `node_draw_recursive()` walks visible nodes and calls `vtable->draw`. Hit-testing walks children in reverse z-order.

**APIs**: `scene_graph_init()`, `scene_graph_destroy()`, `node_create()`, `node_destroy()`, `node_add_child()`, `node_remove_child()`, `node_find_by_name()`, `node_find_by_id()`, `node_hit_test()`, `node_set_position()`, `node_set_size()`, `node_mark_dirty()`, `node_update_transforms()`, `node_draw_recursive()`

### 4.2 Canvas Engine (implicit in `scene/`)

The canvas IS the root scene graph node (`NODE_CANVAS`). It has no special type or structure beyond `node_t`. Its background is drawn by `compositor.c:draw_background()` which fills slate-900 and renders an origin-anchored dot grid that scales with camera zoom.

### 4.3 Renderer (`render/renderer.h`, `render/fb_renderer.c`)

**Abstract backend interface** via `renderer_ops_t` vtable. The compositor and all widgets call **only** `renderer_fill_rect()`, `renderer_draw_rect()`, `renderer_blit()`, `renderer_draw_glyph()`, etc. The concrete `fb_renderer.c` implements these by writing to a heap-allocated backbuffer, then `fb_present()` does a word-by-word memcpy to the VRAM pointer from `vga.c`.

**Alpha blending**: Every pixel write checks the alpha channel. Fully opaque (0xFF) overwrites. Fully transparent (0x00) skips. Partial alpha performs `alpha_blend()` in integer arithmetic.

### 4.4 Compositor (`render/compositor.c`)

The frame loop. Called in an infinite loop from `gui_compositor_task()`. Sequence: input poll → event dispatch → camera inertia → animation tick → transform pass → (restore cursor) → background draw → node draw recursive → cursor draw → present → yield.

### 4.5 Camera (`scene/camera.c`)

Fixed-point camera. Stores `pos_x_fp`/`pos_y_fp` (8 fractional bits) and `zoom_fp` (10 fractional bits, scale 1024). Provides world↔screen conversion via inline functions that use only `int64_t` intermediate arithmetic (no floats). Supports pan with inertia (`vel_x_fp` decays by `870/1024` each frame), zoom-at-pivot, center-on, fit-all, and reset.

### 4.6 Widget Toolkit (`widgets/`)

Four concrete widget types:

| Widget | Node Type | Userdata | vtable.draw | vtable.on_event |
|---|---|---|---|---|
| WindowNode | `NODE_WINDOW` | `window_node_data_t` (title, font, pid) | Title bar + traffic-light + border | Close button hit test |
| Button | `NODE_BUTTON` | `button_data_t` (text, hover, pressed, anim color) | Rounded rect + centered text | Hover/press/click with animation |
| Label | `NODE_LABEL` | `label_data_t` (text, color, font) | Glyph-at-a-time text render | None |
| Panel | `NODE_PANEL` | `panel_data_t` (bg, border) | Filled rect + optional border | None |

### 4.7 Animation Engine (`core/animation_engine.c`)

Fixed-size table (`MAX_ANIMATIONS=64`) of active tweens. Each animation targets a node property (x, y, width, height, opacity_fp, color). Linear interpolation over a specified number of frames. Overwrites existing animations for the same `(node, prop)` pair. Called from `compositor_frame()` before the render pass.

### 4.8 Input Manager (`input/input_manager.c`)

The **sole** reader of hardware state (`mouse.c`, `keyboard.c`). Polls each frame, diffs against previous state, and posts typed delta events to the Event Bus. Normalizes LCtrl as additional left-click (for laptop trackpads without physical buttons). Tracks modifier keys in a single byte.

### 4.9 Event Bus (`core/event_bus.c`)

Typed, prioritized publish/subscribe system with a fixed-size ring buffer (`GUI_EVENT_QUEUE_CAPACITY=256`). `event_bus_post()` is lock-free (single producer, single consumer in kernel context). `event_bus_dispatch()` drains the ring into a sorted snapshot (insertion sort by priority), then iterates subscribers. Subscribers are registered with a filter type or `GUI_EVENT_NONE` (wildcard).

### 4.10 Tool Manager (`input/tools/`)

Three tools registered in priority order: MoveTool > SelectTool > PanTool. The ToolManager subscribes to all events on the bus. Each tool receives events in order; if a tool returns `true` (consumed), propagation stops. PanTool handles RMB drag (pan), keyboard zoom (+/-), H (home), F (fit). SelectTool handles LMB hit-test and hover tracking. MoveTool handles title-bar drag.

### 4.11 Layout Engine (`layout/layout_engine.c`)

Flexbox-like layout for nodes. Supports `LAYOUT_VBOX` (vertical stacking) and `LAYOUT_HBOX` (horizontal stacking). Children can have `flex_weight` (proportional space distribution), `layout_align` (START/CENTER/END/STRETCH), and `margin`/`padding`. Absolute layout skips the engine.

### 4.12 Asset Manager (`assets/asset_manager.c`)

Loads the embedded PSF1 bitmap font (8×16 glyphs) from the kernel binary. Caches 256 `glyph_t` structures pointing to the ROM font data.

### 4.13 Theme Engine (`core/theme_engine.c`)

Global color palette array (`THEME_COLOR_MAX=12` entries). Dark theme with slate/indigo glassmorphism: `0xFF0B1120` (canvas bg), `0xCC1E293B` (window bg, 80% opacity), `0xFFF8FAFC` (text), `0xEF4444` (close button red).

### 4.14 Focus Manager (`window/focus_manager.c`)

Subscribes to all events. On `MOUSE_DOWN`, performs hit-test and sets focus to the clicked node. On keyboard events, dispatches directly to the focused node's `vtable->on_event`. Tab key triggers `focus_manager_focus_next()` (stub).

### 4.15 Window Manager (`window/window_manager.c`)

Subscribes to `GUI_EVENT_WIN_FOCUS`. On focus event, calls `window_manager_bring_to_front()` which moves the focused node to the end of its parent's children array (highest z-order).

### 4.16 Runtime SDK (future)

User-space applications communicate via syscalls. `canvas_create()` is the syscall that allocates a node within the kernel's scene graph. User-space passes world coordinates. The kernel returns a node ID. All subsequent operations (move, resize, draw text, handle events) go through syscall gates.

## 5. APIs

### 5.1 Public (Stable) APIs

```c
// Scene Graph
void scene_graph_init(void);
void scene_graph_destroy(void);
node_t *node_create(node_type_t type, const char *name);
void node_destroy(node_t *node);
bool node_add_child(node_t *parent, node_t *child);
void node_remove_child(node_t *parent, node_t *child);
node_t *node_find_by_name(node_t *root, const char *name);
node_t *node_find_by_id(node_t *root, uint32_t id);
node_t *node_hit_test(node_t *root, int screen_x, int screen_y);
void node_set_position(node_t *node, int x, int y);
void node_set_size(node_t *node, int w, int h);
void node_mark_dirty(node_t *node, uint32_t flags);
void node_update_transforms(node_t *node, gui_transform_t parent_world);
void node_draw_recursive(node_t *node, struct gui_renderer *r);

// Compositor
compositor_t *compositor_create(...);
void compositor_frame(compositor_t *c);

// Renderer
gui_renderer_t *renderer_create(const renderer_ops_t *ops, ...);
void renderer_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color);
void renderer_draw_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color, int t);
void renderer_blit(...);
void renderer_draw_glyph(...);
void renderer_set_clip(gui_renderer_t *r, gui_rect_t clip);
void renderer_present(gui_renderer_t *r);

// Camera
camera_t *camera_create(int screen_w, int screen_h);
void camera_pan(camera_t *cam, int dx, int dy);
void camera_zoom_at(camera_t *cam, int new_zoom_fp, int pivot_sx, int pivot_sy);
int camera_world_to_screen_x(const camera_t *c, int wx);
int camera_screen_to_world_x(const camera_t *c, int sx);

// Event Bus
gui_event_bus_t *event_bus_create(void);
gui_subscription_id_t event_bus_subscribe(gui_event_bus_t *bus, gui_event_type_t type, ...);
bool event_bus_post(gui_event_bus_t *bus, const gui_event_t *event);
uint32_t event_bus_dispatch(gui_event_bus_t *bus);

// Widgets
node_t *window_node_create(...);
node_t *button_create(const char *name, int x, int y, int w, int h, const char *text);
node_t *label_create(const char *name, int x, int y, const char *text, uint32_t color);
node_t *panel_create(const char *name, int x, int y, int w, int h, uint32_t bg_color);

// Animation
void animation_start(node_t *node, anim_prop_t prop, void *custom, int start, int end, int frames);
bool animation_engine_tick(void);

// Theme
uint32_t theme_engine_get_color(theme_color_id_t id);
void theme_engine_set_color(theme_color_id_t id, uint32_t color);
```

### 5.2 Private (Internal) APIs

```c
// Compositor internals
void compositor_invalidate(compositor_t *c, const gui_rect_t *rect);
void compositor_set_cursor(compositor_t *c, gui_cursor_t type);

// Backbuffer direct access (compositor only)
uint32_t *fb_renderer_backbuf(gui_renderer_t *r);

// Tool vtable
typedef struct {
    const char *name;
    void (*on_activate)(tool_t *self);
    void (*on_deactivate)(tool_t *self);
    bool (*on_event)(tool_t *self, const gui_event_t *event);
    void (*destroy)(tool_t *self);
} tool_vtable_t;

// Node vtable
typedef struct {
    void  (*draw)(node_t *self, struct gui_renderer *r);
    bool  (*on_event)(node_t *self, const gui_event_t *event);
    void  (*layout)(node_t *self);
    void  (*destroy)(node_t *self);
} node_vtable_t;
```

## 6. Dependencies and Coupling

| Module | Depends On | Coupling Type |
|---|---|---|
| compositor | renderer, camera, event_bus, input_manager, node | Strong (direct struct access) |
| node | rect, transform, event_bus | Moderate (includes headers) |
| camera | rect | Weak (inline functions) |
| fb_renderer | renderer (ops), vga globals | Loose (via ops table) |
| input_manager | event_bus, mouse, keyboard | Moderate (posts events) |
| event_bus | None (self-contained) | Zero (but many depend on it) |
| tool_manager | event_bus, camera, node, tool | Moderate (subscribes/owns) |
| focus_manager | event_bus, node | Loose (subscribes/calls) |
| window_manager | event_bus, node | Loose (subscribes/mutates) |
| animation_engine | node | Loose (mutates node fields) |
| layout_engine | node | Loose (calls set_position) |
| theme_engine | None | Zero (static array) |
| asset_manager | None (linked font data) | Zero |
| widgets | node, renderer, compositor, theme, assets | Moderate (uses global g_compositor) |

**Key coupling note**: Widgets access the global `g_compositor` to get the camera for world→screen projection. This is a design smell (hidden global dependency) accepted for simplicity in a single-kernel-task system. A future refactor should pass the camera through the draw context.

## 7. Limitations and Trade-offs

| Limitation | Rationale | Mitigation |
|---|---|---|
| Full redraw every frame | Simplicity; dirty-rect tracking is complex | Phase 4 will add dirty-rect culling |
| Single backbuffer | No VRAM double-buffer in VBE LFB | memcpy is fast on x86-64; 1920×1080×4 = 8 MB |
| Bitmap font only (8×16) | PSF1 is trivial to parse; no TTF engine | SDF font rendering planned for Phase 4 |
| No hardware acceleration | No GPU; software framebuffer only | Backend abstraction (vulkan_renderer possible) |
| Linear transform chain | No rotation/shear in common use | `transform_concat` supports it; widgets don't use yet |
| Global g_compositor | Simplifies widget camera access | Refactor to thread draw_context through vtable |
| No text shaping | ASCII/latin-1 only | UTF-8 + SDF planned |
| Single user/task | Monolithic kernel; no multi-user | Not a goal for v1 |

## 8. Performance and Memory

### 8.1 Memory Budget

| Component | Allocation | Size |
|---|---|---|
| scene_graph_t | 1× kmalloc | 16 bytes |
| node_t (per node) | 1× kmalloc | ~160 bytes each |
| node userdata | 1× kmalloc (per widget) | 32–128 bytes |
| camera_t | 1× kmalloc | ~40 bytes |
| input_manager_t | 1× kmalloc | ~2.4 KB |
| event_bus_t | 1× kmalloc | ~20 KB |
| compositor_t | 1× kmalloc | ~6 KB |
| fb_state (backbuffer) | 1× kmalloc | `width × height × 4` |
| animation table | Static (BSS) | ~3 KB |
| theme palette | Static (BSS) | 48 bytes |
| font glyphs | Static (BSS + ROM) | ~8 KB |

### 8.2 Frame Budget (target: 60 FPS = 16.6 ms)

| Phase | Typical Cost | Notes |
|---|---|---|
| Input poll | ~0.01 ms | 128 scancode reads + mouse |
| Event dispatch | ~0.01 ms | Ring buffer drain + subscribers |
| Camera update | ~0.001 ms | Fixed-point clamp |
| Animation tick | ~0.001 ms | 64-entry linear |
| Transform pass | ~0.01 ms | Linear in node count |
| Background fill | ~0.5–2 ms | 1024×768 = 3M pixels fill |
| Node draw | ~0.1–5 ms | Depends on widget count + text |
| Cursor save/restore | ~0.01 ms | 16×16 save + 16×16 paint |
| Present (memcpy) | ~0.5–2 ms | 1024×768×4 = 3 MB |
| **Total** | **~1.2–9 ms** | Well within 16.6 ms budget |

### 8.3 Optimizations

- **Fixed-point math**: Avoids FPU context save/restore in interrupt handlers. All camera/transform math uses `int64_t` intermediates to prevent overflow.
- **Single allocation per node**: `kmalloc` is called once for the node_t struct and once for userdata. No per-frame allocation.
- **Incremental transforms**: `node_mark_dirty(NODE_DIRTY_TRANSFORM)` propagates to children but NOT up the tree. Transform update visits only dirty nodes.
- **Cursor save/restore**: Saves 16×16 pixels under cursor to a stack buffer, avoiding full-screen repaint for cursor-only changes.
- **Event ring buffer**: Fixed-size, no dynamic allocation during `post()`. Single-producer/single-consumer avoids spinlock in common case.
- **Backbuffer → VRAM memcpy**: Word-by-word copy (32-bit at a time) using the CPU's fastest memory path. No MMIO for pixel writes.

## 9. Future Extensions

- **Dirty-rect tracking**: Replace full-redraw with rect-union based partial repaints.
- **SDF font rendering**: Signed Distance Fields for scalable text at any zoom level.
- **Vulkan renderer backend**: New `renderer_ops_t` implementation for GPU-accelerated rendering.
- **Multi-monitor**: Multiple cameras sharing the same scene graph, each projecting to a different framebuffer.
- **User-space SDK**: `canvas_create()` syscall returns a node ID; user-space builds widget trees via syscall gates.
- **Composition effects**: Drop shadows, blur behind windows, animated transitions.
- **Touch input**: Extend Input Manager to handle touch events.
- **Wayland protocol support**: Bridge layer so Linux Wayland clients can display on the LiwusOS canvas.

## 10. Usage Examples

### Bootstrap (gui_main.c pattern)
```c
void gui_init(void) {
    scene_graph_init();
    theme_engine_init();
    animation_engine_init();

    gui_event_bus_t *bus = event_bus_create();
    input_manager_t *im = input_manager_create(bus);
    camera_t *cam = camera_create(1024, 768);
    gui_renderer_t *r = fb_renderer_create();

    node_t *root = node_create(NODE_CANVAS, "canvas");
    g_scene->root = root;

    node_t *win = window_node_create("main", 100, 100, 400, 300, "My App");
    node_t *btn = button_create("ok_btn", 0, 0, 80, 30, "OK");
    node_add_child(win, btn);
    node_add_child(root, win);
    layout_engine_compute(win);

    compositor_t *comp = compositor_create(r, cam, bus, im, root);
}

void gui_compositor_task(void) {
    while (1) compositor_frame(g_compositor);
}
```

### Adding a custom widget
```c
static void my_draw(node_t *self, gui_renderer_t *r) {
    camera_t *cam = g_compositor->camera;
    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int sx = camera_world_to_screen_x(cam, pt.x);
    int sy = camera_world_to_screen_y(cam, pt.y);
    int sw = camera_scale(cam, self->width);
    int sh = camera_scale(cam, self->height);
    self->screen_bounds = rect_make(sx, sy, sw, sh);
    renderer_fill_rect(r, self->screen_bounds, 0xFF334155);
}

static const node_vtable_t my_vtable = {
    .draw = my_draw,
    .on_event = NULL,
    .layout = NULL,
    .destroy = NULL, // no userdata to free
};

node_t *my_widget_create(int x, int y, int w, int h) {
    node_t *n = node_create(NODE_GENERIC, "my_widget");
    n->vtable = &my_vtable;
    n->local_x = x; n->local_y = y;
    n->width = w; n->height = h;
    return n;
}
```

---

*Document v1.0 — reflects `src/kernel/gui/` as of LiwusOS build.*
