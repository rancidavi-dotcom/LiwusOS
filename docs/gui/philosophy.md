# Infinite Canvas Philosophy — LiwusOS GUI

## 1. Objective and Responsibilities

The Infinite Canvas is the **foundational metaphor** of the LiwusOS graphical user interface. Unlike traditional desktop environments that manage a finite screen partitioned into overlapping windows, LiwusOS presents the user with a **continuous, unbounded two-dimensional space** — the Canvas. All visual elements (windows, panels, buttons, terminals, overlays) exist as nodes within this space. There is no concept of "screen coordinates" in the application model; every node lives in **world-space**, and the screen is merely the current viewport into that infinite world.

**Responsibilities:**
- Define the spatial model for all GUI objects (world-space, not screen-space)
- Eliminate the distinction between "window manager" and "compositor"
- Provide a unified coordinate system where pan, zoom, and navigation are intrinsic
- Enable spatial organization patterns impossible on fixed-screen desktops
- Act as the root scene graph node from which all UI descends

## 2. Problems Solved

### Traditional Desktop Problems

| Problem | Traditional OS (Windows/macOS/X11) | LiwusOS Infinite Canvas |
|---|---|---|
| **Window management overhead** | Users must tile, minimize, maximize, restore, and arrange windows within finite screen real estate | Windows are placed in infinite space; the user navigates by panning and zooming — no "window management" required |
| **Spatial disorganization** | Windows stack in z-order; finding a window requires scanning a taskbar or alt-tabbing | Spatial memory: users place things where they want them in 2D space and navigate visually |
| **Screen real estate** | Fixed at monitor resolution; virtual desktops are isolated silos | Infinite canvas provides unlimited space; zoom out to overview, zoom in for detail |
| **Multi-monitor complexity** | Each monitor has separate coordinate space; window spanning is fragile | One continuous world-space; monitors are just additional viewports into the same canvas |
| **Context switching** | Alt-tab between isolated applications | Pan between related content; spatial relationships preserve context |
| **Window decorations** | Every window has title bar, close/min/max buttons consuming content area | Windows are just formatted nodes; decorations are part of the node's draw implementation, not a window manager concern |

### Specific Pain Points Eliminated

1. **No window manager**: The camera is the only "window manager." There is no `wm_window_create()` — only `canvas_create()` which creates a node_t within the scene graph.
2. **No virtual desktops**: The canvas *is* the virtual desktop — infinite, continuous. Zoom out to see everything.
3. **No taskbar required**: Spatial navigation replaces alt-tab. The user pans to find content.
4. **No screen coordinate model**: All application code uses world-space coordinates. Screen projection is a camera concern.
5. **No clipping to screen**: Windows outside the viewport simply aren't drawn; they continue to exist in world-space.

## 3. Key Differences from Mainstream Systems

```
┌─────────────────────────────────────────────────────────────────────┐
│                    Coordinate Model Comparison                      │
├──────────────┬──────────────────┬───────────────────┬───────────────┤
│   Aspect     │   Windows/macOS  │  X11/Wayland      │  LiwusOS      │
├──────────────┼──────────────────┼───────────────────┼───────────────┤
│ Spatial model│ Screen-space     │ Screen-space      │ World-space   │
│ Window ID    │ HWND / NSWindow  │ XWindow / wl_surface │ node_t*     │
│ Coordinate   │ Fixed pixels     │ Fixed pixels      │ Fixed-point   │
│ Viewport     │ Monitor          │ Monitor           │ Camera        │
│ Zoom         │ Accessibility    │ Not supported     │ First-class   │
│ Decoration   │ Window manager   │ Compositor/decoration │ node vtable │
│ Layout       │ Absolute/win32   │ X11 geometry mgr │ Flexbox nodes │
│ Rendering    │ GDI/DirectX/Quartz│ Compositor-based │ Scene graph   │
└──────────────┴──────────────────┴───────────────────┴───────────────┘
```

### Philosophical Divergence

**Windows/macOS/X11**: "The screen is a finite resource. Applications compete for it. The window manager arbitrates."

**LiwusOS**: "Space is infinite. Applications coexist within it. The camera reveals what is relevant."

## 4. Fundamental Principles

### 4.1 Spatial Continuity

All GUI state exists in a continuous 2D plane. There are no discrete "screens," "workspaces," or "desktops." The world-space extends infinitely in all directions. Boundaries exist only where the camera clips to the physical framebuffer resolution.

```
     ┌─────── Infinite Canvas ───────────────────┐
     │                                            │
     │    [Window A]              [Terminal]       │
     │        │                      │            │
     │        ▼                      ▼            │
     │    ┌────────┐            ┌────────┐        │
     │    │ Button │            │ Prompt │        │
     │    └────────┘            └────────┘        │
     │                            ▲               │
     │            [Panel]         │               │
     │         ┌──────────┐      │               │
     │         │ Label    │──────┘               │
     │         └──────────┘                      │
     │                                            │
     │                         [Debug Overlay]    │
     │              ┌──────────────────────┐     │
     │              │ FPS: 60  Mem: 42%   │     │
     │              └──────────────────────┘     │
     └────────────────────────────────────────────┘
           ▲                          ▲
           │        Camera Viewport   │
           └──────────────────────────┘
              (what the user sees on screen)
```

### 4.2 Camera-as-Viewport

The camera is not an afterthought — it is the **primary navigation mechanism**. The user never "minimizes" a window; they pan away from it. The user never "switches desktops"; they zoom out to see all and zoom in on the target. The camera stores:

- Position (world-space, fixed-point)
- Zoom factor (fixed-point, 0.1× to 8.0×)
- Velocity (for inertial scrolling)
- Screen dimensions (for projection)

```
World Space                           Screen Space
┌──────────────────────┐            ┌──────────────┐
│  ┌──────┐            │            │              │
│  │ Win  │  ┌──────┐  │   Camera  │  ┌──────┐    │
│  └──────┘  │ Term │  │  ───────▶ │  │ Term │    │
│            └──────┘  │            │  └──────┘    │
│ ┌────┐   ┌────────┐  │            │ ┌────────┐   │
│ │Btn │   │ Panel  │  │            │ │ Panel  │   │
│ └────┘   └────────┘  │            │ └────────┘   │
└──────────────────────┘            └──────────────┘
        Infinite                        Viewport
     (world coords)                  (screen coords)
```

### 4.3 Node-as-Universal-Unit

Everything in the GUI is a `node_t`. There is no separate "window object," "control handle," or "widget instance." A window is a node of type `NODE_WINDOW`. A button is a node of type `NODE_BUTTON`. The canvas itself is a node of type `NODE_CANVAS`. This uniformity means:

- Every entity has the same parent/child hierarchy
- Every entity goes through the same transform/draw pipeline
- Every entity participates in the same event dispatch
- There is no special-case code for "windows" vs "controls"

```
node_t ──────────────────────────────────────────────────┐
├── id: uint32_t          (unique, monotonically increasing)
├── type: node_type_t     (NODE_CANVAS .. NODE_DEBUG)
├── name: char[32]        (debug-friendly identifier)
├── local_{x,y}           (position in parent space)
├── width, height         (extent in parent space)
├── world_transform       (cached 3×3 affine matrix)
├── screen_bounds         (cached AABB after camera projection)
├── parent / children[]   (hierarchy)
├── prev/next_sibling     (doubly-linked list)
├── visible / interactive / dirty / opacity / z_order
├── layout_{type,align,margin,padding,flex_weight}
├── vtable                (draw, on_event, layout, destroy)
└── userdata              (subtype-specific payload)
```

### 4.4 Lazy Validation

The scene graph uses a **dirty-flag system** to minimize recomputation:

- `NODE_DIRTY_TRANSFORM` — world_transform must be recomputed (set when local position changes)
- `NODE_DIRTY_LAYOUT` — children need repositioning (set when size or layout properties change)
- `NODE_DIRTY_PAINT` — visual content changed (set when text, color, etc. change)

Transform propagation is depth-first and only visits dirty nodes. The compositor calls `node_update_transforms()` once per frame with a clean identity transform at the root.

### 4.5 Single-Pass Rendering

The compositor performs a single depth-first traversal of the visible node tree each frame:

```
compositor_frame()
  ├─ input_manager_poll()
  ├─ event_bus_dispatch()
  ├─ camera_update()           ← inertia
  ├─ animation_engine_tick()
  ├─ node_update_transforms()  ← transform pass
  ├─ draw_background()         ← dotted grid
  ├─ node_draw_recursive()     ← single pass
  ├─ cursor_draw()             ← always on top
  └─ renderer_present()        ← backbuffer → VRAM
```

No deferred rendering. No multi-pass compositing. No offscreen surfaces (except the single backbuffer). This is deliberate: with a software framebuffer on x86-64, fill-rate is the bottleneck. Single-pass minimizes memory bandwidth.

## 5. Architectural Rules

### Rule 1: No Window System in the Traditional Sense

There is no `window_create()` syscall that returns an HWND-like handle. There is only `node_create(NODE_WINDOW, ...)` which returns a `node_t*`. The "window manager" (`window_manager.c`) is a thin subscriber on the event bus that handles z-order — it does not own, track, or manage windows. Windows manage themselves through their vtable.

### Rule 2: The Camera is the Only Window Manager

The camera determines what is visible. There is no "show desktop" button, no "minimize all," no "cascade windows." To see everything, the user zooms out. To focus on one thing, the user pans/zooms to it. The WindowManager's only job is `bring_to_front()` which reorders nodes in the parent's children array.

### Rule 3: All Coordinates are World-Space

Application code never deals with screen coordinates. A window is created at `(x=400, y=300)` in world-space. It remains at those world coordinates regardless of camera position or zoom. The camera projects world→screen each frame.

```
// GUI application code (world-space):
node_t *win = window_node_create("my_win", 400, 300, 320, 240, "Hello");
node_add_child(canvas_root, win);

// The system handles projection:
// screen_x = (world_x - camera_world_x) * zoom / ZOOM_SCALE
```

### Rule 4: The Screen is Just a Viewport

The physical display (framebuffer) is a window into the infinite canvas. It has no special status in the spatial model. Applications do not know the screen resolution. The camera knows the screen resolution; that is its only connection to physical display.

```
┌─────────── Infinite Canvas World ─────────────────────────────┐
│  ┌────────────────┐                                            │
│  │                │  ┌───────────┐                             │
│  │  Screen        │  │ Viewport  │  ┌─────────────────┐       │
│  │  (physical     │  │ (camera)  │  │                 │       │
│  │   framebuffer) │  │           │  │  You are here   │       │
│  │                │  │  ┌─────┐  │  │                 │       │
│  │  ┌──────┐      │  │  │ FOV │  │  └─────────────────┘       │
│  │  │ View │      │  │  └─────┘  │                             │
│  │  └──────┘      │  └───────────┘                             │
│  └────────────────┘                                            │
└────────────────────────────────────────────────────────────────┘
         ▲                           ▲
         │    The canvas is the      │
         │    single source of       │
         │    truth for all GUI      │
         │    object positions       │
         └───────────────────────────┘
```

### Rule 5: Nodes are Not Windows

A `NODE_WINDOW` is a convenience widget that draws a title bar and background. It does not have special OS-level status. A `NODE_TERMINAL` is just another node type. There is no "window class registration," no "window message pump," no "window procedure" in the Win32 sense. Nodes are lightweight (160 bytes each) and polymorphic through their vtable.

## 6. Contrast: Traditional Desktop vs Infinite Canvas

```
Traditional Desktop (Windows/macOS/X11):
┌─────────────────────────────────────────────┐
│  Title Bar: "File Explorer"  ─ □ ×          │
├─────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────┐│
│  │  This window exists at screen coord     ││
│  │  (x=100, y=50). It cannot exist         ││
│  │  outside the monitor's bounds.          ││
│  │  The window manager enforces this.      ││
│  └─────────────────────────────────────────┘│
└─────────────────────────────────────────────┘
       ▲                                       ▲
       │  Window is at screen position         │
       │  Window manager places it             │
       │  Window manager owns decorations      │
       └───────────────────────────────────────┘

LiwusOS Infinite Canvas:
┌─────────────────────────────────────────────┐
│ Canvas World (extends infinitely)           │
│                                              │
│  ┌──────────────────┐                       │
│  │                  │  ┌────────────────┐   │
│  │  Widget A        │  │                │   │
│  │  at world (200,  │  │  Widget B      │   │
│  │  150)             │  │  at world      │   │
│  │                  │  │  (800, 600)    │   │
│  └──────────────────┘  │                │   │
│                         └────────────────┘   │
│                                              │
│  ┌────────── Camera Viewport ──────────────┐ │
│  │  Shows world region (0,0) to (1024,768) │ │
│  │  Can pan/zoom to any world coordinate   │ │
│  └───────────────────────────────────────────│
└─────────────────────────────────────────────┘
       ▲                                       ▲
       │  Windows exist at world positions     │
       │  Camera determines what is visible    │
       │  No window manager required           │
       └───────────────────────────────────────┘
```

## 7. Design Rationale

### Why Not Floating Windows?

Floating windows (the classical desktop metaphor) force users into a **spatial conflict**: every new window reduces available screen space for existing content. Users must constantly arrange, resize, and restack. The Infinite Canvas eliminates this conflict by providing unbounded space. The user navigates; they do not manage.

### Why Not Tiling?

Tiling window managers solve the space conflict but introduce **rigidity**: applications must fit the tile grid, and spatial relationships between content are dictated by the layout algorithm. The Infinite Canvas preserves user agency — content can be placed arbitrarily, grouped semantically, and arranged by task.

### Why Not Virtual Desktops?

Virtual desktops are a **band-aid** for finite screen space. They create discrete, isolated spaces with no spatial relationship between them. The Infinite Canvas is continuous — zoom out to see all virtual desktops at once, zoom in to work on one. Spatial relationships between "desktops" are preserved.

### Why Fixed-Point Math?

The kernel is compiled with `-mno-sse`. There is no hardware FPU used in kernel-space. All camera and transform math uses fixed-point integers (`CAMERA_ZOOM_SCALE = 1024`, `CAMERA_POS_SCALE = 256`, `TRANSFORM_SCALE = 65536`). This guarantees deterministic behavior and avoids SSE register save/restore overhead in interrupt handlers.

---

*Document v1.0 — reflects LiwusOS GUI as implemented in `src/kernel/gui/`.*
