# Infinite Canvas — LiwusOS Spatial Model

## 1. Objective and Responsibilities

The Canvas is the **spatial substrate** of the LiwusOS GUI. It is not a widget, not a window manager, not a "desktop" — it is the **root scene graph node** (`NODE_CANVAS`) that defines the coordinate space within which all user interface elements exist. The canvas is conceptually infinite; there are no boundaries, no edges, no "screen size" that constrains where content may be placed.

**Responsibilities:**
- Serve as the root of the scene graph hierarchy (`g_scene->root`)
- Define the world coordinate system (all positions are relative to the canvas origin at `(0, 0)`)
- Provide visual feedback of the infinite space via a dot-grid background
- Own no special data beyond `node_t` — the canvas IS a node
- Enable the `canvas_create()` abstraction (future syscall interface) that allows user-space processes to request a node within the world

## 2. Problems Solved

### 2.1 The Screen Coordinate Trap

Traditional window systems (X11, Win32, macOS) anchor all UI to screen coordinates. A window at `(x=100, y=50)` means "100 pixels from the left edge of the monitor." This model breaks when:

- The monitor resolution changes
- The user has multiple monitors with different DPI
- The user wants to zoom in/out
- The user wants to place content beyond a screen's bounds

**Solution**: The canvas defines world-space coordinates. Screen coordinates are a derived quantity computed by the camera each frame. A window at world `(100, 50)` stays there regardless of monitor resolution, zoom level, or camera position.

### 2.2 The Finite Desktop Problem

Traditional desktops have a fixed size (the monitor resolution). Users must manage this finite space by stacking, tiling, minimizing, and virtual-desktop-switching. Each of these mechanisms adds cognitive overhead.

**Solution**: The canvas is infinite. Users pan to navigate. Zoom out for overview. Zoom in for detail. There is no "out of screen space" — only "out of current viewport."

### 2.3 The Window Manager Coupling

In traditional systems, the window manager owns the concept of "where windows go." Windows register with the WM, which decorates, positions, and constrains them. This creates tight coupling between the WM, the compositor, and the application.

**Solution**: The canvas is just a node. Windows are children of the canvas node. The "window manager" (`window_manager.c`) is a 69-line file that handles z-order reordering on focus events — it does not own placement, decoration, or lifecycle. Decoration is handled by each window's own `node_vtable_t::draw`.

## 3. Architecture

### 3.1 Conceptual Model

```
                    ┌─────────────────────────────────────┐
                    │         Infinite Canvas             │
                    │         (NODE_CANVAS)               │
                    │         world origin (0,0)          │
                    │                                     │
                    │  ┌──────────┐                       │
                    │  │  Window  │  ┌─────────────┐      │
                    │  │  at      │  │  Terminal   │      │
                    │  │ (200,150)│  │  at (700,0) │      │
                    │  └──────────┘  └─────────────┘      │
                    │                                     │
                    │           ┌────────────┐            │
                    │           │  Debug     │            │
                    │           │  at (-50,  │            │
                    │           │  -100)     │            │
                    │           └────────────┘            │
                    │                                     │
                    │  (0,0) ●──────────────────────▶ X  │
                    │         │                           │
                    │         │                           │
                    │         ▼ Y                         │
                    │         (infinite)                  │
                    └─────────────────────────────────────┘
```

There is no "screen" in the above diagram. The screen is a rectangle floating somewhere over this plane, determined by the camera's position and zoom.

### 3.2 The Canvas as Root Node

The canvas is the **only** child of `g_scene->root` — in fact, `g_scene->root` IS the canvas node:

```c
// gui_main.c:
node_t *root = node_create(NODE_CANVAS, "canvas");
g_scene->root = root;
```

All visual nodes are descendants of this root. The canvas node itself:

- Has `type = NODE_CANVAS`
- Has `visible = true`
- Has no `vtable` (the compositor draws the background directly)
- Has `width/height` irrelevant (the canvas is infinite; its node dimensions are not used for drawing)
- Has `screen_bounds` covering the entire framebuffer

### 3.3 The Dot-Grid Background

The compositor draws the canvas background each frame in `draw_background()`:

```
draw_background(compositor)
  │
  ├─ Fill entire backbuffer with THEME_COLOR_BACKGROUND (0xFF0B1120)
  │
  ├─ dot_spacing = 40 * zoom_fp / CAMERA_ZOOM_SCALE
  │  (dots get farther apart as you zoom in, closer as you zoom out)
  │
  ├─ origin_screen = camera_world_to_screen(camera, world_origin(0,0))
  │
  ├─ for y from origin_screen.y % dot_spacing to screen_height step dot_spacing:
  │    for x from origin_screen.x % dot_spacing to screen_width step dot_spacing:
  │       backbuf[y * width + x] = THEME_COLOR_BUTTON_BG (0xFF334155)
  │
  └─ Result: An origin-anchored grid that visually communicates
     "this space is infinite; you are at world (X, Y)"
```

The grid serves as **spatial orientation** — as the user pans, the grid scrolls with the world, providing continuous visual feedback of movement through the infinite space.

### 3.4 Coordinate Spaces

```
World Space (Canvas)                Screen Space (Viewport)
┌──────────────────────────────┐    ┌────────────────────┐
│     ● Origin (0,0)           │    │                    │
│                              │    │   ┌──────────┐     │
│          ┌──────────┐        │    │   │  Window  │     │
│          │  Window  │        │    │   │  (what   │     │
│          │  at      │        │    │   │  camera  │     │
│          │  (400,   │        │    │   │  sees)   │     │
│          │   300)   │        │    │   └──────────┘     │
│          └──────────┘        │    │                    │
│                              │    │  ──────+──────     │
│     ● Origin at screen       │    │  dot grid         │
│       (20, 15) due to camera │    │  @ zoom 1.0       │
│       offset + zoom          │    └────────────────────┘
└──────────────────────────────┘
```

The camera projects world→screen:

```
screen_x = (world_x - camera_world_x) * zoom / CAMERA_ZOOM_SCALE
screen_y = (world_y - camera_world_y) * zoom / CAMERA_ZOOM_SCALE
```

Where `camera_world_x = pos_x_fp / CAMERA_POS_SCALE` (recovering integer world position from fixed-point camera position).

## 4. Lifecycle

### 4.1 Canvas Creation

```c
// In gui_main.c during gui_init():
scene_graph_init();                          // creates g_scene
node_t *root = node_create(NODE_CANVAS, "canvas");  // allocates node
g_scene->root = root;                         // registers as root
```

### 4.2 Adding Content to the Canvas

```c
// Create a window as a child of the canvas:
node_t *win = window_node_create("app1", 400, 300, 320, 240, "My App");
node_add_child(g_scene->root, win);

// Create another elsewhere:
node_t *term = terminal_node_create("term1", 1200, 100, 640, 480);
node_add_child(g_scene->root, term);
```

Both windows exist in the infinite canvas at their world coordinates. If the camera is looking at `(0,0)`, `term1` at `(1200, 100)` will be off-screen — panned to later.

### 4.3 Per-Frame Processing

The compositor does NOT treat the canvas specially. The canvas node participates in the same pipeline:

```
compositor_frame():
  ├─ draw_background()       ← fills backbuffer directly (not through node system)
  ├─ node_draw_recursive(    ← walks canvas → children → grandchildren
  │    g_scene->root,        ← the canvas NODE_CANVAS
  │    renderer)
  │    ├─ Canvas has no vtable, so nothing drawn for canvas itself
  │    ├─ Window A:  vtable->draw → title bar + content
  │    ├─ Window B:  vtable->draw → title bar + content
  │    └─ Terminal:  vtable->draw → text grid
  └─ cursor_draw()           ← always on top
```

### 4.4 Canvas Destruction

```c
// In gui_main shutdown:
scene_graph_destroy();  // recursively destroys root (canvas) → all children → frees g_scene
```

## 5. APIs

### 5.1 Canvas-Related API

The canvas has **no dedicated API**. It is manipulated through the standard scene graph API:

```c
// Root canvas access
extern scene_graph_t *g_scene;
node_t *canvas = g_scene->root;  // always NODE_CANVAS

// Adding children to the canvas
node_add_child(canvas, window_node);

// Query canvas children
node_t *child = node_find_by_name(canvas, "app1");

// Move nodes within the canvas
node_set_position(node, new_world_x, new_world_y);

// Hit-testing in canvas space
node_t *hit = node_hit_test(canvas, screen_x, screen_y);
```

### 5.2 Future Syscall API (Runtime SDK)

```c
// User-space creates a node within the kernel's canvas:
int canvas_create(const char *name, int world_x, int world_y,
                  int width, int height);

// Returns a node ID (handle for subsequent operations)
// The kernel creates a NODE_GENERIC child of the canvas root.
```

## 6. Contrast: Infinite Canvas vs Traditional Desktop

### 6.1 Traditional Desktop (Windows/macOS/X11)

```
┌─────────────────────────────────────────────┐
│  Screen (1920×1080)                          │
│                                              │
│  ┌───────────┐  ┌───────────┐              │
│  │  Explorer  │  │  Terminal │              │
│  │  (200,50)  │  │  (700,50) │              │
│  └───────────┘  └───────────┘              │
│                                              │
│  ┌──────────────────────────────────────┐   │
│  │  Browser (100, 400)                  │   │
│  │                                      │   │
│  │                                      │   │
│  │  CANNOT place content outside the    │   │
│  │  screen — window manager constrains  │   │
│  └──────────────────────────────────────┘   │
│                                              │
│  Taskbar: [ Start ] [Explorer] [Term] [Br]  │
└─────────────────────────────────────────────┘

Windows that overflow screen edges are clipped.
Windows cannot exist at negative coordinates.
Minimize hides windows to taskbar (loses spatial context).
```

### 6.2 LiwusOS Infinite Canvas

```
┌─────────────────────────────────────────────────────────┐
│  Infinite Canvas (no bounds)                             │
│                                                          │
│  (-200,-100)              (400, 50)                     │
│  ┌──────────────┐        ┌──────────────┐              │
│  │  Debug Panel │        │  Terminal #1 │              │
│  └──────────────┘        └──────────────┘              │
│                                                          │
│         (200, 150)            (900, 200)                │
│         ┌──────────────┐     ┌──────────────┐          │
│         │  App Window  │     │  Browser     │          │
│         └──────────────┘     └──────────────┘          │
│                                                          │
│                                            (1500, 600)  │
│                                            ┌──────────┐ │
│                                            │  Game    │ │
│                                            └──────────┘ │
│                                                          │
│  ┌───────── Camera Viewport ────────────────────────┐   │
│  │  Shows world region (100,50) to (1124,698)       │   │
│  │  Currently viewing: Terminal #1 + App Window     │   │
│  │  Debug Panel and Browser are off-screen           │   │
│  └───────────────────────────────────────────────────── │
│                                                          │
│  No taskbar needed. Pan to find content.                 │
│  Zoom out to see everything.                             │
└─────────────────────────────────────────────────────────┘
```

### 6.3 Key Differences Summarized

| Aspect | Traditional Desktop | LiwusOS Canvas |
|---|---|---|
| Coordinate model | Screen-anchored (pixels from monitor corner) | World-anchored (pixels from canvas origin) |
| Boundaries | Clipped to monitor resolution | Infinite (no boundaries) |
| Window placement | Window manager decides (constrained) | Application decides (unconstrained) |
| Finding windows | Taskbar, Alt+Tab, Exposé | Pan, zoom, spatial memory |
| Space management | Minimize, maximize, tile | Not needed (infinite space) |
| Multiple monitors | Separate coordinate spaces | One shared world space |
| Zoom | Accessibility feature (magnifier) | First-class navigation primitive |
| "Out of space" | Common problem | Concept does not exist |

## 7. Dependencies

| Component | Depends On | Nature |
|---|---|---|
| Canvas node | `scene_graph_t` | The canvas IS the scene graph root |
| Canvas background | `compositor.c` | The compositor draws it via `draw_background()` |
| Canvas children | `node.h` (node_add_child) | Standard scene graph API |
| Camera projection | `camera.h` | Converts world→screen for all canvas children |
| Theme | `theme_engine.h` | Background color + dot color from palette |
| VGA globals | `vga_fb_width`, `vga_fb_height` | Backbuffer dimensions for background fill |

## 8. Limitations and Trade-offs

| Limitation | Rationale | Impact |
|---|---|---|
| No "minimize" concept | Minimize makes no sense in infinite space; just pan away | Users must learn spatial navigation |
| No window snapping | No screen edges to snap to; arbitrary placement | Intentional — spatial freedom is the goal |
| No workspace isolation | All content exists in one continuous space | Can be noisy with many windows at 1:1 zoom; mitigated by zoom |
| Origin at (0,0) is arbitrary | No natural anchor point | Users naturally create their own anchors (place main window first) |
| No canvas boundaries limit | Nodes can be placed arbitrarily far from origin | `local_x`/`local_y` are signed ints (±2B pixels = enough for any practical use) |
| No multi-canvas support | Single root canvas | Multiple canvases could be added (each as a root-level group) |

## 9. Performance and Memory

- **Canvas node**: `sizeof(node_t)` = ~160 bytes (one allocation)
- **Background fill**: `screen_width × screen_height` pixel writes (optimized with 32-bit stores)
- **Dot grid**: `(screen_width / spacing) × (screen_height / spacing)` dot write operations
- **Child iteration**: Linear in number of direct canvas children (no spatial indexing)
- **No per-canvas memory**: The canvas has no extra state beyond the base `node_t`

### Background Fill Benchmark

At 1920×1080 with 40px dot spacing at zoom 1.0:
- Fill: 2,073,600 pixels = ~0.5 ms (memcpy-like write)
- Dots: ~(1920/40) × (1080/40) = 48 × 27 = 1,296 pixel writes = ~0.002 ms

## 10. Future Extensions

### 10.1 Multiple Canvases

Support for multiple root-level canvases (e.g., "workspace canvas" and "system canvas" for HUD/notifications):

```c
node_t *workspace = node_create(NODE_CANVAS, "workspace");
node_t *system_hud = node_create(NODE_CANVAS, "system_hud");
g_scene->root = node_create(NODE_GROUP, "scene_root");
node_add_child(g_scene->root, workspace);
node_add_child(g_scene->root, system_hud);
```

### 10.2 Canvas Persistence

Serialize canvas state (node positions, sizes, types) to disk so the user's spatial arrangement survives reboots.

### 10.3 Spatial Bookmarks

Save named camera positions for quick navigation:
```c
camera_bookmark_save("project_files");
camera_bookmark_jump("project_files");  // animates camera pan/zoom
```

### 10.4 Canvas Grid Snapping

Optional snap-to-grid for alignment, with configurable grid spacing.

### 10.5 Infinite Canvas Background Images

Support for a tiled background image or a procedural background (e.g., starfield pattern at extreme zoom levels).

### 10.6 User-Space canvas_create() Syscall

```c
// Kernel syscall: user-space requests a node on the canvas
int sys_canvas_create(const char *name, int x, int y, int w, int h);
// Returns a node ID. User-space then uses:
// sys_canvas_move(node_id, x, y)
// sys_canvas_resize(node_id, w, h)
// sys_canvas_set_draw(node_id, pixel_data, rect)
// sys_canvas_destroy(node_id)
```

## 11. Usage Examples

### Creating the canvas (system init)
```c
// gui_init():
scene_graph_init();
node_t *canvas = node_create(NODE_CANVAS, "canvas");
g_scene->root = canvas;

// Add a terminal emulator at world (100, 200)
node_t *term = terminal_node_create("term", 100, 200, 640, 400);
node_add_child(canvas, term);

// Add a window at world (800, 50)
node_t *win = window_node_create("files", 800, 50, 400, 300, "File Browser");
node_add_child(canvas, win);
```

### Navigating the canvas (tool code)
```c
// PanTool: RMB drag → camera_pan()
if (e->type == GUI_EVENT_MOUSE_MOVE && s->dragging) {
    int dx = e->mouse.x - s->drag_start_mx;
    int dy = e->mouse.y - s->drag_start_my;
    int world_dx = -dx * CAMERA_ZOOM_SCALE / cam->zoom_fp;
    int world_dy = -dy * CAMERA_ZOOM_SCALE / cam->zoom_fp;
    cam->pos_x_fp = s->drag_start_px + world_dx * CAMERA_POS_SCALE;
    cam->pos_y_fp = s->drag_start_py + world_dy * CAMERA_POS_SCALE;
}

// ZoomTool: scroll → camera_zoom_at()
if (e->type == GUI_EVENT_MOUSE_SCROLL) {
    int step = e->mouse.dy > 0 ? CAMERA_ZOOM_STEP_FP : -CAMERA_ZOOM_STEP_FP;
    camera_zoom_at(cam, cam->zoom_fp + step, e->mouse.x, e->mouse.y);
}
```

### Placing nodes programmatically
```c
// Place nodes in a 2D grid within the infinite canvas
for (int row = 0; row < 10; row++) {
    for (int col = 0; col < 10; col++) {
        int world_x = col * 350;
        int world_y = row * 280;
        node_t *win = window_node_create("grid_win",
            world_x, world_y, 320, 240, "Grid Window");
        node_add_child(g_scene->root, win);
    }
}
// Camera can fit all:
camera_fit(cam, rects, 100);
```

### Checking if a point is on the canvas (vs off-screen)
```c
// The canvas doesn't care about "on screen" vs "off screen."
// Everything is always on the canvas. The question is whether
// it falls within the camera viewport:
gui_rect_t viewport = camera_viewport_in_world(cam);
for (uint32_t i = 0; i < root->child_count; i++) {
    node_t *child = root->children[i];
    gui_rect_t node_world = rect_make(child->local_x, child->local_y,
                                       child->width, child->height);
    if (rect_intersects(node_world, viewport)) {
        // Node is visible on screen this frame
    }
}
```

---

*Document v1.0 — reflects the infinite canvas model as implemented in `src/kernel/gui/` and `src/kernel/gui/scene/node.c` as of LiwusOS build.*
