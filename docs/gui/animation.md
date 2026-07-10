# LiwusOS GUI — Animation Engine

## Objective

Document the frame-based tweening engine that interpolates node properties (position, size, opacity, color) over a configurable number of frames. Used for smooth visual transitions like button hover/press effects and window appearance animations.

---

## Problems Solved

- **Smooth visual feedback**: Buttons animate color changes on hover/press instead of snapping instantly.
- **Window appearance animations**: Windows animate size from zero to final dimensions on creation.
- **Decoupled from rendering**: Animation runs independently per frame, setting node properties and marking nodes dirty for the next paint cycle.
- **Simple, predictable API**: Linear interpolation with overwrite semantics — no external dependencies or complex easing curves.

---

## Architecture

```
                                                MAX_ANIMATIONS = 64
                                                    │
animation_start(node, prop, target, start, end, frames)
  │                                                  │
  └─ overwrite existing anim for same node+prop ─────┤
  └─ find free slot                                   │
       └─ s_animations[slot] = { target, prop, ... }  │
                                                      ▼
                                              ┌──────────────────┐
                                              │  s_animations[]  │
                                              │  static array    │
                                              │  [0..63]         │
                                              └────────┬─────────┘
                                                       │
compositor_frame ──► animation_engine_tick()
                          │
                          ├─ for each active:
                          │    ├─ current_frame++
                          │    ├─ interp(start, end, frame, duration)
                          │    │    ├─ (for color: per-channel ARGB interp)
                          │    ├─ apply to node property
                          │    │    ├─ X/Y → node_set_position
                          │    │    ├─ WIDTH/HEIGHT → direct set + dirty
                          │    │    ├─ OPACITY_FP → node->opacity = val/65536
                          │    │    └─ COLOR → *color_target = val + dirty
                          │    └─ if complete → active = false
                          └─ returns true if any running (keeps compositor loop alive)
```

---

## Data Structures

### Animation State Machine

```
         INACTIVE
            │
            │ animation_start()
            v
         ACTIVE ─────────────────────► COMPLETED
            │                              │
            │ current_frame++               │ (replaced by new
            │ interp and apply              │  animation_start()
            │                              │  or expires)
            v                              v
      current_frame >=        animation_start() for
      duration_frames        same node+prop → rewinds
            │                              │
            └──────────────────────────────┘
```

### `animation_t` (`animation_engine.h:20`)

```c
typedef struct {
    node_t     *target;           // node being animated
    anim_prop_t prop;             // which property
    uint32_t   *color_target;     // if COLOR: pointer to the uint32_t variable
    int         start_val;        // initial value
    int         end_val;          // final value
    int         duration_frames;  // total frames for animation
    int         current_frame;    // current frame (0-based)
    bool        active;           // whether this slot is in use
} animation_t;
```

### Global State

```c
static animation_t s_animations[MAX_ANIMATIONS];  // size = 64
```

---

## Property Types

| Enum                    | Value | Target Field          | Applied Via                     | Dirty Flags              |
|-------------------------|-------|-----------------------|---------------------------------|--------------------------|
| `ANIM_PROP_X`           | 0     | `node->local_x`       | `node_set_position(x, y)`       | `TRANSFORM`              |
| `ANIM_PROP_Y`           | 1     | `node->local_y`       | `node_set_position(x, y)`       | `TRANSFORM`              |
| `ANIM_PROP_WIDTH`       | 2     | `node->width`         | Direct set                      | `PAINT | LAYOUT`         |
| `ANIM_PROP_HEIGHT`      | 3     | `node->height`        | Direct set                      | `PAINT | LAYOUT`         |
| `ANIM_PROP_OPACITY_FP`  | 4     | `node->opacity`       | `opacity = val / 65536.0f`      | `PAINT`                  |
| `ANIM_PROP_COLOR`       | 5     | `*color_target`       | Write to userdata uint32_t      | `PAINT`                  |

---

## API

### `animation_engine_init()` (`animation_engine.c:9`)

```c
void animation_engine_init(void);
```

Zeros the entire `s_animations` array. Called during `gui_init()` before any widgets are created.

### `animation_engine_tick()` (`animation_engine.c:83`)

```c
bool animation_engine_tick(void);
```

Advances every active animation by one frame. Interpolates the property value and applies it to the target node. Returns `true` if any animation is still running (allowing the compositor to continue rendering). Called every frame in `compositor_frame()`.

### `animation_start()` (`animation_engine.c:41`)

```c
void animation_start(node_t *node, anim_prop_t prop,
                     void *custom_target, int start, int end, int frames);
```

| Parameter      | Meaning                                    |
|----------------|--------------------------------------------|
| `node`         | Target node (must not be NULL)             |
| `prop`         | Property to animate                        |
| `custom_target`| For `ANIM_PROP_COLOR`: pointer to the `uint32_t` in userdata. For others: `NULL` |
| `start`        | Initial value (or target-independent base) |
| `end`          | Final value                                |
| `frames`       | Duration in frames (must be > 0)           |

If an animation already exists for the same `node + prop` combination, it is overwritten. Otherwise, the first free slot is used. If no slot is available (all 64 active), the call is silently ignored.

### `animation_cancel_all()` (`animation_engine.c:74`)

```c
void animation_cancel_all(node_t *node);
```

Marks all animations targeting the given node as inactive. Called internally by `node_destroy()`.

---

## Interpolation

### Linear interpolation (`animation_engine.c:13`)

```c
static int interp(int start, int end, int t, int duration) {
    if (duration <= 0) return end;
    if (t >= duration) return end;
    return start + ((end - start) * t) / duration;
}
```

Pure linear interpolation. No easing curves. t = `current_frame`, duration = `duration_frames`.

### Color interpolation (`animation_engine.c:20`)

```c
static uint32_t interp_color(uint32_t start, uint32_t end, int t, int duration) {
    // Decompose start and end into A, R, G, B
    // interp each channel independently
    // Recombine into ARGB uint32_t
}
```

Channel-by-channel linear interpolation in ARGB format (`0xAARRGGBB`). Alpha is interpolated alongside color channels.

---

## Overwrite Semantics

When `animation_start()` is called, the engine first searches for an existing animation with the same `(node, prop)` pair:

```c
for (int i = 0; i < MAX_ANIMATIONS; i++) {
    if (s_animations[i].active &&
        s_animations[i].target == node &&
        s_animations[i].prop == prop) {
        slot = i;  // overwrite
        break;
    }
}
```

This means rapid hover/unhover on a button smoothly transitions from the current interpolated value rather than snapping. The overwritten animation's `start_val` is the **original requested start**, not the current interpolated value — this is a known limitation (see trade-offs).

---

## Color Animation Flow (Button Example)

From `src/kernel/gui/widgets/button.c:76`:

```
MOUSE_ENTER:
  animation_start(btn, COLOR, &d->current_bg_color,
                  d->current_bg_color, BUTTON_BG_HOVER, 15)
       │
       ▼
  Over 15 frames: button_data_t.current_bg_color interpolates
  from current color → BUTTON_BG_HOVER (0xFF475569)
       │
       ▼
  Each tick sets *(color_target) = interpolated ARGB
  Marks NODE_DIRTY_PAINT → button redraws with new bg color

MOUSE_LEAVE:
  animation_start(btn, COLOR, &d->current_bg_color,
                  d->current_bg_color, BUTTON_BG, 15)
       │
       ▼
  Interpolates back to BUTTON_BG (0xFF334155)

MOUSE_DOWN (while hovered):
  animation_start(btn, COLOR, &d->current_bg_color,
                  d->current_bg_color, BUTTON_BG_PRESS, 5)
       │
       ▼
  Faster animation (5 frames) to press color (0xFF1E293B)

MOUSE_UP:
  animation_start(btn, COLOR, &d->current_bg_color,
                  d->current_bg_color, target, 15)
       │
       ▼
  Hovered → to HOVER; not hovered → to BG
```

### Window appearance animation (`gui_main.c:122`)

```c
win->width = 0;   // start from zero
win->height = 0;
animation_start(win, ANIM_PROP_WIDTH, NULL, 0, 300, 30);
animation_start(win, ANIM_PROP_HEIGHT, NULL, 0, 200, 30);
```

Two animations run in parallel: width interpolates from 0→300 and height from 0→200 over 30 frames.

---

## Dependencies

| Module          | Header                  | Usage                       |
|-----------------|-------------------------|-----------------------------|
| Node            | `scene/node.h`          | Target, property access, dirty flags |
| String          | `string.h`              | `memset` for init           |
| KHeap           | `kheap.h`               | (indirect, via node_* functions) |

---

## Limitations & Trade-offs

- **Linear only**: No easing curves (ease-in, ease-out, bounce, elastic). All animations are linear interpolation.
- **Frame-based, not time-based**: Duration is in frames, not milliseconds. At 60 FPS, 30 frames ≈ 500ms, but this varies if the framerate drops.
- **Overwrite start value**: When an animation is overwritten, `start_val` is set to the new caller-provided start, not the current interpolated value. This can cause a visible snap if the caller doesn't pass the current property value.
- **No chaining**: Animations cannot be sequenced (e.g., "animate X, then when complete, animate Y"). The caller must check for completion via polling or timer.
- **Static array**: MAX_ANIMATIONS=64 is hardcoded. If all slots are exhausted, new animations are silently dropped.
- **No thread safety**: Animations are ticked from the compositor task; concurrent calls from other tasks are unsafe.
- **No animation completion callbacks**: No way to be notified when an animation finishes.
- **Fixed-point opacity**: `ANIM_PROP_OPACITY_FP` uses 16.16 fixed-point, which is then cast to float. The float path is noted as incomplete in the code.

---

## Performance

- **32 bytes per animation slot**: `6 fields + alignment = ~32 bytes per entry, 64 entries = ~2 KB total.
- **O(n) per tick**: Walks all 64 slots (active or not). Returns early for inactive slots.
- **One heap allocation**: None per animation — the static array is pre-allocated at compile time. No malloc/free overhead during runtime.
- **Divide in interpolation**: `(end - start) * t / duration` involves one integer division per animated property per frame. 64 active animations = 64 divisions/frame.

---

## Future Extensions

- **Easing functions**: Support for `ease_in`, `ease_out`, `ease_in_out`, `bounce`, `elastic` via function pointer table.
- **Time-based animation**: Duration in milliseconds with accumulation based on real elapsed time.
- **Completion callbacks**: `animation_start_with_callback(node, prop, ..., on_complete, userdata)`.
- **Chaining/sequencing**: `animation_start_chain(animation_t *chain[], count)`.
- **Dynamic array**: Replace static `MAX_ANIMATIONS` with a dynamically growing array.
- **Property groups**: Animate multiple properties simultaneously with a single call.
- **Reverse/pause/resume**: Control API for running animations.
- **Animation timeline**: Shared timeline for coordinated multi-node animations.

---

## Usage Examples

### Button hover animation (typical)

```c
// In button_on_event():
if (hovered) {
    animation_start(self, ANIM_PROP_COLOR,
                     &d->current_bg_color,
                     d->current_bg_color,
                     theme_engine_get_color(THEME_COLOR_BUTTON_BG_HOVER),
                     15);
}
```

### Window open animation

```c
node_t *win = window_node_create("win", 100, 100, 0, 0, "App");
node_add_child(root, win);
animation_start(win, ANIM_PROP_WIDTH, NULL, 0, 400, 30);
animation_start(win, ANIM_PROP_HEIGHT, NULL, 0, 300, 30);
```

### Slide-in from above

```c
animation_start(panel, ANIM_PROP_Y, NULL, -200, 50, 20);
```

### Fade in (opacity)

```c
node->opacity = 0.0f;
animation_start(node, ANIM_PROP_OPACITY_FP, NULL, 0, 65536, 20);
// 65536 = 1.0 in 16.16 fixed point
```