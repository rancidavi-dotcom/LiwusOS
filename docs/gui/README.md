# LiwusOS GUI Documentation

## Overview

The LiwusOS GUI is a kernel-side composited GUI subsystem built around a **scene graph** architecture. All user-interface elements — windows, buttons, labels, panels, overlays, and the infinite canvas background — are represented as `node_t` objects in a hierarchical tree. A kernel task (`gui_compositor_task()`) runs a tight frame loop that polls input, dispatches events, updates transforms and animations, and renders the entire scene graph to a software framebuffer back-buffer before flipping it to the VGA hardware framebuffer. The compositor currently performs a full redraw every frame, with dirty-rect optimization planned for Phase 2.

User-space applications interact with the GUI entirely through **syscalls 120–124** using the **Scene Graph SDK** (`sdk/include/liwus_gui.h`). Applications create a canvas (window), add widget nodes (text, button, panel), position them, and the kernel compositor handles all rendering — no event loop, no framebuffer management, no draw code is required in user-space. This is a departure from the older LGX framebuffer API (syscalls 10–13 via `int $0x80`) which required applications to manage their own pixel buffers. The kernel GUI comprises approximately 2500 lines of C across 25 source files, organized into 15 modules covering math primitives, scene graph management, camera control, rendering, compositing, event bus, input, layout, animations, themes, widgets, focus, and window management.

All GUI math uses fixed-point arithmetic (`CAMERA_ZOOM_SCALE=1024`, `CAMERA_POS_SCALE=256`, `TRANSFORM_SCALE=65536`) to avoid floating-point operations in the kernel (compiled with `-mno-sse`). The theme engine provides a modern dark palette inspired by slate/indigo with 80–93% opacity for glassmorphism effects. Widgets use a vtable dispatch pattern (`node_vtable_t`) that allows each node type to define its own `draw()`, `on_event()`, `layout()`, and `destroy()` methods.

---

## Document Index

| Document | Description |
|---|---|
| [api.md](api.md) | Complete API reference for all kernel modules and syscalls. Covers scene graph (14 functions), camera (9 functions + 6 inline converters), renderer (8 ops + 8 wrappers), compositor (6 functions), event bus (6 functions + 4 helpers), input manager (8 functions), theme engine (3 functions), animation engine (4 functions), focus manager (5 functions), window manager (3 functions), asset manager (3 functions), layout engine (1 function), and all widget APIs (11 functions). Includes full syscall documentation for calls 120–124 with parameter, return, and error details. |
| [sdk.md](sdk.md) | User-Space SDK documentation. Covers the public API in `liwus_gui.h` (Canvas and Node types, 8 wrapper functions), the syscall implementation via `syscall` instruction in `liwus_gui.c`, SDK tools (`liw-builder`, `img-gen`), cross-compilation with `x86_64-elf-gcc`, the app model (no event loop needed), and a complete reference example from `demo_gui.c`. |
| [ipc.md](ipc.md) | IPC architecture (future). Describes the current kernel-only event model, the planned shared memory + message channel for user-space event delivery, message format (`gui_ipc_header_t`, message types, payload structures), the shared memory pixel data model, and the event delivery flow from hardware IRQ to user-space callback. |
| [plugins.md](plugins.md) | Plugin system (future). Documents the plugin lifecycle (load → init → register → unload), the `plugin_interface_t` API, node type extension via `register_node_type()`, Lua scripting integration using the existing `third_party/lua/`, plugin isolation/sandboxing with memory and frame budgets, and a complete native plugin skeleton example. |
| [accessibility.md](accessibility.md) | Accessibility (future). Covers screen reader support with serial output, keyboard navigation (Tab traversal, keyboard shortcuts), high-contrast theme variants (`THEME_VARIANT_HIGH_CONTRAST`), focus indicator system with gold focus rings, the semantic accessibility tree derived from the scene graph, and the proposed `access_role`, `access_label`, and `access_desc` node properties. |
| [debugging.md](debugging.md) | Debugging infrastructure. Documents serial port debugging via `serial_print()`, the scene inspector (tree traversal showing node properties), widget inspector (bounds, type, state), FPS counter (`compositor_t.frame_number`), event monitor (live bus traffic via `GUI_EVENT_NONE` subscription), render debugger (color-coded draw call boundaries), the `NODE_DEBUG` overlay type, and keyboard shortcuts (I=Inspect, E=Events, D=Debug, R=Render). |
| [profiling.md](profiling.md) | Profiling infrastructure. Documents frame timing with per-phase measurement (`PHASE_POLL_INPUT` through `PHASE_PRESENT`), draw call counting (fill_rect, draw_rect, blit, blit_scaled, draw_glyph counters), memory tracking (backbuffer size, node count, per-module heap), performance counters (FPS, frame time, culled nodes, dirty area), and the planned on-screen profiler overlay with bar charts. |
| [testing.md](testing.md) | Testing strategy. Covers unit tests for rect operations (9 tests), transform composition (7 tests), alpha blending (4 tests), widget button click detection, scene graph hierarchy (6 tests), layout engine (3 tests), camera coordinate roundtrip tests (5 tests), and visual regression testing with pixel-by-pixel comparison against reference .ppm files embedded as C arrays. Includes kernel-side test task harness and user-space test runner approach. |
| [coding_guidelines.md](coding_guidelines.md) | Coding guidelines. Covers naming conventions (snake_case, _t suffixes, SCREAMING_CASE enums), file organization (one .h/.c pair per module), include path style (relative within gui/, absolute from kernel), memory management (kmalloc/kfree, no VLA), error handling (return NULL/false, no exceptions), fixed-point arithmetic rules, vtable pattern, event handling rules (never read hardware directly), dependency graph with 9 rules, code style (4-space indent, no tabs, braces on same line for control flow), and kernel constraints (no SSE, no floating-point in hot paths). |
| [roadmap.md](roadmap.md) | Technical roadmap divided into 6 phases. Phase 1 (complete) delivered the full scene graph, camera, renderer, compositor, event bus, input, all widgets, layout, animation, theme, focus, window management, tools, syscalls, and SDK. Phase 2 adds dirty rects and culling. Phase 3 expands the widget toolkit. Phase 4 adds performance optimizations and GPU backend. Phase 5 evolves the compositor with viewports and effects. Phase 6 creates the application ecosystem with plugins, IPC, and network transparency. |

---

## Quick Start for Developers

### Building the Kernel with GUI

```bash
# From the LiwusOS root directory
make clean && make
```

### Running with QEMU

```bash
./run.sh
```

Or manually:
```bash
qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -serial file:qemu_serial.log \
    -m 512M
```

### Building a User-Space App

```bash
x86_64-elf-gcc \
    -ffreestanding -nostdlib \
    -I sdk/include \
    -L sdk/lib \
    apps/demo_gui/demo_gui.c \
    -o apps/demo_gui/demo_gui.elf \
    -l c -l liwus_gui -l g -l m \
    -T sdk/liwus.ld -lgcc
```

### Watching Serial Debug Output

```bash
tail -f qemu_serial.log
```

---

## Key Design Principles

1. **No floating-point in kernel hot paths.** All camera math (zoom, pan, coordinate conversion), transform composition, and renderer operations use fixed-point integers. Floating-point is permitted only in animation tick functions and user-space→kernel float conversion.

2. **All input goes through the EventBus.** Hardware drivers are read only by `input_manager_poll()`. No widget or tool may call `get_mouse_x()`, `keyboard_is_pressed()`, or any hardware read function directly. This ensures testability, debuggability, and clean separation of concerns.

3. **Backend-agnostic renderer.** All drawing goes through `gui_renderer_t` and its `renderer_ops_t` vtable. The software framebuffer backend (`fb_renderer.c`) is the current implementation; a future Vulkan backend would swap in without changing any widget or compositor code.

4. **User-space apps are stateless for rendering.** Apps create and arrange nodes via syscalls; the kernel compositor owns the framebuffer and renders every frame. No `lgx_refresh()`, no per-app backbuffer, no draw loops in user-space.

5. **Scene graph is the single source of truth.** Node hierarchy, position, visibility, opacity, layout, and event handling all derive from the `node_t` tree. There is no separate widget registry or window list — everything is in the scene graph.

6. **Fixed allocation limits, all from kernel heap.** Arrays (children[64], dirty_rects[64], animations[64], subscribers[64], ring[256]) are statically sized. All dynamic memory comes from `kmalloc`/`kfree`. Object pools are reserved for Phase 4.

7. **Monolithic kernel GUI compositor.** The compositor runs as a kernel task with full access to the scene graph, framebuffer, and event bus. Phase 6 splits this into a service model with IPC to user-space.

---

## Module Dependency Graph

```
math/           (rect.h, transform.h)   — no deps
  │
  ▼
scene/          (node.h, camera.h)      — depends on math/
  │
  ▼
core/           (event_bus, theme,      — depends on scene/
  │               animation)
  ▼
layout/         (layout_engine)         — depends on scene/
  │
  ▼
input/          (input_manager, tools/) — depends on core/ + scene/
  │
  ▼
render/         (renderer, compositor,  — depends on scene/ + core/ + input/
  │               fb_renderer)
  ▼
widgets/        (button, label, panel,  — depends on scene/ + render/ + core/
  │               window_node)
  ▼
window/         (focus_manager,         — depends on scene/ + core/
                  window_manager)
  │
  ▼
gui_main.c      (bootstrap)             — depends on ALL modules
```

---

## File Listing

### Kernel Source Files (`src/kernel/gui/`)

| File | Lines | Description |
|---|---|---|
| `gui_main.c` | 158 | Bootstrap: init modules in dependency order, create demo scene, launch compositor task |
| `gui_main.h` | 16 | Public entry points: `gui_init()`, `gui_compositor_task()` |
| `math/rect.h` | 107 | AABB type, 12 inline operations (make, zero, is_empty, contains, intersects, intersection, union, offset, inflate, clip) |
| `math/transform.h` | 127 | 3×3 affine transform (16.16), 9 inline operations (identity, translation, scale, concat, apply, apply_rect, invert) |
| `scene/node.h` | 216 | node_t, scene_graph_t, node_type_t, node_vtable_t, dirty flags, 12 functions |
| `scene/node.c` | 255 | Scene graph implementation: create, destroy, add_child, remove_child, find_by_id, hit_test, set_position, set_size, mark_dirty, update_transforms, draw_recursive |
| `scene/camera.h` | 128 | camera_t type, 6 inline coordinate converters (world↔screen, rect, viewport, scale) |
| `scene/camera.c` | 119 | Camera lifecycle, pan, center_on, zoom_at, reset, fit, update (inertia) |
| `render/renderer.h` | 134 | Abstract renderer interface: renderer_ops_t vtable, gui_renderer_t struct, 8 inline dispatchers |
| `render/renderer.c` | 26 | Renderer create/destroy |
| `render/fb_renderer.h` | 13 | Framebuffer backend: `fb_renderer_create()`, `fb_renderer_backbuf()` |
| `render/fb_renderer.c` | 242 | Software framebuffer: alpha blend, clip, fill_rect, draw_rect, blit, blit_scaled, draw_glyph, set_clip, set_opacity, present (memcpy to VRAM) |
| `render/compositor.h` | 97 | Compositor type with dirty rects, cursor state, frame counter |
| `render/compositor.c` | 241 | compositor_frame() loop: poll→dispatch→camera→animations→transforms→background→draw nodes→cursor→present→switch_task |
| `core/event_bus.h` | 228 | Event types (26 IDs), priorities (4 levels), payload structs, event_t, 5 function signatures + 4 inline post helpers |
| `core/event_bus.c` | 182 | Ring buffer (256), subscriber table (64), insertion-sort dispatch, stop_propagation |
| `core/theme_engine.h` | 36 | 13 color IDs (THEME_COLOR_BACKGROUND through THEME_COLOR_CLOSE_BTN) |
| `core/theme_engine.c` | 33 | Dark slate palette initialization, get/set color |
| `core/animation_engine.h` | 44 | 6 anim_prop_t values, animation_t, 64 max animations |
| `core/animation_engine.c` | 135 | Engine: tick, start, cancel_all with linear interpolation and color support |
| `input/input_manager.h` | 70 | Modifier masks, input_manager_t (opaque), 6 function signatures |
| `input/input_manager.c` | 132 | Poll: compare hardware state to previous frame, post delta events (MOVE, DOWN, UP, KEY) |
| `input/tools/tool.h` | 71 | Abstract tool interface with vtable (active/deactivate/event/destroy) |
| `input/tools/tool_manager.h` | 23 | Tool manager create/destroy/add_tool |
| `input/tools/tool_manager.c` | — | Implements event routing to tools |
| `input/tools/pan_tool.h` | 14 | Pan tool: RMB or Space+LMB panning, H=home, F=fit |
| `input/tools/pan_tool.c` | — | Pan tool implementation |
| `input/tools/select_tool.h` | 19 | Select tool: LMB selects, get_selection() |
| `input/tools/select_tool.c` | — | Selection tool implementation |
| `input/tools/move_tool.h` | 16 | Move tool: drag selected node |
| `input/tools/move_tool.c` | — | Move tool implementation |
| `widgets/window_node.h` | 14 | window_node_create, set_title, set_pid |
| `widgets/window_node.c` | 173 | Draws title bar with close button (red dot, sends SIGKILL), background, border, text |
| `widgets/button.h` | 18 | button_create, set_text, set_on_click |
| `widgets/button.c` | 160 | State machine: normal→hovered→pressed→clicked, color animation |
| `widgets/label.h` | 16 | label_create, set_text, set_color |
| `widgets/label.c` | 114 | Glyph-by-glyph text rendering, width auto-calculation |
| `widgets/panel.h` | 18 | panel_create, set_bg_color, set_border |
| `widgets/panel.c` | 111 | Filled rect with optional border, camera-projected screen bounds |
| `window/focus_manager.h` | 31 | focus_manager_create/destroy, get_focus, set_focus, focus_next |
| `window/focus_manager.c` | 99 | Subscribes to all events, routes keyboard to focused node, handles Tab (stub) |
| `window/window_manager.h` | 20 | window_manager_create/destroy, bring_to_front |
| `window/window_manager.c` | 69 | Subscribes to WIN_FOCUS, moves node to end of children array |
| `layout/layout_engine.h` | 16 | layout_engine_compute |
| `layout/layout_engine.c` | 154 | VBOX and HBOX flex layout with alignment, padding, margin |
| `assets/asset_manager.h` | 22 | asset_manager_init/destroy, get_font |
| `assets/asset_manager.c` | 47 | PSF font parser, 256-glyph cache, font bitmap from kernel binary |

### Syscall Dispatch (`src/kernel/syscall.c` — GUI portion)

| Lines | Function | Syscall |
|---|---|---|
| 731–754 | `sys_gui_canvas_create` | 120 |
| 756–769 | `sys_gui_node_create` | 121 |
| 771–783 | `sys_gui_canvas_add` | 122 |
| 785–794 | `sys_gui_node_move` | 123 |
| 796–802 | `sys_gui_camera_zoom` | 124 |
| 1172–1182 | Dispatch cases | 120–124 |

### User-Space SDK Files (`sdk/`)

| File | Description |
|---|---|
| `include/liwus_gui.h` | Public API header: Canvas, Node types, NODE_ constants, 6 function signatures |
| `lib/liwus_gui.c` | Syscall wrappers via `syscall` instruction, float→int scaling for camera_zoom |
| `lib/libliwus_gui.a` | Pre-built static library (archive) |
| `tools/liw-builder.c` | ELF build utility |
| `tools/img-gen.c` | Image to C array converter |
| `tools/gen_wallpaper.py` | Wallpaper generator |
| `tools/gen_ui_assets.py` | UI sprite generator |
| `tools/convert_wallpaper.py` | PNG to raw converter |
| `tools/img2c.py` | Image to C header converter |

### Application Files (`apps/`)

| File | Description |
|---|---|
| `demo_gui/demo_gui.c` | Reference app: creates canvas, title, button, panel — no event loop |
| `demo_gui/demo_gui.elf` | Pre-compiled binary |
| `view/view.c` | Image viewer (uses legacy LGX API, not Scene Graph SDK) |
| `doomgeneric/doomgeneric_liwus.c` | Doom port (uses legacy LGX API: `int $0x80` framebuffer) |

### Additional Files

| File | Description |
|---|---|
| `include/syscall_nums.h` | Syscall number definitions (1–33; 120–124 handled directly in syscall.c) |
| `LGX Liwus Graphics eXtension` | Legacy LGX API documentation |
| `LGX_MANUAL.md` | Legacy LGX manual (older sibling of this file set) |