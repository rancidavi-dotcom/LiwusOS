# Accessibility — LiwusOS GUI (Future Architecture)

## Objective

Document the planned accessibility infrastructure for the LiwusOS GUI. Covers screen reader support, keyboard navigation, high-contrast themes, focus indicators, and the semantic accessibility tree derived from the scene graph.

---

## Problems Solved

- **No screen reader support:** Currently there is no mechanism for visually impaired users to navigate or understand the GUI
- **Keyboard navigation:** Basic Tab-based focus traversal exists as a stub (`focus_manager_focus_next` is empty)
- **No high-contrast themes:** Theme colors are hard-coded with a single dark palette (slate-900 based)
- **No semantic properties:** Nodes have no role, label, or description attributes for assistive technology
- **No focus indicators:** Focus state changes do not produce any visual feedback

---

## Current Accessibility State

### What Exists

- `focus_manager_t` module — manages keyboard focus, posts `GUI_EVENT_WIN_FOCUS`/`GUI_EVENT_WIN_BLUR`
- `focus_manager_set_focus()` — marks focused/blurred nodes dirty for repaint
- `node_t.interactive` flag — marks nodes that receive input events
- `layout_engine` — Tab-order is determined by child array order

### What Is Missing

- `focus_manager_focus_next()` — declared but **implementation is empty** (stub at `focus_manager.c:98`)
- No focus ring/indicator rendering in any widget
- No `aria-*` equivalent properties on `node_t`
- No screen reader infrastructure
- No high-contrast theme variant

---

## Accessibility Tree (Planned)

### Derivation from Scene Graph

The accessibility tree is a parallel tree derived from the scene graph, containing only nodes relevant to assistive technology. It is rebuilt when the scene graph structure changes.

```
Scene Graph                    Accessibility Tree
────────────                    ──────────────────
canvas_root                    ─── (role=application)
  └─ demo_win (NODE_WINDOW)       └─ window "App Demo GUI"
       ├─ title (NODE_LABEL)           ├─ label "Hello, World!"
       ├─ btn (NODE_BUTTON)           └─ button "Click Me"
       └─ panel (NODE_PANEL)              └─ group "Settings"
            ├─ checkbox
            └─ slider
```

### Semantic Node Properties (Planned)

Additional fields to be added to `node_t`:

```c
typedef enum {
    ACCESS_ROLE_NONE = 0,
    ACCESS_ROLE_BUTTON,
    ACCESS_ROLE_LABEL,
    ACCESS_ROLE_WINDOW,
    ACCESS_ROLE_PANEL,
    ACCESS_ROLE_IMAGE,
    ACCESS_ROLE_SCROLLBAR,
    ACCESS_ROLE_SLIDER,
    ACCESS_ROLE_CHECKBOX,
    ACCESS_ROLE_RADIO,
    ACCESS_ROLE_TEXTBOX,
    ACCESS_ROLE_TREEITEM,
    ACCESS_ROLE_LISTITEM,
    ACCESS_ROLE_TAB,
    ACCESS_ROLE_MENU,
    ACCESS_ROLE_MENUITEM,
} access_role_t;

/* Extended node properties (future) */
struct node {
    /* ... existing fields ... */
    
    /* Accessibility */
    access_role_t    access_role;
    char             access_label[64];    /* human-readable name */
    char             access_desc[128];    /* extended description */
    bool             access_focused;      /* rendered with focus ring */
    int              access_value;        /* for sliders, scrollbars */
    int              access_min;          /* range minimum */
    int              access_max;          /* range maximum */
    int              access_step;         /* increment step */
};
```

---

## Screen Reader Support (Planned)

### Architecture

```
GUI Scene Graph
     │
     ▼
Accessibility Tree (filtered, annotated)
     │
     ▼
Screen Reader Engine (kernel task or user-space service)
     │
     ├── Text-to-Speech (future: audio subsystem + TTS engine)
     │     └── Speak: "Button: Click Me. Press Space to activate."
     │
     └── Serial/Braille output (for early development)
           └── Serial: "[BTN] Click Me   [LBL] Hello, World!"
```

### TTS Integration

Currently no audio subsystem exists. The screen reader would initially output to the serial console for development, with TTS integration planned after the audio driver stack is complete.

### Speech Output Format

```c
typedef struct {
    const char *node_name;
    access_role_t role;
    const char *access_label;
    int32_t value;
    bool focused;
} access_speech_event_t;

/* Composed string format: */
"<role>: <label> [value: <n>] [focused]"
```

### Examples

| Node | Speech Output |
|---|---|
| Button "Click Me" | "Button: Click Me. Press Space to activate." |
| Slider volume 75% | "Slider: Volume. Value 75 percent. Use arrows to adjust." |
| Window "Settings" | "Window: Settings. Contains 3 items." |
| Focus change to button | "Focus: Button 'OK'." |

---

## Keyboard Navigation (Planned)

### Current State

```c
// focus_manager.c — keyboard handler (lines 35-39)
if (event->type == GUI_EVENT_KEY_DOWN && event->key.keycode == 0x0F /* Tab */) {
    focus_manager_focus_next(fm);
    return;
}
```

`focus_manager_focus_next()` is a **stub** — it does nothing.

### Planned Navigation Model

| Key | Action |
|---|---|
| Tab | Move focus to next interactive node (depth-first, in-order) |
| Shift+Tab | Move focus to previous interactive node |
| Enter / Space | Activate focused node (click button, toggle checkbox) |
| Arrow keys | Navigate within container (list, tree view, slider) |
| Esc | Close menu, cancel drag, unfocus |
| F6 | Cycle between window panes |
| Alt+F4 | Close active window |

### Tab Traversal Order

```
Windows are traversed in z-order (topmost first).
Within a window: depth-first, pre-order traversal of interactive nodes.
Only nodes with interactive=true AND visible=true are included.
```

### Focus Ring Visual (Planned)

```c
// Every widget's draw() would include:
if (self->access_focused) {
    renderer_draw_rect(r, self->screen_bounds, FOCUS_RING_COLOR, FOCUS_RING_THICKNESS);
    // Additional focus indicator: outer glow or bright border
}
```

Focus ring color: `0xFFFFD700` (gold, high contrast against any background).

---

## High Contrast Themes (Planned)

### Theme Variant System

```c
typedef enum {
    THEME_VARIANT_DARK,          /* current: slate-based dark theme */
    THEME_VARIANT_HIGH_CONTRAST, /* high contrast: black/white/yellow */
    THEME_VARIANT_LIGHT,         /* light mode (future) */
} theme_variant_t;

void theme_engine_set_variant(theme_variant_t variant);
```

### High-Contrast Palette

```c
// High contrast variant
s_palette[THEME_COLOR_BACKGROUND]      = 0xFF000000;  /* black */
s_palette[THEME_COLOR_WINDOW_BG]       = 0xFF000000;  /* black */
s_palette[THEME_COLOR_WINDOW_TITLEBAR] = 0xFF000000;  /* black */
s_palette[THEME_COLOR_WINDOW_BORDER]   = 0xFFFFFFFF;  /* white */
s_palette[THEME_COLOR_TEXT_PRIMARY]    = 0xFFFFFFFF;  /* white */
s_palette[THEME_COLOR_TEXT_SECONDARY]  = 0xFFFFFF00;  /* yellow */
s_palette[THEME_COLOR_BUTTON_BG]       = 0xFF000000;  /* black */
s_palette[THEME_COLOR_BUTTON_BG_HOVER] = 0xFF333333;  /* dark grey */
s_palette[THEME_COLOR_BUTTON_BORDER]   = 0xFFFFFFFF;  /* white */
s_palette[THEME_COLOR_BUTTON_TEXT]     = 0xFFFFFFFF;  /* white */
s_palette[THEME_COLOR_CLOSE_BTN]       = 0xFFFF0000;  /* red */
```

---

## Focus Indicator System (Planned)

### Focus Ring Rendering

```c
#define FOCUS_RING_COLOR       0xFFFFD700  /* gold */
#define FOCUS_RING_THICKNESS   2
#define FOCUS_RING_GAP         2           /* px between node border and ring */

void renderer_draw_focus_ring(gui_renderer_t *r, gui_rect_t node_bounds) {
    gui_rect_t ring = rect_inflate(node_bounds, FOCUS_RING_GAP);
    renderer_draw_rect(r, ring, FOCUS_RING_COLOR, FOCUS_RING_THICKNESS);
}
```

### Focus States

```c
typedef enum {
    FOCUS_STATE_NONE,      /* no focus, not interactive */
    FOCUS_STATE_READY,     /* focusable but not focused — ring on Tab landing */
    FOCUS_STATE_ACTIVE,    /* currently focused — show ring */
    FOCUS_STATE_PRESSED,   /* space/enter pressed — visual press feedback */
} focus_state_t;
```

---

## Accessibility Event Flow (Planned)

```
User presses Tab
  → input_manager_poll()
    → event_bus_post(GUI_EVENT_KEY_DOWN, scancode=0x0F)
      → focus_manager handler receives event
        → focus_manager_focus_next()
          → node_hit_test() finds next interactive node
          → focus_manager_set_focus(next_node)
            → posts GUI_EVENT_WIN_BLUR (old) + GUI_EVENT_WIN_FOCUS (new)
            → marks nodes dirty for paint
            → accessibility tree notifier:
              → generates access_speech_event_t
              → sends to screen reader engine
                → serial output: "[FOCUS] Button: Close"
                → (future) TTS output
```

---

## Accessibility Policy

```
Rules:
  1. Every interactive node MUST have an access_role != NONE
  2. Every non-label interactive node MUST have an access_label
  3. The root node MUST have role=application
  4. Screen reader is opt-out (accessible by default)
  5. Keyboard navigation MUST work without a mouse
  6. All functionality MUST be reachable via Tab + Enter
```

---

## Dependencies

- **Focus manager:** Must implement `focus_manager_focus_next()` (currently a stub)
- **Event bus:** Already delivers keyboard events — focus system routes them
- **Widget vtable:** All widgets must implement focus ring drawing in their `draw()` method
- **Theme engine:** High-contrast variant requires enumerating all theme colors and providing alternate values
- **Serial output:** Screen reader output goes to serial during development (`serial_print`)

---

## Limitations & Trade-offs

| Concern | Status / Mitigation |
|---|---|
| **No audio subsystem** | Screen reader outputs to serial initially; TTS requires audio driver + TTS engine |
| **Focus_next stub** | Must be implemented — simple tree walker (O(n) per traversal, n = node count) |
| **No access_role on nodes** | Adding to `node_t` increases per-node memory by ~100 bytes |
| **Widget draw() changes** | Every widget must render focus ring — ~10 lines per draw() function |
| **Canvas zoom and focus** | Focus ring must scale with camera zoom — trivial via camera_scale() |
| **Performance** | Access tree update on every scene graph change — O(n) incremental update |

---

## Future Extensions

| Extension | Description | Phase |
|---|---|---|
| Full keyboard navigation | Tab, arrows, shortcuts for all widgets | Phase 4 |
| Screen reader engine (serial) | Access tree to serial text output | Phase 5 |
| High-contrast theme variant | Runtime switchable theme | Phase 4 |
| Focus indicators in all widgets | Gold ring on focused nodes | Phase 4 |
| Screen reader (audio/TTS) | Requires kernel audio driver | Post-Phase 6 |
| Braille display support | Serial output in Braille format | Post-Phase 6 |
| Gesture-based navigation (pointers) | For head-tracking or eye-gaze | Post-Phase 6 |
| Color-blind modes | Deutan/protan/tritan palette adjustments | Phase 5 |