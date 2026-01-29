# Input Manager — LiwusOS GUI Input Subsystem

## Objective

The Input Manager is the **sole** module authorized to read hardware input state (mouse, keyboard). It normalises raw hardware signals into typed, delta-based `gui_event_t` payloads posted to the Event Bus. No widget, tool, or compositor component may read hardware registers or call `get_mouse_x()`, `keyboard_is_pressed()`, etc. directly. This enforces a clean decoupling between hardware drivers and UI logic.

## Problems Solved

- **Hardware abstraction**: mouse.c and keyboard.c are polled only by `input_manager_poll()`. If the underlying driver changes (e.g., switching from PS/2 to USB HID), only the Input Manager is affected.
- **Delta event generation**: instead of every consumer computing `prev vs current` state, the Input Manager diffs frame-over-frame and posts events only on transitions (mouse move when position changes, button down/up on edges, key down/up on edges).
- **Modifier tracking**: a single `modifiers` byte (bitmask of `MOD_SHIFT`, `MOD_CTRL`, `MOD_ALT`, `MOD_SUPER`) is computed each frame from scancode state and attached to key events. Consumers need not track modifier keys individually.
- **LCtrl as left-click**: scancode `0x1D` (Left Control) sets `cur_btn[1]` (left button) high, enabling single-handed canvas interaction on laptops without external mice.

## Architecture

```
+-------------------+       +-----------------------+       +------------------+
|   mouse.c         |       |  input_manager_t      |       |  gui_event_bus_t |
|   get_mouse_x()   |-----> |                       |-----> |  event_bus_post() |
|   get_mouse_y()   |       |  .prev_mx, .prev_my   |       +------------------+
|   is_left_clicked()|      |  .cur_mx, .cur_my     |               │
|   is_right_clicked()|      |  .prev_btn[4]         |               ▼
+-------------------+       |  .cur_btn[4]          |       +------------------+
                            |  .prev_keys[128]      |       |  Subscribers     |
+-------------------+       |  .cur_keys[128]       |       |  ToolManager     |
|   keyboard.c      |-----> |  .modifiers (uint8_t) |       |  FocusManager    |
|   keyboard_is_pressed()|  +-----------------------+       |  WindowManager   |
+-------------------+                                         +------------------+
         │                                                           │
         │           input_manager_poll() called once                │
         │           per compositor_frame()                          │
         └───────────────────────────────────────────────────────────┘
```

## Lifecycle

### Initialisation
```c
gui_event_bus_t *bus = event_bus_create();
input_manager_t *im = input_manager_create(bus);
```
The Input Manager stores a reference to the bus but does **not** subscribe to it (it is a producer only).

### Per-Frame Poll
```c
void input_manager_poll(input_manager_t *im);
```
Called once per `compositor_frame()`. Sequence:
1. Copy `cur_*` → `prev_*` (save previous frame state).
2. Read hardware via `get_mouse_x()`, `get_mouse_y()`, `is_left_clicked()`, `is_right_clicked()`, `keyboard_is_pressed()` for all 128 scancodes.
3. Apply LCtrl-as-left-click rule.
4. Compute modifiers from current scancode state.
5. Diff state and post delta events:
   - `event_post_mouse_move()` — only if `dx != 0 || dy != 0`
   - `event_post_mouse_button()` — on rising/falling edge per button (1–3)
   - `event_post_key()` — on rising/falling edge per scancode (1–127)

### Destruction
```c
void input_manager_destroy(input_manager_t *im);
```
Frees the `input_manager_t` struct. Does **not** destroy the Event Bus (owned by the compositor).

## APIs

### Public API (`input_manager.h`)

```c
input_manager_t *input_manager_create(gui_event_bus_t *bus);
void             input_manager_destroy(input_manager_t *im);
void input_manager_poll(input_manager_t *im);

int  input_mouse_x(const input_manager_t *im);
int  input_mouse_y(const input_manager_t *im);
bool input_mouse_button(const input_manager_t *im, uint8_t button);
bool input_key_held(const input_manager_t *im, uint8_t scancode);
uint8_t input_modifiers(const input_manager_t *im);
```

### Modifier Bitmasks

```c
#define MOD_SHIFT  (1u << 0)   /* scancode 0x2A or 0x36 */
#define MOD_CTRL   (1u << 1)   /* scancode 0x1D          */
#define MOD_ALT    (1u << 2)   /* scancode 0x38          */
#define MOD_SUPER  (1u << 3)   /* scancode 0x5B          */
```

### Private (Internal) State

```c
struct input_manager {
    gui_event_bus_t *bus;
    int     prev_mx, prev_my;
    bool    prev_btn[4];           /* index 1=Left 2=Right 3=Middle */
    bool    prev_keys[128];        /* scancode → previous frame     */
    int     cur_mx, cur_my;
    bool    cur_btn[4];
    bool    cur_keys[128];
    uint8_t modifiers;
};
```

### Scancode Constants (used in codebase)

| Scancode | Key      | Purpose              |
|----------|----------|----------------------|
| `0x1D`   | LCtrl    | Modifier + click emu |
| `0x2A`   | LShift   | Modifier             |
| `0x36`   | RShift   | Modifier             |
| `0x38`   | LAlt     | Modifier             |
| `0x5B`   | LWin     | Modifier (Super)     |
| `0x0D`   | `=` / `+`| Zoom in (PanTool)    |
| `0x0C`   | `-` / `_`| Zoom out (PanTool)   |
| `0x23`   | `H`      | Home/Reset camera    |
| `0x21`   | `F`      | Fit all (PanTool)    |

## Dependencies

- `mouse.h` — `get_mouse_x()`, `get_mouse_y()`, `is_left_clicked()`, `is_right_clicked()`
- `keyboard.h` — `keyboard_is_pressed(scancode)`
- `event_bus.h` — `event_post_mouse_move()`, `event_post_mouse_button()`, `event_post_key()`
- `kheap.h` — `kmalloc()` / `kfree()`

## Limitations / Trade-offs

| Limitation | Rationale |
|---|---|
| Keyboard limited to 128 scancodes | Set 1 scancodes fit in 128 bytes; extended keys (multimedia, F13+) unsupported |
| No key repeat events | Repeat is the responsibility of the consumer (e.g., text editor via `KEY_CHAR`) |
| Mouse scroll is not generated | Scroll detection not yet implemented in `mouse.c`; reserved for `cur_btn[3]` / delta |
| Single bus, single producer | The kernel is single-CPU; IRQ handlers may post, but there is one poller |
| `float` in `transform_apply` | `transform_invert_simple` uses `float` — a known issue for the `-mno-sse` build (see coordinate_system.md) |
| No touch input | Touchscreen/pointer support is a future extension |

## Performance / Memory Optimisations

- `input_manager_t` is 2.4 KB (`128 × 2 bool + 4 × 2 bool + 2 int + 1 uint8 + pointer`); allocated once from kernel heap.
- No dynamic allocation during `poll()` — all state is pre-allocated.
- Delta filtering reduces bus traffic: no redundant `MOUSE_MOVE` events when the cursor is stationary.
- `compute_modifiers()` runs in O(4) — only checks the four modifier scancodes.

## Future Extensions

- Keyboard repeat generation with configurable delay/rate.
- Mouse scroll wheel → `GUI_EVENT_MOUSE_SCROLL` events.
- Touch input → `GUI_EVENT_TOUCH_*` event types.
- Multi-monitor: screen-space coordinates need a display origin offset.
- Input remapping / accessibility layer between hardware read and event post.

## Usage Examples

### Compositor Frame Loop (driver)
```c
void compositor_frame(compositor_t *c) {
    /* 1. Poll input → posts events */
    input_manager_poll(c->input);

    /* 2. Dispatch events to subscribers */
    event_bus_dispatch(c->bus);

    /* 3. ...render, present... */
}
```

### Immediate State Query (tool)
```c
static bool pan_event(tool_t *self, const gui_event_t *e) {
    /* PanTool does NOT poll hardware — it receives events */
    if (e->type == GUI_EVENT_KEY_DOWN) {
        if (e->key.scancode == 0x23) {  /* H key */
            camera_reset(self->camera);
            return true;
        }
    }
    return false;
}
```

### Tool Manager Subscriber (consumer)
```c
/* tool_manager.c subscribes with GUI_EVENT_NONE (all events) */
static void on_event(const gui_event_t *event, void *userdata) {
    tool_manager_t *tm = (tool_manager_t *)userdata;
    if (event->type < GUI_EVENT_MOUSE_MOVE || event->type > GUI_EVENT_KEY_CHAR)
        return;
    for (int i = 0; i < tm->tool_count; i++) {
        if (tool_event(tm->tools[i], event))
            break;
    }
}
```

---

*Document v1.0 — reflects kernel/gui/input/input_manager.{h,c} as of LiwusOS build.*
