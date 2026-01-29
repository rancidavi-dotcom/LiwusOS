# Debugging Infrastructure — LiwusOS GUI

## Objective

Document the debugging facilities available in the LiwusOS GUI subsystem, including serial printing, scene/widget inspection, frame counting, event monitoring, render debugging, and keyboard shortcuts for debug overlays.

---

## Problems Solved

- The GUI runs as a kernel task on a QEMU VM — no debugger, no breakpoints, no standard output
- Scene graph operations are invisible without serial debug output
- Widget bounds and states are difficult to verify visually
- Performance issues (frame drops, ghost cursors) require visibility into compositor internals
- No runtime inspection of node properties, camera state, or event traffic

---

## Architecture

```
┌────────────────────────────────────────────────────────┐
│                  Compositor Frame Loop                  │
│                                                         │
│  compositor_frame()                                     │
│    │                                                    │
│    ├── serial_print("[FRAME] #%llu\n", frame_number)    │
│    ├── input_manager_poll()                             │
│    ├── event_bus_dispatch()                             │
│    ├── camera_update()                                  │
│    ├── animation_engine_tick()                          │
│    ├── node_update_transforms()                         │
│    ├── draw_background()                                │
│    ├── node_draw_recursive()                            │
│    │     └─ serial_print("Visiting node: %s\n", name)   │
│    ├── cursor_draw()                                    │
│    ├── renderer_present()                               │
│    └── switch_task()                                    │
└────────────────────────────────────────────────────────┘
```

---

## Serial Port Debugging

### Function

```c
extern void serial_print(const char *s);     // defined in kernel/drivers/serial.c
extern char* itoa(int num, char* str, int base);  // for integer formatting
```

**Usage:** Called directly from all GUI modules. QEMU captures serial output to `serial.log` or `qemu_serial.log`.

### Current Serial Print Locations

| File | Line | Printf |
|---|---|---|
| `scene/node.c:239` | `node_draw_recursive()` | `"Visiting node: %s\n"` |
| `widgets/window_node.c:37-43` | `window_draw()` | `"window_draw: screen_x=%d\n"` |

### Enabling Serial Debug

Run with QEMU serial output:
```bash
qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -serial file:qemu_serial.log
```

Monitor serial in real-time:
```bash
tail -f qemu_serial.log
```

---

## Scene Inspector

### Proposed Debug Mode

Toggle with `I` key (when `InspectTool` is implemented):

```
 ┌─────────────────────────────────────────────────────────┐
 │  SCENE INSPECTOR               FPS: 58   Nodes: 12     │
 ├─────────────────────────────────────────────────────────┤
 │  canvas (NODE_CANVAS)     id=1  bounds=(0,0,1024,768)  │
 │  ├─ demo_win (NODE_WINDOW)                            │
 │  │   id=3  pos=(100,100)  size=(300,200)              │
 │  │   visible=true  interactive=true  opacity=1.0      │
 │  │   ├─ lbl (NODE_LABEL)                              │
 │  │   │   id=4  text="Hello, Infinite Canvas!"         │
 │  │   │   pos=(0,0)  world=(100,130)  screen=(150,80)  │
 │  │   ├─ btn (NODE_BUTTON)                             │
 │  │   │   id=5  text="Click Me"                        │
 │  │   │   pos=(70,30)  world=(170,160)                 │
 │  │   │   state=hovered=false pressed=false            │
 │  │   └─ pnl (NODE_PANEL)                              │
 │  │       id=6  bg=0x88000000                          │
 │  └─ overlay (NODE_OVERLAY)                            │
 └─────────────────────────────────────────────────────────┘
```

### Tree Traversal Function

```c
void scene_inspector_dump(node_t *root, int depth) {
    if (!root) return;
    serial_print_indent(depth);
    serial_print(root->name);
    serial_print(" (");
    serial_print(node_type_string(root->type));
    serial_print(") ");
    serial_print("id=");
    serial_print_int(root->id);
    serial_print("\n");
    for (uint32_t i = 0; i < root->child_count; i++) {
        scene_inspector_dump(root->children[i], depth + 1);
    }
}
```

---

## Widget Inspector

### Per-Widget Debug Overlay

When inspect mode is active, clicking a widget shows its properties overlaid on screen:

```
 ┌────────────────────┐
 │  Widget Inspector  │
 ├────────────────────┤
 │ Type:    NODE_BTN  │
 │ Name:    btn       │
 │ ID:      5         │
 │ Bounds:  100,130   │
 │          120x36    │
 │ Dirty:   0x00       │
 │ Opacity: 1.0        │
 │ z-order: 0         │
 │ State:  hovered    │
 └────────────────────┘
```

### Implementation in Renderer

```c
// Debug overlay render (Phase 4)
static void widget_inspector_draw(node_t *self, gui_renderer_t *r) {
    // Draw semi-transparent overlay rect above widget
    gui_rect_t overlay = rect_make(
        self->screen_bounds.x,
        self->screen_bounds.y - 80,
        180, 80);
    renderer_fill_rect(r, overlay, 0xCC000000);
    // Draw text properties...
}
```

---

## FPS Counter

### Current State

The compositor tracks `frame_number` in `compositor_t`:

```c
uint64_t frame_number;
```

Incremented at the end of each `compositor_frame()` call.

### FPS Calculation

```c
static uint64_t last_fps_time = 0;
static uint32_t fps_counter = 0;
static uint32_t current_fps = 0;

void fps_tick(void) {
    fps_counter++;
    uint64_t now = timer_get_ticks();  /* from kernel timer */
    if (now - last_fps_time >= 1000) {  /* 1 second elapsed */
        current_fps = fps_counter;
        fps_counter = 0;
        last_fps_time = now;
    }
}
```

Called at the end of `compositor_frame()`. Display on screen as overlay text when debug mode is enabled.

### Expected FPS

Target: **60 FPS** (16.6 ms per frame). Current Phase 1 full-repaint may achieve 30–50 FPS depending on backbuffer size and node count.

---

## Event Monitor

### Proposed Feature

Toggle with `E` key to show a live scroll of event bus traffic:

```
[EVENT] MOUSE_MOVE   x=200,y=150 dx=2,dy=1
[EVENT] MOUSE_DOWN   x=200,y=150 btn=1
[EVENT] KEY_DOWN     sc=0x1D (LCTRL) mods=0x02
[EVENT] NODE_DIRTY   node_id=5
[EVENT] MOUSE_UP     x=210,y=155 btn=1
[EVENT] FRAME_BEGIN  frame=482
[EVENT] FRAME_END    frame=482
```

### Implementation

```c
// Subscribe to ALL events with a debug handler
event_bus_subscribe(bus, GUI_EVENT_NONE, debug_event_handler, NULL);

static void debug_event_handler(const gui_event_t *e, void *userdata) {
    serial_print("[EVENT] ");
    serial_print(event_type_string(e->type));
    serial_print("\n");
}
```

---

## Render Debugger

### Draw Call Boundaries

Highlight each draw call with colored borders:

```c
void debug_draw_call_begin(node_t *node, gui_renderer_t *r) {
    // Draw a 1-pixel colored border around the node's screen_bounds
    uint32_t debug_colors[] = {
        0xFFFF0000, // red
        0xFF00FF00, // green
        0xFF0000FF, // blue
        0xFFFFFF00, // yellow
        0xFFFF00FF, // magenta
        0xFF00FFFF, // cyan
    };
    static int color_index = 0;
    renderer_draw_rect(r, node->screen_bounds,
                       debug_colors[color_index++ % 6], 1);
}
```

### Clip Rect Visualization

When `renderer_set_clip()` is called, draw the clip area as a semi-transparent overlay:

```c
void debug_show_clip(gui_renderer_t *r, gui_rect_t clip) {
    renderer_fill_rect(r, clip, 0x220000FF); /* faint blue overlay */
}
```

---

## Debug Node Type

A special `NODE_DEBUG` node type (type=10) for debug overlays. Debug nodes are always rendered on top and are not interactive:

```c
static void debug_overlay_draw(node_t *self, gui_renderer_t *r) {
    // Draw FPS counter
    // Draw frame number
    // Draw node count
    // Draw camera pos/zoom
    // Draw last event info
}
```

The `NODE_DEBUG` type is already defined in both kernel and user-space:

```c
#define NODE_DEBUG    10
```

---

## Keyboard Shortcuts (Planned)

| Key | Function | Status |
|---|---|---|
| **I** | Toggle Inspect mode (scene inspector overlay) | Planned |
| **E** | Toggle Event Monitor | Planned |
| **D** | Toggle Debug Overlay (FPS, node count, memory) | Planned |
| **R** | Toggle Render Debug boundaries | Planned |
| **F12** | Capture a debug screenshot to serial/heap | Planned |

These would be implemented as a `DebugTool` in the tool system, similar to how `PanTool`, `SelectTool`, and `MoveTool` work.

---

## Debug Overlay Mockup

```
┌─ FPS: 58 ───────────────────────────────────────────┐
│  Frame: 1234          Nodes: 12          Memory: 3.2M│
│  Camera: pos=(120, 45) zoom=1.00x                  │
│  Mouse: x=512, y=384  btn=1  hovered=btn              │
│  ──────────────────────────────────────────────     │
│  [EVENT] MOUSE_MOVE  x=512,y=384                    │
│  [EVENT] KEY_DOWN    sc=0x2A (LSHIFT)              │
│  ────────────────────────────────────────────── │
│  Scene:                                             │
│  canvas                                             │
│  └─ demo_win (300×200 @ 100,100)                   │
│      ├─ lbl "Hello, Infinite Canvas!"    50×16      │
│      ├─ btn "Click Me"                  120×36      │
│      └─ pnl                            260×40       │
└────────────────────────────────────────────────────┘
```

---

## Debugging Workflow

### Standard Debug Session

```bash
# 1. Build and run
./build.sh
qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -serial file:qemu_serial.log \
    -m 512M

# 2. In another terminal, watch serial output
tail -f qemu_serial.log

# 3. See node visitation output
Visiting node: canvas
Visiting node: demo_win
Visiting node: lbl
window_draw: screen_x=200
Visiting node: btn
Visiting node: pnl
```

### Debugging Specific Issues

| Symptom | Debug Action |
|---|---|
| Ghost cursor trails | Check `cursor_saved`, `cursor_restore()` in compositor |
| Wrong node position | Enable transform debug: log `world_transform` for each node |
| No click response | Enable event monitor: verify `GUI_EVENT_MOUSE_DOWN` is posted |
| Black screen | Check `vga_fb_addr` is non-zero, `fb_renderer_backbuf` returns valid |
| FPS too low | Enable FPS counter, check draw call count exceeding budget |

---

## Dependencies

- **Serial driver:** `drivers/serial.c` provides `serial_print()` — already linked in kernel
- **Timer:** `kernel/timer.c` for FPS calculation — already available
- **Event bus:** Debug subscription to `GUI_EVENT_NONE` for event monitor
- **Scene graph:** `node_find_by_id()` / `node_hit_test()` for inspector queries
- **VGA globals:** `vga_fb_width`, `vga_fb_height`, `vga_fb_pitch` for coordinate validation

---

## Limitations & Trade-offs

| Concern | Mitigation |
|---|---|
| Serial output slows compositor frame | Debug prints can be gated by `#ifdef GUI_DEBUG` |
| Debug overlay consumes framebuffer pixels | Debug nodes have `z_order = 999` and can be hidden with a key toggle |
| FPS counter requires timer | Timer IRQs are already available in kernel |
| No screenshot functionality | Back-buffer is accessible via `fb_renderer_backbuf()` — can be dumped to debug heap |

---

## Future Extensions

| Extension | Description |
|---|---|
| `InspectTool` | I-key toggles inspect mode; click to see widget properties | Post-Phase 3 |
| `EventMonitor` | E-key toggles live event log overlay | Post-Phase 3 |
| Render debug borders | Color-coded draw call boundaries | Phase 4 |
| Heap allocation tracking | Track kmalloc/kfree per module | Phase 4 |
| Frame time profiler | Color-coded timeline of compositor_frame phases | Phase 4 |
| Screenshot capture | Save backbuffer to initrd as ppm | Phase 5 |