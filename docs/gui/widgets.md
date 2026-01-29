# LiwusOS GUI — Widget System

## Objective

Provide a complete widget framework built on the scene graph where every widget is a `node_t`. Widget behavior is specialized through `node_vtable_t`, and mutable state is stored in `node->userdata`. This document covers the architecture, all existing widget types, and the procedure for creating new widget types.

---

## Problems Solved

- **No hardcoded UI**: Widgets are nodes in a tree, enabling dynamic creation, destruction, re-parenting, and z-ordering.
- **Polymorphism without C++**: `node_vtable_t` gives each node subtype its own draw, event, layout, and destroy handlers.
- **State isolation**: Each widget stores its private data in a `kmalloc`'d struct hung off `node->userdata`.
- **Separation of concerns**: Drawing uses the renderer; events use the event bus; layout uses the layout engine. Widgets only implement their visual/behavioral specifics.

---

## Architecture

Every widget IS a `node_t`. The base node provides spatial data, hierarchy, dirty flags, and a transform chain. The vtable pointer (`node->vtable`) dispatches to the subtype's implementation:

```
    ┌───────────────────────────────────────────────────┐
    │                    node_t                         │
    ├───────────────────────────────────────────────────┤
    │  id, type, name                                   │
    │  local_x, local_y, width, height                  │
    │  world_transform, screen_bounds                   │
    │  parent, children[], child_count                   │
    │  prev_sibling, next_sibling                       │
    │  visible, interactive, dirty, opacity, z_order    │
    │  layout_type, layout_align                        │
    │  margin[4], padding[4], flex_weight               │
    ├───────────────────────────────────────────────────┤
    │  vtable  ──►  { draw, on_event, layout, destroy } │
    │  userdata ──►  { widget-specific state }          │
    └───────────────────────────────────────────────────┘
```

### Virtual Dispatch Table

Defined in `src/kernel/gui/scene/node.h:87`:

```c
typedef struct {
    void  (*draw)(node_t *self, struct gui_renderer *r);
    bool  (*on_event)(node_t *self, const gui_event_t *event);
    void  (*layout)(node_t *self);
    void  (*destroy)(node_t *self);
} node_vtable_t;
```

All function pointers are optional — the system checks for NULL before calling.

---

## Widget Categories

| Category   | Description                                  | Types                        |
|------------|----------------------------------------------|------------------------------|
| **Shell**  | Top-level window frame                       | `NODE_WINDOW`                |
| Container  | Groups children visually                     | `NODE_PANEL`, `NODE_GROUP`   |
| Control    | User-interactive element                     | `NODE_BUTTON`                |
| Display    | Read-only visual output                      | `NODE_LABEL`, `NODE_IMAGE`   |
| App        | Hosts a terminal/application surface         | `NODE_TERMINAL`              |
| Overlay    | Floating layer above all                     | `NODE_OVERLAY`, `NODE_DEBUG` |
| Root       | Scene graph root                             | `NODE_CANVAS`                |

---

## Widget Hierarchy (Typical Scene)

```
canvas (NODE_CANVAS)
│
└─ group (NODE_GROUP)
   │
   ├─ window "terminal" (NODE_WINDOW)
   │  │  layout_type = LAYOUT_VBOX
   │  │  padding = [30, 10, 10, 10]
   │  │
   │  ├─ label "title" (NODE_LABEL)
   │  │    layout_align = ALIGN_CENTER
   │  │
   │  ├─ button "close" (NODE_BUTTON)
   │  │    layout_align = ALIGN_CENTER
   │  │    on_click → callback
   │  │
   │  └─ panel "content" (NODE_PANEL)
   │       flex_weight = 1
   │       layout_align = ALIGN_STRETCH
   │       ├─ terminal (NODE_TERMINAL)
   │       └─ ...
   │
   └─ overlay (NODE_OVERLAY)
```

---

## widget_data Pattern

Every widget subtype follows this pattern:

1. **Define a struct** for private state.
2. **`kmalloc` it** in the constructor, zero it with `memset`.
3. **Store pointer** in `node->userdata`.
4. **Cast back** at the top of every vtable method.

### Example: Button (`src/kernel/gui/widgets/button.c:12`)

```c
typedef struct {
    char              *text;
    bool               hovered;
    bool               pressed;
    button_click_cb_t  on_click;
    void              *click_ud;
    const glyph_t     *font;
    uint32_t           current_bg_color;
} button_data_t;

static void button_draw(node_t *self, gui_renderer *r) {
    button_data_t *d = (button_data_t *)self->userdata;
    // ... draw using d->text, d->current_bg_color, etc.
}
```

---

## Drawing Convention

All widgets compute screen-space bounds from the node's `world_transform` and the camera, then render:

```c
static void my_widget_draw(node_t *self, gui_renderer *r) {
    extern compositor_t *g_compositor;
    camera_t *cam = g_compositor->camera;

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int screen_x = camera_world_to_screen_x(cam, pt.x);
    int screen_y = camera_world_to_screen_y(cam, pt.y);
    int screen_w = camera_scale(cam, self->width);
    int screen_h = camera_scale(cam, self->height);

    self->screen_bounds = rect_make(screen_x, screen_y, screen_w, screen_h);

    renderer_fill_rect(r, self->screen_bounds, color);
    renderer_draw_rect(r, self->screen_bounds, border_color, 1);
    // draw glyphs...
}
```

This gives correct pan/zoom behavior. The stored `screen_bounds` is also reused for hit-testing.

---

## Event Handling Convention

Widgets check `screen_bounds` containment and handle mouse/key events:

```c
static bool my_widget_on_event(node_t *self, const gui_event_t *e) {
    my_data_t *d = (my_data_t *)self->userdata;

    if (e->type == GUI_EVENT_MOUSE_MOVE) {
        bool inside = rect_contains_point(self->screen_bounds, e->mouse.x, e->mouse.y);
        // update hover state, animate color...
        return true; // if consumed
    }
    if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        if (rect_contains_point(self->screen_bounds, e->mouse.x, e->mouse.y)) {
            // handle click
            return true;
        }
    }
    return false; // propagate
}
```

Returning `true` stops propagation through the scene graph.

---

## How to Create a New Widget Type

### Step 1. Add a `node_type_t` enum value

In `src/kernel/gui/scene/node.h:46`:

```c
typedef enum {
    NODE_GENERIC  = 0,
    NODE_CANVAS   = 1,
    NODE_GROUP    = 2,
    NODE_WINDOW   = 3,
    NODE_PANEL    = 4,
    NODE_BUTTON   = 5,
    NODE_LABEL    = 6,
    NODE_IMAGE    = 7,
    NODE_TERMINAL = 8,
    NODE_OVERLAY  = 9,
    NODE_DEBUG    = 10,
    // Add new type here — never renumber existing types
} node_type_t;
```

### Step 2. Write vtable functions

Create `mywidget.c` with:

```c
typedef struct {
    // widget state
} mywidget_data_t;

static void mywidget_draw(node_t *self, gui_renderer *r) { ... }
static bool mywidget_on_event(node_t *self, const gui_event_t *e) { ... }
static void mywidget_layout(node_t *self) { ... }
static void mywidget_destroy(node_t *self) { ... }

static const node_vtable_t mywidget_vtable = {
    .draw     = mywidget_draw,
    .on_event = mywidget_on_event,
    .layout   = mywidget_layout,
    .destroy  = mywidget_destroy,
};
```

### Step 3. Write constructor

```c
node_t *mywidget_create(const char *name, int x, int y, int w, int h) {
    node_t *n = node_create(NODE_MYWIDGET, name);
    if (!n) return NULL;

    mywidget_data_t *d = kmalloc(sizeof(mywidget_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(mywidget_data_t));

    // Initialize defaults
    n->userdata = d;
    n->vtable   = &mywidget_vtable;
    n->local_x  = x;
    n->local_y  = y;
    n->width   = w;
    n->height  = h;
    n->interactive = true; // if it handles events

    return n;
}
```

### Step 4. Create header and expose API

```c
// mywidget.h
#ifndef GUI_MYWIDGET_H
#define GUI_MYWIDGET_H
#include "../scene/node.h"
node_t *mywidget_create(const char *name, int x, int y, int w, int h);
void mywidget_set_something(node_t *widget, int value);
#endif
```

### Step 5. Destroy handler

Always free `userdata` and any sub-allocations:

```c
static void mywidget_destroy(node_t *self) {
    if (self->userdata) {
        mywidget_data_t *d = (mywidget_data_t *)self->userdata;
        if (d->some_string) kfree(d->some_string);
        kfree(d);
        self->userdata = NULL;
    }
}
```

---

## Existing Widget Constructors

All constructors live under `src/kernel/gui/widgets/`.

### Window (`window_node.c:130`)

```c
node_t *window_node_create(const char *name, int x, int y, int w, int h, const char *title);
```
- Type: `NODE_WINDOW`
- `userdata`: `window_node_data_t` — `{ char *title, const glyph_t *font, int process_id }`
- Draws: title bar with close dot, content background, border
- Events: close button click → sends SIGKILL to `process_id` and hides window
- Setters:
  ```c
  void window_node_set_title(node_t *win, const char *title);
  void window_node_set_pid(node_t *win, int pid);
  ```

### Button | `button.c:114`

```c
node_t *button_create(const char *name, int x, int y, int w, int h, const char *text);
```
- Type: `NODE_BUTTON`
- `interactive`: true
- `userdata`: `button_data_t` — `{char *text, bool hovered, bool pressed, button_click_cb_t on_click, void *click_ud, uint32_t current_bg_color}`
- Draws: filled rect with animated background color, centered text, 1px border
- Events: hover → animate color to HOVER, press → animate to PRESS, release → fire callback, animate to HOVER/BG
- Setters:
  ```c
  void button_set_text(node_t *button, const char *text);
  void button_set_on_click(node_t *button, button_click_cb_t cb, void *userdata);
  ```
- Callback typedef:
  ```c
  typedef void (*button_click_cb_t)(node_t *button, void *userdata);
  ```

### Label | `label.c:68`

```c
node_t *label_create(const char *name, int x, int y, const char *text, uint32_t color);
```
- Type: `NODE_LABEL`
- `userdata`: `label_data_t` — `{char *text, uint32_t color, const glyph_t *font}`
- Width computed automatically as `strlen(text) * 8`; height fixed at 16
- Setters:
  ```c
  void label_set_text(node_t *label, const char *text);
  void label_set_color(node_t *label, uint32_t color);
  ```
- `on_event`: NULL (no interaction)

### Panel | `panel.c:76`

```c
node_t *panel_create(const char *name, int x, int y, int w, int h, uint32_t bg_color);
```
- Type: `NODE_PANEL`
- `userdata`: `panel_data_t` — `{uint32_t bg_color, uint32_t border_color, int border_thickness}`
- Draws: filled background (if non-zero alpha), optional border
- Setters:
  ```c
  void panel_set_bg_color(node_t *panel, uint32_t color);
  void panel_set_border(node_t *panel, uint32_t color, int thickness);
  ```

### Image (planned) | `NODE_IMAGE`

```c
// Not yet implemented
node_t *image_create(const char *name, int x, int y, int w, int h, image_handle_t img);
```

### Terminal | `NODE_TERMINAL`
- Implemented separately as an application surface widget hosting a terminal emulator. Shares the same vtable pattern.

### Overlay | `NODE_OVERLAY`
- Used for modal dialogs, dropdowns, tooltips. No dedicated constructor file yet — created via `node_create(NODE_OVERLAY, name)` with a custom vtable.

---

## Dependencies

| Module         | Header                          | Used by widgets for                   |
|----------------|---------------------------------|---------------------------------------|
| Node           | `scene/node.h`                  | Base type, hierarchy, transforms      |
| Renderer       | `render/renderer.h`             | All draw functions                    |
| Compositor     | `render/compositor.h`           | Camera access, `g_compositor` global  |
| Theme Engine   | `core/theme_engine.h`            | Color palette constants               |
| Animation      | `core/animation_engine.h`        | Button hover/press animations         |
| Layout Engine  | `layout/layout_engine.h`         | `layout_engine_compute()`             |
| Event Bus      | `core/event_bus.h`              | Event types and convenience post      |
| Asset Manager  | `assets/asset_manager.h`        | Font loading                          |
| Transform      | `math/transform.h`               | `transform_apply`, `world_transform`  |
| Rect           | `math/rect.h`                    | `screen_bounds`, containment tests    |
| KHeap          | `kheap.h`                       | `kmalloc/kfree` for userdata          |

---

## Limitations & Trade-offs

- **No data binding**: Widgets must be updated manually via setters; no reactive framework.
- **Single-threaded**: All widget operations occur on the compositor task. No thread-safe mutation API.
- **No CSS/styling language**: Colors, borders, and layout properties are set programmatically.
- **No accessibility**: No screen reader or keyboard navigation beyond basic Tab traversal (stub).
- **Auto-size limited**: Labels auto-size width; buttons and panels require explicit dimensions.
- **No clipping**: Widgets may draw outside their bounds (child overflow not clipped).

---

## Performance & Memory

- **Memory per widget**: `sizeof(node_t)` (~184 bytes) + userdata struct (varies: button ~48, label ~28, panel ~16, window ~32).
- **Draw overhead**: Each widget calls `transform_apply` + `camera_*` per frame. Full redraw of all nodes every frame (dirty-rect optimization planned for Phase 4).
- **Userdata allocation**: Single `kmalloc` per constructor. Sub-allocations for strings (title, text) add fragmentation.
- **Vtable storage**: One `const node_vtable_t` per widget type — shared across all instances, no per-widget cost.

---

## Future Extensions

- **`image_create()`**: Constructor for `NODE_IMAGE` with bitmap/pixel buffer display.
- **`scroll_container`**: Clipping scrollable area with scrollbars.
- **`text_input`**: Editable text field with cursor and selection.
- **`combo_box`**, **`slider`**, **`checkbox`**: Standard form controls.
- **`widget_data` validation**: Debug mode to verify correct `node->type` vs `userdata` struct type.
- **Widget reference counting**: Smart pointer pattern for safe async destruction.

---

## Usage Example

```c
#include "widgets/window_node.h"
#include "widgets/button.h"
#include "widgets/label.h"
#include "widgets/panel.h"

node_t *win = window_node_create("main", 50, 50, 400, 300, "My App");
win->layout_type = LAYOUT_VBOX;
win->padding[0] = 30; win->padding[1] = 10;
win->padding[2] = 10; win->padding[3] = 10;

node_t *lbl = label_create("greeting", 0, 0, "Hello!", 0xFFF8FAFC);
lbl->layout_align = ALIGN_CENTER;
lbl->margin[2] = 8;

node_t *btn = button_create("submit", 0, 0, 120, 36, "Submit");
btn->layout_align = ALIGN_CENTER;
button_set_on_click(btn, my_callback, my_data);

node_t *pnl = panel_create("content", 0, 0, 380, 200, 0x881E293B);
pnl->flex_weight = 1;
pnl->layout_align = ALIGN_STRETCH;
panel_set_border(pnl, 0xFF475569, 1);

node_add_child(win, lbl);
node_add_child(win, btn);
node_add_child(win, pnl);
node_add_child(root, win);

layout_engine_compute(win);
```