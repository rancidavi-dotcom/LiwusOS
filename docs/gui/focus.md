# LiwusOS GUI — Focus Manager

## Objective

Document the focus management subsystem that tracks which node currently has keyboard focus and routes keyboard events to it. Handles focus-on-click, focus change events (WIN_BLUR/WIN_FOCUS), and Tab-based focus traversal.

---

## Problems Solved

- **Keyboard routing**: Without focus management, keyboard events would have no directed target. The focus manager ensures keys reach the intended widget.
- **Focus-on-click**: When the user clicks a widget, it receives focus automatically.
- **Canvas exclusion**: Clicking the bare canvas (NODE_CANVAS) does NOT set focus — the focus manager explicitly skips it.
- **Focus visibility**: Owned and blur events allow widgets to visually indicate focus state.
- **Z-order integration**: Focus change triggers `window_manager_bring_to_front()` via the WIN_FOCUS event.

---

## Architecture

```
                    ┌──────────────────────┐
                    │   GUI Event Bus      │
                    └───────┬──────────────┘
                            │
              ┌─────────────┴──────────────┐
              │                            │
              ▼                            ▼
    ┌─────────────────┐        ┌────────────────────┐
    │  focus_manager   │        │  tool_manager       │
    │  (sub ALL events)│        │  (sub HIGH events)  │
    └──────┬──────────┘        └────────────────────┘
           │
           ├─ MOUSE_DOWN: node_hit_test() → set_focus()
           │    └─ skip NODE_CANVAS
           │    └─ WIN_BLUR → old node
           │    └─ WIN_FOCUS → new node
           │
           ├─ KEY_DOWN/UP/CHAR:
           │    └─ focused_node->vtable->on_event()
           │    └─ consumed? → stop
           │    └─ Tab key → focus_manager_focus_next()
           │
           └─ state: fm->focused_node
```

### Keyboard Event Routing

```
KEY_DOWN sent to event bus
       │
       ▼
focus_bus_handler() intercepts (subscribed to GUI_EVENT_NONE)
       │
       ├─ is focused_node != NULL?
       │    └─ YES → vtable->on_event(focused_node, &event)
       │         └─ returned true? → event consumed, stop
       │
       └─ is keycode == Tab (0x0F)?
            └─ YES → focus_manager_focus_next()
            └─ NO  → event propagates to other subscribers
```

---

## Data Structure

Defined in `src/kernel/gui/window/focus_manager.c:8`:

```c
struct focus_manager {
    gui_event_bus_t *bus;
    node_t          *scene_root;
    node_t          *focused_node;
    uint32_t         sub_id;
};
```

| Field          | Purpose                                    |
|----------------|--------------------------------------------|
| `bus`          | Event bus reference (for subscription)        |
| `scene_root`   | Root of the scene graph (for hit-testing)  |
| `focused_node` | Currently focused node (or NULL)           |
| `sub_id`       | Subscription ID for `focus_bus_handler`    |

---

## API

### `focus_manager_create()` (`focus_manager.c:43`)

```c
focus_manager_t *focus_manager_create(gui_event_bus_t *bus, node_t *root);
```

- Allocates a `focus_manager_t` on the kernel heap.
- Subscribes to `GUI_EVENT_NONE` (ALL events) on the event bus with handler `focus_bus_handler`.
- Returns NULL on allocation failure.
- Called in `gui_init()` at `gui_main.c:142`.

### `focus_manager_destroy()` (`focus_manager.c:58`)

```c
void focus_manager_destroy(focus_manager_t *fm);
```

- Unsubscribes the handler from the bus.
- Frees the focus manager structure.
- Does NOT destroy the focused node (ownership belongs to the scene graph).

### `focus_manager_get_focus()` (`focus_manager.c:66`)

```c
node_t *focus_manager_get_focus(focus_manager_t *fm);
```

- Returns the currently focused node pointer, or NULL if no node has focus.

### `focus_manager_set_focus()` (`focus_manager.c:71`)

```c
void focus_manager_set_focus(focus_manager_t *fm, node_t *node);
```

Sets focus to a new node. The sequence is:

```
focus_manager_set_focus(fm, new_focus)
│
├─ if fm->focused_node == new_focus → no-op (return)
│
├─ 1. Old focus blur:
│     if old_focus:
│       ev.type = GUI_EVENT_WIN_BLUR
│       ev.generic.a = (uint64_t)old_focus
│       old_focus->vtable->on_event(old_focus, &ev)
│       node_mark_dirty(old_focus, NODE_DIRTY_PAINT)
│
├─ 2. fm->focused_node = new_focus
│
└─ 3. New focus:
     if new_focus:
         ev.type = GUI_EVENT_WIN_FOCUS
         ev.generic.a = (uint64_t)new_focus
         new_focus->vtable->on_event(new_focus, &ev)
         node_mark_dirty(new_focus, NODE_DIRTY_PAINT)
```

### `focus_manager_focus_next()` (`focus_manager.c:96`)

```c
void focus_manager_focus_next(focus_manager_t *fm);
```

Stub — currently a TODO:

```c
void focus_manager_focus_next(focus_manager_t *fm) {
    if (!fm || !fm->scene_root) return;
    /* TODO: Implement focus traversal by walking the tree
       looking for interactive nodes */
}
```

Planned behavior: walk the scene graph depth-first, find the next `interactive` node after the currently focused one, and call `focus_manager_set_focus()` on it.

---

## Event Bus Handler

The core event processing logic (`focus_manager.c:15`):

```c
static void focus_bus_handler(const gui_event_t *event, void *userdata) {
    focus_manager_t *fm = (focus_manager_t *)userdata;

    if (event->type == GUI_EVENT_MOUSE_DOWN && event->mouse.button == 1) {
        node_t *hit = node_hit_test(fm->scene_root, event->mouse.x, event->mouse.y);
        if (hit && hit->type == NODE_CANVAS) hit = NULL;
        focus_manager_set_focus(fm, hit);
        return; /* Do not consume — let tools/widgets process it */
    }

    if (event->type == GUI_EVENT_KEY_DOWN ||
        event->type == GUI_EVENT_KEY_UP ||
        event->type == GUI_EVENT_KEY_CHAR) {

        if (fm->focused_node && fm->focused_node->vtable &&
            fm->focused_node->vtable->on_event) {
            bool consumed = fm->focused_node->vtable->on_event(
                                fm->focused_node, event);
            if (consumed) return;
        }

        if (event->type == GUI_EVENT_KEY_DOWN &&
            event->key.keycode == 0x0F /* Tab */) {
            focus_manager_focus_next(fm);
            return;
        }
    }
}
```

Key design points:

- **Subscribed to ALL events** (`GUI_EVENT_NONE`): The focus manager processes at the highest priority (CRITICAL by subscription order), intercepting mouse clicks before any other handler.
- **MOUSE_DOWN → hit test**: Performs `node_hit_test()` and sets focus. Does NOT consume the event — tools (pan/move/select) and widgets still receive it.
- **Canvas exclusion**: If `hit->type == NODE_CANVAS`, the hit is discarded (focus is set to NULL), preventing the canvas background from stealing focus.
- **Keyboard dispatch**: Only the focused node receives keyboard events. The event does NOT propagate through the scene graph's normal capture→target→bubble path — it's routed directly to `focused_node->vtable->on_event()`.
- **Tab traversal**: Only triggered if no other handler consumed a KEY_DOWN with keycode 0x0F.
- **Non-consuming**: Returns from the handler without calling `event_stop_propagation()`, allowing other subscribers (tools, widgets) to also process the same event.

---

## Focus Change Events

When focus changes, two synthetic events are generated:

### `GUI_EVENT_WIN_BLUR` (type=11)

Sent to the previously focused node. Payload in `event.generic.a` contains the old node pointer.

```c
ev.type = GUI_EVENT_WIN_BLUR;
ev.generic.a = (uint64_t)old;
old->vtable->on_event(old, &ev);
node_mark_dirty(old, NODE_DIRTY_PAINT);
```

### `GUI_EVENT_WIN_FOCUS` (type=10)

Sent to the newly focused node. Payload in `event.generic.a` contains the new node pointer.

```c
ev.type = GUI_EVENT_WIN_FOCUS;
ev.generic.a = (uint64_t)node;
node->vtable->on_event(node, &ev);
node_mark_dirty(node, NODE_DIRTY_PAINT);
```

### Window Manager Integration

The window manager subscribes to `GUI_EVENT_WIN_FOCUS` and calls `window_manager_bring_to_front(focused_node)`, reordering siblings so the focused window appears on top in z-order (`window_manager.c:14`).

---

## Hit-Testing Details

`node_hit_test()` (`node.c:160`):

```c
node_t *node_hit_test(node_t *root, int screen_x, int screen_y) {
    if (!root || !root->visible) return NULL;

    // Children in reverse order (topmost first)
    for (int i = (int)root->child_count - 1; i >= 0; i--) {
        node_t *hit = node_hit_test(root->children[i], screen_x, screen_y);
        if (hit) return hit;
    }

    // Test this node
    if (root->interactive &&
        rect_contains_point(root->screen_bounds, screen_x, screen_y)) {
        return root;
    }

    return NULL;
}
```

- **Depth-first, reverse order**: Children are tested in reverse z-order (highest z-index first), so the topmost node is found first.
- **Only interactive nodes**: Nodes with `interactive = false` are skipped (e.g., labels, panels, non-interactive containers).
- **screen_bounds must be valid**: `screen_bounds` is updated by each widget's `draw()` function during rendering. If a widget has not been drawn yet, its `screen_bounds` will be zero and hit-testing will fail.
- **Canvas skip**: The focus manager explicitly sets the hit to NULL if the result is a `NODE_CANVAS` node.

---

## Integration with Compositor Frame

From `compositor_frame()` (`compositor.c:198`):

```
compositor_frame()
│
├─ 1. input_manager_poll()       // HID → event bus
├─ 2. event_bus_dispatch()       // focus_manager processes first
│      ├─ MOUSE_DOWN → hit-test → set_focus → WIN_BLUR/WIN_FOCUS
│      └─ KEY_DOWN → route to focused_node→on_event
│
├─ 3. camera_update()
├─ 4. animation_engine_tick()
├─ 5. node_update_transforms()
├─ 6. draw_background()
├─ 7. node_draw_recursive()     // focus indicator painted by widget
├─ 8. cursor_draw()
└─ 9. renderer_present()
```

The focus manager operates entirely within the event dispatch phase. It does not draw anything itself — focus indicators (e.g., focus ring, highlighted border) are the responsibility of individual widgets in their `draw()` function, typically by checking whether `focus_manager_get_focus(fm) == self`.

---

## Dependencies

| Module         | Header                       | Usage                                   |
|----------------|------------------------------|-----------------------------------------|
| Event Bus      | `core/event_bus.h`          | Event subscription, event types         |
| Node           | `scene/node.h`              | Hit-testing, dirty flags, vtable access |
| KHeap          | `kheap.h`                   | `kmalloc`/`kfree` for focus manager     |
| Window Manager | `window/window_manager.h`   | `window_manager_bring_to_front()` (via event) |
| String         | `string.h`                   | `memset` for zeroing struct             |

---

## Limitations & Trade-offs

- **No visual focus indicator**: The focus manager does not draw any focus ring or highlight. Individual widgets must implement their own focus visuals by checking focus state.
- **Tab traversal is a stub**: `focus_manager_focus_next()` is not implemented. Tab key handling exists but does nothing.
- **Single focus target**: Only one node can have focus at a time. No multi-focus or focus groups.
- **No focus-by-direction**: Arrow-key focus navigation is not supported (planned for Phase 4+).
- **Canvas exclusion is hardcoded**: `NODE_CANVAS` is explicitly skipped. This should be a configurable property.
- **Event handler does not stop propagation**: The focus bus handler returns without calling `event_stop_propagation()`, meaning all other subscribers on the bus still receive mouse/key events. This is intentional for tool integration but could lead to double-processing if not carefully designed.
- **No trackpad scrolling support**: Focus manager does not handle scroll wheel events.
- **No programmatic focus API**: Other than `focus_manager_set_focus()`, there is no `request_focus()` or `focus_nearest()` method.
- **Focus survives node destruction**: If the focused node is destroyed, `fm->focused_node` becomes a dangling pointer. The caller should clear focus before destroying a focused widget.

---

## Performance

- **O(1) operations**: `get_focus`, `set_focus` are constant time.
- **Hit-test O(n)**: Walks all nodes in the scene graph per MOUSE_DOWN.
- **No per-frame overhead**: Focus manager does nothing during the frame besides the event dispatch phase.

---

## Future Extensions

- **`focus_manager_focus_next()`**: Full Tab navigation with depth-first traversal of interactive nodes.
- **`focus_manager_focus_prev()`**: Shift+Tab reverse traversal.
- **Focus rings**: Default focus-indicator drawing in the compositor or as a separate overlay.
- **Arrow key navigation**: D-pad / arrow keys for spatial focus movement.
- **Focus groups**: Isolated focus scopes within panels for modal dialogs.
- **Focus debugging overlay**: Visual debug mode showing the focus path and interactive node tree.

---

## Usage Examples

### Creating a widget that responds to focus

```c
static bool mywidget_on_event(node_t *self, const gui_event_t *e) {
    if (e->type == GUI_EVENT_WIN_FOCUS) {
        // Draw focus indicator
        node_mark_dirty(self, NODE_DIRTY_PAINT);
        return true;
    }
    if (e->type == GUI_EVENT_WIN_BLUR) {
        // Remove focus indicator
        node_mark_dirty(self, NODE_DIRTY_PAINT);
        return true;
    }
    // ... handle other events
    return false;
}
```

### Drawing a focus indicator

```c
static void mywidget_draw(node_t *self, gui_renderer *r) {
    // Normal drawing...
    renderer_fill_rect(r, self->screen_bounds, d->bg_color);

    // Focus indicator
    extern focus_manager_t *s_focus; // or via event bus
    if (s_focus && focus_manager_get_focus(s_focus) == self) {
        renderer_draw_rect(r, self->screen_bounds, 0xFFFF0000, 2);
    }
}
```

### Clearing focus before destruction

```c
if (node == focus_manager_get_focus(s_focus)) {
    focus_manager_set_focus(s_focus, NULL);
}
node_destroy(node);
```