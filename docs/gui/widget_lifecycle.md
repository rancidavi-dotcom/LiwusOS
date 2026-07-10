# LiwusOS GUI — Widget Lifecycle

## Objective

Document the complete lifecycle of a widget node from creation through destruction, including mutation, rendering, event dispatch, layout recomputation, and animation. Each lifecycle stage shows the exact function calls and data flow.

---

## Problems Solved

- **Predictable state machine**: Every node follows the same creation → dirty → render → destroy sequence, making debugging and reasoning about widget behavior consistent.
- **Deferred computation**: Dirty flags batch changes across frames, avoiding redundant work.
- **Safe teardown**: Destruction is recursive depth-first, children destroyed before parent, with vtable cleanup hooks.

---

## Architecture Overview

```
   CREATION ──────────────► MUTATION ──────────────► RENDER ──────► DESTROY
       │                       │                        │
       ├─ kmalloc userdata     ├─ setter function       ├─ transform update
       ├─ node_create()        ├─ node_mark_dirty()     ├─ layout compute
       ├─ set vtable           └─ next frame picks up   ├─ draw_recursive
       ├─ node_add_child()                               └─ vtable->draw
       └─ layout_engine_compute()
```

---

## Lifecycle State Machine

```
         ┌──────────────────────────────────────────────────────┐
         │                                                      │
         v                                                      │
    ┌─────────┐    node_destroy()   ┌───────────┐              │
    │ CREATED ├────────────────────►│ DESTROYED │              │
    └────┬────┘                     └───────────┘              │
         │                                                      │
    node_add_child()                                            │
         │                                                      │
         v                                                      │
    ┌─────────┐   setter/modify   ┌─────────┐                 │
    │ ATTACHED │─────────────────►│  DIRTY  │                 │
    └────┬────┘                   └────┬────┘                 │
         │                             │                       │
    layout_engine_compute()            │ (next frame)          │
         │                             │                       │
         v                             v                       │
    ┌──────────┐   compositor_frame  ┌──────────┐            │
    │  LAIDOUT  │───────────────────►│ RENDERED │────────────┘
    └──────────┘                     └──────────┘
         │
         └──► animation_engine_tick() (per-frame updates)
              (sets dirty on the node again → re-render loop)
```

---

## Stage 1: Creation

### Sequence (`gui_main.c:60`)

1. **`kheap.h`**: `kmalloc` the userdata struct, `memset` to zero.
2. **`node_create(type, name)`**: Allocates a `node_t`, assigns unique `id` from `g_scene->next_id++`, sets default `visible=true`, `interactive=true`, `opacity=1.0f`, `dirty=NODE_DIRTY_ALL`, `world_transform=identity`, `screen_bounds=zero`.
3. **Assign `vtable`**: Set `n->vtable = &mywidget_vtable` (a static const global).
4. **Set spatials**: `n->local_x`, `local_y`, `width`, `height`.
5. **`node_add_child(parent, child)`**: Links child to parent, sets `child->parent`, patches sibling linked list, increments `parent->child_count`, marks child `DIRTY_TRANSFORM | DIRTY_LAYOUT`.
6. **`layout_engine_compute()`**: Called explicitly after the tree is assembled (not automatic).

### Call chain

```
gui_init()
  └─ scene_graph_init()                        // alloc g_scene
  └─ mywidget_create()
       ├─ kmalloc(widget_data_t)           // private state
       ├─ node_create(NODE_MYWIDGET, name)     // alloc + init node
       ├─ n->vtable = &mywidget_vtable
       ├─ n->local_x = x; n->width = w; ...
       └─ n->userdata = d
  └─ node_add_child(parent, n)
  └─ layout_engine_compute(parent)
```

---

## Stage 2: Mutation

### Property changes

Setters modify node fields then call `node_mark_dirty()`:

| Function                      | Flags Set                          |
|-------------------------------|------------------------------------|
| `node_set_position(x, y)`     | `NODE_DIRTY_TRANSFORM`             |
| `node_set_size(w, h)`         | `NODE_DIRTY_LAYOUT | DIRTY_PAINT`  |
| `label_set_text(text)`        | `NODE_DIRTY_PAINT`                 |
| `button_set_text(text)`       | `NODE_DIRTY_PAINT`                 |
| `window_node_set_title(title)`| `NODE_DIRTY_PAINT`                 |
| `panel_set_bg_color(color)`   | `NODE_DIRTY_PAINT`                 |
| `theme_engine_set_color(id, c)`| (caller must invalidate manually)  |

### `node_mark_dirty()` propagation (`node.c:198`)

```c
void node_mark_dirty(node_t *node, uint32_t flags) {
    node->dirty |= flags;
    if (flags & NODE_DIRTY_TRANSFORM) {
        for (uint32_t i = 0; i < node->child_count; i++)
            node_mark_dirty(node->children[i], NODE_DIRTY_TRANSFORM);
    }
}
```

- Transform dirt propagates **down** to all descendants (transform is cumulative).
- Layout and paint dirt are **not** propagated — children that changed their own content must mark themselves.

---

## Stage 3: Rendering — The Compositor Frame

Called every frame via `compositor_frame()` (`compositor.c:198`):

```
compositor_frame(c)
│
├─ 1. input_manager_poll()           // read HID, post events
├─ 2. event_bus_dispatch()           // deliver events to subscribers
├─ 3. camera_update()               // pan/zoom inertia
├─ 4. animation_engine_tick()        // interpolate all active animations
│       └─ for each active animation:
│            └─ interp(start, end, frame, duration)
│            └─ apply to node (set position/size/color)
│            └─ node_mark_dirty()
│
├─ 5. node_update_transforms(root, identity)
│       └─ recursively: if DIRTY_TRANSFORM
│            └─ world_transform = local_transform ∘ parent_world
│            └─ clear DIRTY_TRANSFORM
│            └─ recurse to children
│
├─ 6. draw_background()              // fill canvas with dot grid
├─ 7. node_draw_recursive(root)
│       └─ for each visible node (depth-first, z-order):
│            ├─ if vtable->draw exists:
│            │     compute screen_bounds from world_transform + camera
│            │     fill rect, draw border, draw text
│            │     clear NODE_DIRTY_PAINT
│            └─ recurse children
│
├─ 8. cursor_draw()                 // cursor always topmost
└─ 9. renderer_present()            // flip backbuffer → VRAM
```

### Transform Update (`node.c:214`)

```c
void node_update_transforms(node_t *node, gui_transform_t parent_world) {
    if (node->dirty & NODE_DIRTY_TRANSFORM) {
        gui_transform_t local = transform_translation(node->local_x, node->local_y);
        node->world_transform = transform_concat(local, parent_world);
        node->dirty &= ~NODE_DIRTY_TRANSFORM;
    }
    for (uint32_t i = 0; i < node->child_count; i++)
        node_update_transforms(node->children[i], node->world_transform);
}
```

### Draw Recursion (`node.c:235`)

```c
void node_draw_recursive(node_t *node, gui_renderer *r) {
    if (!node || !node->visible) return;
    if (node->vtable && node->vtable->draw) node->vtable->draw(node, r);
    for (uint32_t i = 0; i < node->child_count; i++)
        node_draw_recursive(node->children[i], r);
    node->dirty &= ~NODE_DIRTY_PAINT;
}
```

---

## Stage 4: Event Handling

```
input_manager_poll()
  └─ event_bus_post(MOUSE_MOVE, MOUSE_DOWN, KEY_DOWN, ...)
       └─ event_bus_dispatch(bus)
            ├─ [CRITICAL]   focus_manager (GUI_EVENT_NONE subscription)
            │    ├─ MOUSE_DOWN: node_hit_test() → focus_manager_set_focus()
            │    │    ├─ sends WIN_BLUR to old focus
            │    │    ├─ sends WIN_FOCUS to new focus
            │    │    └─ window_manager_bring_to_front(new)
            │    └─ KEY_DOWN/UP/CHAR: dispatch to focused_node->on_event()
            │         ├─ Tab key → focus_manager_focus_next()
            │         └─ else focused widget handles it
            │
            ├─ [HIGH]     tool_manager (pan/zoom/select tools)
            │
            ├─ [NORMAL]   scene graph capture→target→bubble
            │    └─ node_hit_test() on MOUSE_DOWN/UP/MOVE
            │         └─ calls widget->vtable->on_event() if hit
            │
            └─ [LOW]      window_manager (WIN_FOCUS → bring_to_front)
```

### Event flow for a button click (detailed):

1. User clicks mouse → IRQ → `input_manager_poll()` reads mouse, posts `GUI_EVENT_MOUSE_DOWN` (button=1, x, y).
2. `event_bus_dispatch()` delivers to focus manager (subscribed to `GUI_EVENT_NONE` — all events).
3. Focus manager calls `node_hit_test(root, x, y)`.
4. `node_hit_test()` checks children in reverse z-order, then tests `rect_contains_point(screen_bounds)`.
5. Returns the deepest, topmost interactive node.
6. Focus manager: if hit is not `NODE_CANVAS`, calls `focus_manager_set_focus(hit)`.
7. Event bus continues delivery; tool manager gets the event (for drag operations).
8. If the event reaches the button's node during scene graph traversal, `button_on_event()` runs:
   - Checks `hovered` state
   - Sets `pressed = true`
   - Starts color animation to `BUTTON_BG_PRESS`
   - Returns `true` (consumed)

---

## Stage 5: Layout

Layout is triggered explicitly or when `NODE_DIRTY_LAYOUT` is set.

```
Property change (set_size, add_child, set_layout_type)
  └─ node_mark_dirty(node, NODE_DIRTY_LAYOUT)
       └─ next compositor_frame()
            └─ caller calls layout_engine_compute(node)
                 ├─ respects vtable->layout override if set
                 ├─ if LAYOUT_VBOX: layout_vbox(node)
                 │    └─ flex weight distro, alignment, padding/margin
                 ├─ if LAYOUT_HBOX: layout_hbox(node)
                 ├─ else LAYOUT_ABSOLUTE: just recurse
                 └─ clears NODE_DIRTY_LAYOUT on node
```

Note: Layout is **not** automatically called in `compositor_frame()`. It must be triggered explicitly (typically during construction or in response to resize events).

---

## Stage 6: Animation

```
animation_start(node, prop, custom_target, start, end, frames)
  └─ finds existing anim for same node+prop → overwrites
  └─ or finds free slot in static s_animations[64]
  └─ sets: target, prop, start_val, end_val, duration_frames, current_frame=0, active=true

animation_engine_tick() — called every compositor frame
  └─ for each active animation:
       ├─ current_frame++
       ├─ interp(start, end, current_frame, duration_frames)
       ├─ if COLOR: interp per-channel ARGB
       ├─ apply: X/Y → node_set_position, WIDTH/HEIGHT → direct set + dirty flags
       ├─ OPACITY_FP → node->opacity = val/65536.0f + dirty
       ├─ COLOR → write to color_target uint32_t + dirty
       └─ if current_frame >= duration → active = false
  └─ returns true if any animation still running (triggers redraw keep-alive)
```

---

## Stage 7: Destruction

```
node_destroy(node)
  │
  ├─ 1. Destroy children (depth-first recursive)
  │     └─ for each child: node_destroy(child)
  │          └─ calls vtable->destroy(child)   // free userdata + sub-allocations
  │          └─ kfree(child)
  │
  ├─ 2. Calls vtable->destroy(self)             // free userdata + sub-allocations
  │     └─ button: kfree(d->text), kfree(d)
  │     └─ label: kfree(d->text), kfree(d)
  │     └─ window: kfree(d->title), kfree(d)
  │     └─ panel: kfree(d)
  │
  ├─ 3. animation_cancel_all(node)              // mark all anims targeting this node as inactive
  │
  └─ 4. kfree(node)
       └─ g_scene->node_count--
```

### vtable->destroy implementations

Each widget's destroy function is responsible for freeing its `userdata`:

| Widget  | Frees                                  |
|---------|----------------------------------------|
| Window  | `d->title` (kfree'd), then `d`        |
| Button  | `d->text` (kfree'd), then `d`         |
| Label   | `d->text` (kfree'd), then `d`           |
| Panel   | `d` (no sub-allocations)              |

**Important**: `node_destroy()` calls `vtable->destroy` **after** destroying children. The subtype destructor should not walk children — `node_destroy()` handles that.

---

## Dependencies

| Stage       | Module                    | Key Functions                       |
|------------|---------------------------|-------------------------------------|
| Creation   | `kheap.h`                 | `kmalloc`, `kfree`                  |
| Creation   | `scene/node.h`            | `node_create`, `node_add_child`     |
| Mutation   | `scene/node.h`            | `node_mark_dirty`, `node_set_position`, `node_set_size` |
| Rendering  | `render/compositor.h`     | `compositor_frame`                  |
| Rendering  | `scene/node.h`            | `node_update_transforms`, `node_draw_recursive` |
| Events     | `core/event_bus.h`       | `event_bus_dispatch`, `event_bus_post` |
| Events     | `window/focus_manager.h` | `focus_manager_set_focus`           |
| Layout     | `layout/layout_engine.h`  | `layout_engine_compute`              |
| Animation  | `core/animation_engine.h` | `animation_start`, `animation_engine_tick` |
| Destruction| `scene/node.h`            | `node_destroy`                      |

---

## Limitations & Trade-offs

- **No micro-lifecycles**: No explicit `mount`, `unmount`, or `visibility-change` hooks. Widgets must check `visible` flag in draw.
- **Deferred layout**: Layout is not automatically triggered by `compositor_frame`. Explicit calls or resize event handling required.
- **Transform recomputation**: Always walks the full tree every frame (could optimize to only dirty subtrees).
- **No async destruction**: `node_destroy()` must be called from the compositor task; calling from an event handler is unsafe if the widget is processing events.

---

## Usage Example — Full Lifecycle

```c
/* CREATION */
node_t *win = window_node_create("demo", 100, 100, 0, 0, "My Window");
node_add_child(root, win);

/* MUTATION — animate size */
animation_start(win, ANIM_PROP_WIDTH, NULL, 0, 300, 30);
animation_start(win, ANIM_PROP_HEIGHT, NULL, 0, 200, 30);

/* MUTATION — set text */
button_set_text(btn, "New Label");

/* DESTRUCTION — when window closes */
node_destroy(win); // depth-first: destroys children first, then window
```