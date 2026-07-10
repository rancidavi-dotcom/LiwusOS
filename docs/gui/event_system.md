# Event System — Typed, Prioritised Event Bus

## Objective

Provide a **decoupled, typed, prioritised** event distribution mechanism that decouples event producers (Input Manager, animation engine, window manager) from consumers (tools, widgets, focus manager). Events are posted to a fixed-capacity ring buffer and dispatched each frame in priority order. The system supports subscription filtering, stop-propagation, and is designed for future Capture→Target→Bubble scene graph routing.

## Problems Solved

- **Producer-consumer decoupling**: hardware drivers never call widget code directly. The Input Manager posts `gui_event_t` structs; subscribers receive them via callbacks.
- **Frame-atomic dispatch**: all events posted during a frame are drained atomically by `event_bus_dispatch()`, sorted by priority, and delivered in a single batch. Handlers may post new events safely (they go into the ring for the next frame).
- **Typed payloads**: a tagged union (`gui_mouse_payload_t`, `gui_key_payload_t`, `gui_generic_payload_t`) keeps the event struct compact (32–40 bytes) while supporting all current input types without casting.
- **Priority ordering**: `CRITICAL(0)` events are delivered before `HIGH(1)`, `NORMAL(2)`, and `LOW(3)`. Insertion sort is used on the pending snapshot (N ≤ 256, O(N²) is acceptable).
- **Interrupt safety**: `event_bus_post()` uses a simple non-blocking ring buffer — safe to call from IRQ handlers (though the kernel is currently single-threaded).
- **Subscriber filtering**: handlers register for a specific `gui_event_type_t`, or use `GUI_EVENT_NONE` as a wildcard to receive all events (used by ToolManager).

## Architecture

```
                     event_bus_post()
                     ┌──────────────┐
                     │   Ring Buf   │
  InputManager ─────▶│   [256]      │
  AnimationEngine ──▶│  head/tail   │
  WindowManager  ───▶│  count       │
                     └──────┬───────┘
                            │  drain
                            ▼
                     ┌──────────────┐
                     │   pending[]  │   snapshot + insertion sort
                     │  (priority)  │
                     └──────┬───────┘
                            │  for each event
                            ▼
                     ┌──────────────────┐
                     │  Subscription    │
                     │  Table (×64)     │
                     │  ┌────────────┐  │
                     │  │ filter     │  │  ← GUI_EVENT_NONE = wildcard
                     │  │ handler fn │  │
                     │  │ userdata*  │  │
                     │  └────────────┘  │
                     └──────────────────┘
                            │
                            ▼
                   ┌────────────────────┐
                   │  Handler Callback  │
                   │                    │
                   │  event_stop_       │
                   │  propagation()     │
                   └────────────────────┘

    Scene Graph Routing (future):
    Capture → Target → Bubble
```

## Event Types

```c
typedef enum {
    /* Input */
    GUI_EVENT_NONE           = 0,   /* wildcard for subscription */
    GUI_EVENT_MOUSE_MOVE     = 1,
    GUI_EVENT_MOUSE_DOWN     = 2,
    GUI_EVENT_MOUSE_UP       = 3,
    GUI_EVENT_MOUSE_SCROLL   = 4,
    GUI_EVENT_KEY_DOWN       = 5,
    GUI_EVENT_KEY_UP         = 6,
    GUI_EVENT_KEY_CHAR       = 7,
    GUI_EVENT_MOUSE_ENTER    = 8,
    GUI_EVENT_MOUSE_LEAVE    = 9,

    /* Window lifecycle */
    GUI_EVENT_WIN_FOCUS      = 10,
    GUI_EVENT_WIN_BLUR       = 11,
    GUI_EVENT_WIN_CLOSE      = 12,
    GUI_EVENT_WIN_MOVE       = 13,
    GUI_EVENT_WIN_RESIZE     = 14,

    /* Canvas / Camera */
    GUI_EVENT_CANVAS_PAN     = 20,
    GUI_EVENT_CANVAS_ZOOM    = 21,

    /* Node changes */
    GUI_EVENT_NODE_DIRTY     = 30,
    GUI_EVENT_NODE_ADDED     = 31,
    GUI_EVENT_NODE_REMOVED   = 32,

    /* System */
    GUI_EVENT_FRAME_BEGIN    = 40,
    GUI_EVENT_FRAME_END      = 41,

    GUI_EVENT_TYPE_COUNT
} gui_event_type_t;
```

**Rule**: new IDs are appended at the end of each group. **Never renumber** — event type values are serialised in the ring buffer and persisted in subscription filters.

## Priority Levels

```c
typedef enum {
    GUI_PRIORITY_CRITICAL = 0,   /* focus changes, lifecycle */
    GUI_PRIORITY_HIGH     = 1,   /* mouse/keyboard input      */
    GUI_PRIORITY_NORMAL   = 2,   /* node dirty, canvas events */
    GUI_PRIORITY_LOW      = 3,   /* frame markers, debug      */
} gui_event_priority_t;
```

## Payload Structures

```c
/* Mouse — coordinates in SCREEN space (pixels) */
typedef struct {
    int x, y;          /* current cursor position */
    int dx, dy;         /* delta from previous frame */
    uint8_t button;     /* 0=none, 1=left, 2=right, 3=middle */
} gui_mouse_payload_t;

/* Keyboard */
typedef struct {
    uint8_t  scancode;  /* raw keyboard scancode (Set 1) */
    uint32_t keycode;   /* USB HID keycode (future) */
    uint32_t unicode;   /* decoded character (future) */
    uint8_t  modifiers; /* bit0=Shift bit1=Ctrl bit2=Alt bit3=Super */
} gui_key_payload_t;

/* Opaque 32-byte payload for custom events */
typedef struct {
    uint64_t a, b, c, d;
} gui_generic_payload_t;
```

### Event Struct

```c
typedef struct {
    gui_event_type_t     type;
    gui_event_priority_t priority;
    uint32_t             target_id;  /* 0 = broadcast */
    bool                 propagating;
    union {
        gui_mouse_payload_t   mouse;
        gui_key_payload_t     key;
        gui_generic_payload_t generic;
    };
} gui_event_t;
```

**Layout**: `type` (4B) + `priority` (4B) + `target_id` (4B) + `propagating` (1B + 3B padding) + payload (24B max) = **40 bytes**. 256 × 40 = 10,240 bytes for the ring buffer.

## Internal Structures

### Event Bus

```c
struct gui_event_bus {
    gui_event_t   ring[RING_CAPACITY];       /* 256 entries */
    uint32_t      head;                       /* next write     */
    uint32_t      tail;                       /* next read      */
    uint32_t      count;

    gui_event_t   pending[RING_CAPACITY];     /* dispatch snapshot */
    uint32_t      pending_count;

    subscriber_t  subs[MAX_SUBSCRIBERS];      /* 64 entries */
    uint32_t      sub_count;
    uint32_t      next_sub_id;
    bool          stop_propagation;
};
```

### Subscriber Table Entry

```c
typedef struct {
    gui_subscription_id_t  id;
    gui_event_type_t       filter;     /* GUI_EVENT_NONE = wildcard */
    gui_event_handler_t    handler;
    void                  *userdata;
    bool                   active;
} subscriber_t;
```

## Lifecycle

### Creation
```c
gui_event_bus_t *event_bus_create(void);
```
Allocates from kernel heap, zero-initialises ring and subscriber table, sets `next_sub_id = 1`.

### Subscription
```c
gui_subscription_id_t event_bus_subscribe(
    gui_event_bus_t      *bus,
    gui_event_type_t      type,
    gui_event_handler_t   handler,
    void                 *userdata);
```
Scans for first inactive slot, assigns a monotonically increasing ID. Returns 0 on failure (table full).

### Unsubscription
```c
void event_bus_unsubscribe(gui_event_bus_t *bus, gui_subscription_id_t id);
```
Marks the slot inactive; ID is **not** reused.

### Posting
```c
bool event_bus_post(gui_event_bus_t *bus, const gui_event_t *event);
```
Ring-buffer write. Returns `false` if the buffer is full (event dropped). Safe from interrupt context (no dynamic allocation, no blocking).

### Dispatch
```c
uint32_t event_bus_dispatch(gui_event_bus_t *bus);
```
1. **Drain** ring → `pending[]` snapshot.
2. **Sort** by priority (insertion sort, ascending).
3. **Deliver** to subscribers: for each subscriber whose `filter` matches (wildcard or exact type), call `handler(event, userdata)`.
4. **Stop propagation**: if a handler called `event_stop_propagation()`, skip remaining subscribers for this event.

Returns count of handler invocations.

## Convenience Posting Helpers

```c
static inline void event_post_mouse_move(gui_event_bus_t *bus,
    int x, int y, int dx, int dy);
    // type=MOUSE_MOVE, priority=HIGH, target_id=0, propagating=true

static inline void event_post_mouse_button(gui_event_bus_t *bus,
    int x, int y, uint8_t button, bool pressed);
    // type=DOWN/UP, priority=HIGH

static inline void event_post_key(gui_event_bus_t *bus,
    uint8_t scancode, bool pressed);
    // type=KEY_DOWN/UP, priority=HIGH

static inline void event_post_node_dirty(gui_event_bus_t *bus, uint32_t node_id);
    // type=NODE_DIRTY, priority=NORMAL, propagating=false
```

## Dependencies

- `kheap.h` — `kmalloc()` / `kfree()` for bus allocation
- `string.h` — `memset()` for initialisation
- `<stdint.h>`, `<stdbool.h>` — type definitions

## Limitations / Trade-offs

| Limitation | Rationale |
|---|---|
| Fixed 256-entry ring buffer | Capped at 256 to bound memory (10 KB); overflow drops events silently — acceptable for single-producer single-consumer |
| O(N²) insertion sort | N ≤ 256 events per frame; typical load is < 20 events. Simple, no heap allocation |
| 64-subscriber hard limit | Scalable for kernel GUI; expansion requires dynamic array |
| No Capture→Target→Bubble phases | Designed but not implemented — `target_id` is set but unused; `propagating` flag is respected but the tree walk is not coded |
| Single-thread dispatch | `event_bus_dispatch()` must be called from the compositor task only; not re-entrant |
| No event pooling | Events are copied by value; on a 40-byte struct this is negligible |

## Performance / Memory Optimisations

- Ring buffer avoids dynamic allocation: O(1) enqueue/dequeue.
- Snapshot-based dispatch allows handlers to call `event_bus_post()` without corrupting the iteration cursor.
- Subscriber table is a flat array — no linked-list overhead for 64 entries.
- Priority sort skips when `pending_count < 2`.
- `stop_propagation` short-circuits subscriber iteration per event.

## Safety Guarantees

- `event_stop_propagation()` resets to `false` for each event — never leaks across events.
- A wildcard subscriber (`GUI_EVENT_NONE`) receives all events; it must filter by `event->type` itself. Only ToolManager uses this.
- Zero-initialised structs: all fields are 0/NULL until explicitly set, preventing use of uninitialised subscription data.

## Future Extensions

### Scene Graph Event Routing (Capture → Target → Bubble)
```
Phase 1 (Capture):  walk root → target, calling on_event on each ancestor
Phase 2 (Target):   deliver to the target node
Phase 3 (Bubble):   walk target → root, calling on_event on each ancestor
                    stop_propagation cancels bubble
```
The `target_id` field in `gui_event_t` and the `propagating` flag already exist in the struct. A future `event_bus_dispatch_scene_graph()` will implement the walk using `node_find_by_id()`.

### Additional Event Types
- `GUI_EVENT_TOUCH_BEGIN / MOVE / END` for touchscreen support
- `GUI_EVENT_GESTURE_PINCH / SWIPE` for canvas gesture recognition
- `GUI_EVENT_DRAG_DROP` for DnD operations

### Priority Inversion Prevention
If a CRITICAL event posts a HIGH event in its handler, that event enters the ring and will be dispatched next frame — not preemptively. Future work could flush immediately for latency-sensitive events.

## Usage Examples

### Subscribing a Module (FocusManager)
```c
static void on_focus_event(const gui_event_t *e, void *userdata) {
    focus_manager_t *fm = (focus_manager_t *)userdata;
    if (e->type == GUI_EVENT_MOUSE_DOWN) {
        node_t *hit = node_hit_test(fm->scene_root, e->mouse.x, e->mouse.y);
        if (hit) focus_manager_set_focus(fm, hit);
    }
}

// In focus_manager_create:
fm->sub_id = event_bus_subscribe(bus, GUI_EVENT_MOUSE_DOWN,
                                  on_focus_event, fm);
```

### Posting a Custom Event
```c
gui_event_t ev;
ev.type = GUI_EVENT_WIN_CLOSE;
ev.priority = GUI_PRIORITY_HIGH;
ev.target_id = window_node->id;
ev.propagating = true;
ev.generic.a = window_node->id;
ev.generic.b = ev.generic.c = ev.generic.d = 0;
event_bus_post(bus, &ev);
```

### Checking Subscription Success
```c
gui_subscription_id_t sid = event_bus_subscribe(bus, GUI_EVENT_KEY_DOWN,
                                                 my_handler, my_data);
if (sid == 0) {
    // table full — log error
}
```

---

*Document v1.0 — reflects kernel/gui/core/event_bus.{h,c} as of LiwusOS build.*
