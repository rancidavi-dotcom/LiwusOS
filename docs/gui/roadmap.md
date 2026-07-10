# Technical Roadmap — LiwusOS GUI

## Objective

Define the phased evolution of the LiwusOS GUI subsystem from its current Phase 1 infrastructure through Phase 6's full application ecosystem. Each phase lists objectives, dependencies, completion criteria, and risks.

---

## Current Phase: Phase 1 — Infrastructure (Complete)

### Objectives

1. Establish the scene graph core with node lifecycle and hierarchy management
2. Implement camera with fixed-point zoom/pan/inertia
3. Build a software framebuffer renderer with alpha blending, glyph drawing, and clipping
4. Create the compositor frame loop with full redraw
5. Implement typed event bus with priority dispatch
6. Build input manager polling mouse+keyboard and posting delta events
7. Develop basic widgets (button, label, panel, window with title bar and close button)
8. Implement layout engine (VBOX, HBOX with flex weight and alignment)
9. Build animation engine (linear tweening for position, size, color)
10. Create theme engine with static dark palette
11. Implement focus management (basic)
12. Implement window manager with z-order bring-to-front
13. Define and implement syscall interface (120–124)
14. Create user-space SDK (liwus_gui.h / liwus_gui.c)
15. Build tool system (PanTool, SelectTool, MoveTool)
16. Implement fixed-point math infrastructure (rect.h, transform.h)

### Dependencies

| Dependency | Status |
|---|---|
| VGA framebuffer driver (`vga.c`) | ✅ Complete |
| Kernel heap (`kheap.h`) | ✅ Complete |
| Keyboard driver (`keyboard.h`) | ✅ Complete |
| Mouse driver (`mouse.h`) | ✅ Complete |
| PSF font binary | ✅ Linked in kernel |
| Task scheduler (`task.h`) | ✅ Complete |
| Serial debug output (`serial.h`) | ✅ Complete |
| `kmalloc`/`kfree` (kheap.h) | ✅ Complete |

### Files Created

| File | Purpose |
|---|---|
| `gui_main.c/h` | Bootstrap and compositor task |
| `math/rect.h` | AABB operations |
| `math/transform.h` | Affine transform |
| `scene/node.h/c` | Scene graph core |
| `scene/camera.h/c` | Camera with zoom/pan |
| `render/renderer.h/c` | Abstract renderer |
| `render/fb_renderer.h/c` | Framebuffer backend |
| `render/compositor.h/c` | Compositor frame loop |
| `core/event_bus.h/c` | Event bus |
| `core/theme_engine.h/c` | Color palette |
| `core/animation_engine.h/c` | Linear tweening |
| `input/input_manager.h/c` | Input polling |
| `input/tools/*` | Tool system |
| `layout/layout_engine.h/c` | VBOX/HBOX layout |
| `widgets/button.h/c` | Button widget |
| `widgets/label.h/c` | Label widget |
| `widgets/panel.h/c` | Panel widget |
| `widgets/window_node.h/c` | Window widget |
| `window/focus_manager.h/c` | Focus management |
| `window/window_manager.h/c` | Z-order management |
| `sdk/include/liwus_gui.h` | User-space API header |
| `sdk/lib/liwus_gui.c` | User-space syscall wrappers |

### Completion Criteria

- [x] Scene graph can create/destroy nodes with hierarchy
- [x] Camera pans, zooms, centers, and fits on command
- [x] Renderer fills rects, draws borders, blits, and renders glyphs
- [x] Compositor runs a frame loop: input → events → transforms → draw → present → yield
- [x] Event bus posts and dispatches mouse/keyboard events with priority ordering
- [x] Buttons respond to hover and click with color animation
- [x] Labels render colored text
- [x] Windows have title bars and close buttons (close = kill process)
- [x] Layout engine stacks children vertically/horizontally with flex weights
- [x] Theme provides consistent dark palette
- [x] Focus routes keyboard events
- [x] Windows bring to front on click
- [x] Syscalls 120–124 work from user-space
- [x] `demo_gui.elf` runs as user-space app showing a window with widgets

### Risks

| Risk | Mitigation |
|---|---|
| Full redraw every frame is slow | Phase 2 adds dirty rect culling |
| No floating-point math is restrictive | Fixed-point (CAMERA_ZOOM_SCALE, CAMERA_POS_SCALE, TRANSFORM_SCALE) handles all GUI math |
| Node array (64 children max) is inflexible | Sufficient for Phase 1; Phase 4 adds linked list or dynamic allocation |
| Serial debug output is slow | Gated by `#ifdef GUI_DEBUG` eventually |

---

## Phase 2 — Dirty Rect & Culling (Next)

### Objectives

1. Implement dirty rectangle tracking (partial redraw instead of full)
2. Add viewport culling (skip off-screen nodes)
3. Add occlusion culling (skip hidden nodes behind opaque surfaces)
4. Spatial indexing with quadtree on large canvases

### Dependencies

| Dependency | Status |
|---|---|
| Phase 1 compositor | ✅ Complete |
| `compositor_t.dirty_rects[]` (pre-allocated array) | ✅ Pre-allocated but unused |

### Completion Criteria

- [ ] `compositor_invalidate()` accumulates and merges dirty rects instead of setting `full_redraw=true`
- [ ] Only dirty regions are repainted
- [ ] Nodes completely outside the camera viewport are skipped during draw traversal
- [ ] Nodes completely hidden behind opaque parents are skipped
- [ ] Benchmark: 10× performance improvement with small dirty regions

### Risks

| Risk | Mitigation |
|---|---|
| Dirty rect merging is O(n²) | Limit to 64 rects per frame; merge adjacent rects early |
| Culling check overhead could exceed draw savings | Off-screen check is O(1) per node; occlusion test is O(depth) |

---

## Phase 3 — Widget Toolkit Expansion

### Objectives

1. Terminal widget (ANSI parser rendering into scene graph)
2. Image widget (PNG decode + display)
3. Scrollbar, slider, checkbox, radio button, dropdown
4. Text input (multi-line editor)
5. Tree view, list view

### Dependencies

| Dependency | Status |
|---|---|
| libpng integration (sdk/lib/libpng.a) | ✅ Available |
| PSF font (for text rendering) | ✅ Available |
| Existing widget patterns (button, label, panel) | ✅ Available |

### Completion Criteria

- [ ] Terminal widget renders ANSI-colored text in a scrollable pane
- [ ] Image widget displays PNG/JPEG from VFS or memory
- [ ] Slider with thumb drag and range
- [ ] Checkbox with checked/unchecked state
- [ ] Text input with cursor and caret rendering
- [ ] Tree view with expandable items
- [ ] All new widgets use existing vtable pattern

### Risks

| Risk | Mitigation |
|---|---|
| Terminal widget needs scrolling buffer management | Circular buffer of lines, configurable max lines |
| PNG decode in kernel is too heavy | libpng already ported; decode in user-space via IPC in Phase 6 |
| Text cursor rendering is complex | Use existing glyph renderer; blink via animation engine |

---

## Phase 4 — Performance & GPU

### Objectives

1. Object pools for node allocations (replace individual kmalloc)
2. Texture atlas for glyphs and sprites
3. GPU backend (Vulkan prototype — initial)
4. Glyph cache (pre-rasterized text)
5. Render batching (group draw calls by color/blit source)

### Dependencies

| Dependency | Status |
|---|---|
| Phase 2 (dirty rects) | Not started |
| GPU driver (`include/gpu.h`, `gpu.c`) | Planned |
| Vulkan (Vulkan SDK or custom kernel-side) | Not started |

### Completion Criteria

- [ ] Node allocation from pre-allocated pool reduces allocation overhead by 90%
- [ ] Texture atlas for 256 glyphs in a single blit operation
- [ ] Vulkan backend renders a test scene at 60 FPS
- [ ] Glyph cache avoids re-parsing PSF font every frame
- [ ] Draw calls halved through batching

### Risks

| Risk | Mitigation |
|---|---|
| Vulkan driver requires significant kernel work | Keep software fallback as primary; Vulkan as optional |
| Texture atlas reduces flexibility for per-glyph colors | Use color parameter in blit; atlas stores only luminance |
| Object pool fragmentation | Fixed-size pools per allocation type (node_t, button_data_t) |

---

## Phase 5 — Compositor Evolution

### Objectives

1. Multiple viewports (split-screen display)
2. Compositor effects (blur, drop shadows, transitions)
3. Hardware cursor (use VGA cursor if available)
4. Multi-monitor support (extend compositor for multiple framebuffers)
5. Full keyboard navigation (Tab traversal working)

### Dependencies

| Dependency | Status |
|---|---|
| GPU backend (Phase 4) | ❌ Not started |
| Dirty rect (Phase 2) | ❌ Not started |

### Completion Criteria

- [ ] Two viewports showing different portions of the canvas
- [ ] Window drop shadows rendered as Gaussian blur
- [ ] Hardware cursor sprite replaces software sprite
- [ ] Second monitor initialized by VBE, renderer outputs to both
- [ ] `focus_manager_focus_next()` implemented with full depth-first traversal

### Risks

| Risk | Mitigation |
|---|---|
| Multi-monitor VBE is hardware-dependent | Abstract via renderer ops table; each monitor gets its own gui_renderer_t |
| Gaussian blur is expensive | Use separable convolution; blur only shadow regions |

---

## Phase 6 — Application Ecosystem

### Objectives

1. Plugin system (Lua scripting via `third_party/lua`)
2. Compositor IPC to user-space (event delivery, shared memory)
3. Window decorations as themeable widgets
4. App lifecycle management (start, stop, focus, terminate)
5. Network transparency (remote display protocol)

### Dependencies

| Dependency | Status |
|---|---|
| Phase 5 compositor | ❌ Not started |
| Lua runtime (`third_party/lua/`) | ❌ Not compiled |
| VMM shared memory support | ❌ Not started |
| Network stack (`tcp.h`, `udp.h`) | Planned separately |

### Completion Criteria

- [ ] Lua script can create and arrange GUI nodes
- [ ] User-space apps receive mouse/keyboard events via IPC channel
- [ ] App window decorations are themeable from user-space
- [ ] Apps can be launched and terminated from a launcher
- [ ] Basic remote display protocol works over loopback

### Risks

| Risk | Mitigation |
|---|---|
| Lua in kernel space adds memory pressure | Lua state is per-plugin; max 16 simultaneous scripts |
| IPC event delivery adds syscall overhead per event | Use shared memory ring buffer (zero-syscall reads) |
| Remote display protocol latency | Differential updates (only changed nodes/rects sent over network) |

---

## Phase Dependency Graph

```
Phase 1 ─────────────────────────────────────────────
    Infrastructure (Scene Graph, Renderer, Compositor,
    Event Bus, Input, Widgets, Layout, Animation,
    Theme, Syscalls, SDK, Tools)
    
        │
        ▼
Phase 2 ──────────┐    Phase 3 ──────────┐
    Dirty Rects     │    Widget Toolkit     │
    Viewport Culling│    (Terminal, Image,  │
    Occlusion       │    Slider, Checkbox,  │
                    │    TreeView, ListView) │
        │          │         │              │
        ▼          │         ▼              │
Phase 4 ◄──────────┴─────────┘              │
    Performance (Object pools,              │
    Texture atlas, Glyph cache,             │
    Vulkan prototype, Batching)              │
        │                                  │
        ▼                                  │
Phase 5 ──────────────────────────────────┘
    Compositor Evolution (Viewports,
    Effects, Hardware Cursor, Multi-monitor)
        │
        ▼
Phase 6 ─────────────────────────────────────────────
    Application Ecosystem (Plugins, IPC,
    Lua Scripting, Network Transparency)
```

---

## Summary Timeline Estimate

| Phase | Effort | Complexity | Risk |
|---|---|---|---|
| Phase 1 (Complete) | ~800 LOC | High | Medium |
| Phase 2 | ~200 LOC | Medium | Low |
| Phase 3 | ~1500 LOC | High | Medium |
| Phase 4 | ~3000 LOC | Very High | High |
| Phase 5 | ~1000 LOC | High | Medium |
| Phase 6 | ~4000 LOC | Very High | High |

---

## Key Constants for Each Phase

| Constant | Phase 1 | Target Phase 4 |
|---|---|---|
| Frame rate | ~30–50 FPS (full redraw) | 60 FPS (dirty rect + batching) |
| Max nodes | 64 per parent, unlimited total | Object pool with 1024 pre-allocated |
| Draw calls per frame | ~80 | ~20 (batched) |
| Memory (backbuffer) | `width*height*4` | same (texture atlas adds ~256KB) |
| Rendering backend | Software framebuffer only | Software + Vulkan optional |
| User-space apps | Simple syscall wrappers | Full IPC + Lua plugins |