# Theme Engine

## Objective

Provide a centralized color palette and styling system for all LiwusOS GUI widgets. The theme engine defines named color slots that widgets query during drawing, enabling consistent visual styling and runtime theme changes without modifying widget code.

## Problems Solved

- **Hardcoded colors eliminated**: Widgets no longer embed color literals. All colors come from `theme_engine_get_color()`.
- **Runtime theming**: `theme_engine_set_color()` changes the palette at runtime. Future `FRAME_BEGIN` dispatch notifies widgets of theme changes.
- **Dark theme by default**: The default palette uses a modern dark slate/indigo scheme with glassmorphism transparency.
- **Alpha channel support**: Colors use full ARGB (0xAARRGGBB). Alpha < 0xFF enables glassmorphism effects (e.g., `0xCC1E293B` for window backgrounds with 80% opacity).
- **Single source of truth**: 24 color slots (`THEME_COLOR_MAX`) cover all current widget needs. New widgets extend the enum.

## Architecture

### Color Palette

```c
static uint32_t s_palette[THEME_COLOR_MAX];
```

The palette is a static array of `uint32_t` ARGB values initialized during `theme_engine_init()` with the default dark theme.

### Color ID Enumeration

```c
typedef enum {
    THEME_COLOR_BACKGROUND,         // 0  Very dark slate (canvas)
    THEME_COLOR_WINDOW_BG,          // 1  Slate-800 @ 80% opacity
    THEME_COLOR_WINDOW_TITLEBAR,    // 2  Slate-900 @ 93% opacity
    THEME_COLOR_WINDOW_BORDER,      // 3  Slate-600
    THEME_COLOR_TEXT_PRIMARY,       // 4  Slate-50 (white-ish)
    THEME_COLOR_TEXT_SECONDARY,     // 5  Slate-400
    THEME_COLOR_BUTTON_BG,          // 6  Slate-700
    THEME_COLOR_BUTTON_BG_HOVER,    // 7  Slate-600
    THEME_COLOR_BUTTON_BG_PRESS,    // 8  Slate-800
    THEME_COLOR_BUTTON_BORDER,      // 9  Slate-500
    THEME_COLOR_BUTTON_TEXT,        // 10 White
    THEME_COLOR_CLOSE_BTN,          // 11 Red-500
    // ... expandable to 24
    THEME_COLOR_MAX                 // = 12 (currently)
} theme_color_id_t;
```

### Default Palette Table

| ID | Name | ARGB Hex | Description |
|----|------|----------|-------------|
| 0 | `THEME_COLOR_BACKGROUND` | `0xFF0B1120` | Very dark slate for canvas |
| 1 | `THEME_COLOR_WINDOW_BG` | `0xCC1E293B` | Slate-800, 80% opacity (glassmorphism) |
| 2 | `THEME_COLOR_WINDOW_TITLEBAR` | `0xEE0F172A` | Slate-900, 93% opacity |
| 3 | `THEME_COLOR_WINDOW_BORDER` | `0xFF475569` | Slate-600 |
| 4 | `THEME_COLOR_TEXT_PRIMARY` | `0xFFF8FAFC` | Slate-50 |
| 5 | `THEME_COLOR_TEXT_SECONDARY` | `0xFF94A3B8` | Slate-400 |
| 6 | `THEME_COLOR_BUTTON_BG` | `0xFF334155` | Slate-700 |
| 7 | `THEME_COLOR_BUTTON_BG_HOVER` | `0xFF475569` | Slate-600 |
| 8 | `THEME_COLOR_BUTTON_BG_PRESS` | `0xFF1E293B` | Slate-800 |
| 9 | `THEME_COLOR_BUTTON_BORDER` | `0xFF64748B` | Slate-500 |
| 10 | `THEME_COLOR_BUTTON_TEXT` | `0xFFFFFFFF` | White |
| 11 | `THEME_COLOR_CLOSE_BTN` | `0xFFEF4444` | Red-500 |

### Color Usage by Widget

```
Compositor:
  draw_background()
    ├── fill backbuffer with THEME_COLOR_BACKGROUND
    └── dot grid uses THEME_COLOR_BUTTON_BG

Window:
  draw()
    ├── title bar: fill with THEME_COLOR_WINDOW_TITLEBAR
    ├── client area: fill with THEME_COLOR_WINDOW_BG
    ├── close dot: fill with THEME_COLOR_CLOSE_BTN
    ├── border: draw with THEME_COLOR_WINDOW_BORDER
    └── title text: draw with THEME_COLOR_TEXT_SECONDARY

Button:
  draw() / on_event()
    ├── background: ANIM_PROP_COLOR animating between
    │   THEME_COLOR_BUTTON_BG / BUTTON_BG_HOVER / BUTTON_BG_PRESS
    ├── border: draw with THEME_COLOR_BUTTON_BORDER
    └── text: draw with THEME_COLOR_BUTTON_TEXT

Panel:
  draw()
    └── bg_color: set by user (can be any value, not necessarily themed)
```

### Alpha Blending

The framebuffer renderer's `alpha_blend()` function handles per-pixel alpha compositing:

```c
static inline uint32_t alpha_blend(uint32_t bg, uint32_t fg) {
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 0xFF) return fg;         // fully opaque
    if (a == 0x00) return bg;         // fully transparent
    uint32_t inv = 255 - a;
    uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv) >> 8;
    uint32_t g = (((fg >>  8) & 0xFF) * a + ((bg >>  8) & 0xFF) * inv) >> 8;
    uint32_t b = (( fg        & 0xFF) * a + ( bg        & 0xFF) * inv) >> 8;
    return (0xFF000000) | (r << 16) | (g << 8) | b;
}
```

Window backgrounds with `0xCC` alpha (204/255 ≈ 80%) show the canvas dot grid through them. This glassmorphism effect is a visual signature of the LiwusOS desktop.

## APIs

### Public

```c
// Initialize the default palette (called from gui_init())
void theme_engine_init(void);

// Get a color from the current theme.
// Returns 0xFFFFFFFF (white) if id is out of range.
uint32_t theme_engine_get_color(theme_color_id_t id);

// Override a theme color at runtime.
// Use for custom palettes, per-user themes, or accent color changes.
void theme_engine_set_color(theme_color_id_t id, uint32_t color);
```

## Dependencies

- `stdint.h` — `uint32_t` for ARGB colors.
- No heap allocation (static array).
- No dependencies on other GUI subsystems.

## Limitations / Trade-offs

| Limitation | Rationale |
|------------|-----------|
| Flat color palette | No gradients, no border radius, no box shadows. All visual richness comes from alpha blending and color choices. |
| No CSS-like theming | Colors are set programmatically. No theme file format yet. |
| No per-widget overrides | `theme_engine_set_color()` changes the global palette. All widgets of the same type look identical. |
| Fixed 24-color slot limit | `THEME_COLOR_MAX = 12` currently, with room for 12 more. Exceeding 24 requires changing the enum and recompiling. |
| No theme hot-swap notification | Widgets do not currently listen for theme change events. Future dispatch of `GUI_EVENT_THEME_CHANGED` solves this. |
| No opacity on widget draw calls | Widgets pass their background color directly to `fill_rect`. The alpha channel is respected by `alpha_blend()` in `fb_fill_rect`. |

## Performance / Memory Optimizations

- **Static array**: `s_palette[THEME_COLOR_MAX]` is 24 × 4 bytes = 96 bytes in BSS. Zero heap, zero init cost.
- **Inlined getter**: `theme_engine_get_color` is a single array access + bounds check. Small enough to inline.
- **Widgets cache**: Button stores `current_bg_color` in its `button_data_t` to avoid theme queries every frame. Animation engine interpolates between theme colors.

## Future Extensions

| Feature | Approach |
|---------|----------|
| CSS-like theme files | Parse a TOML/JSON file of `[theme.button] bg = "#334155"`. Load at boot via asset manager. |
| Hot reload | Subscribe to file watcher; on change, re-parse theme file and call `theme_engine_set_color()` for each slot, then `compositor_invalidate_full()`. |
| Per-widget overrides | Add `uint32_t *override_colors` field to `node_t`. If non-null, widget queries override first, fall back to theme engine. |
| Per-window themes | Associate a `theme_id` with each window node. Widgets check their window ancestor's theme before using global. |
| Accent color | Add `THEME_COLOR_ACCENT` used for selection highlights, focus rings, and links. |
| Dark/Light mode toggle | Two preset palettes (`s_dark_palette`, `s_light_palette`). `theme_engine_set_mode(THEME_MODE_LIGHT)` copies the light preset. |
| Gradient support | Extend `theme_engine_get_gradient(id)` returning two colors for linear gradient fills. Requires renderer support for gradients. |

## Usage Examples

```c
// Widget draw function queries theme colors:
static void button_draw(node_t *self, gui_renderer_t *r) {
    button_data_t *d = (button_data_t *)self->userdata;

    // Background (may be animated between theme colors)
    renderer_fill_rect(r, self->screen_bounds, d->current_bg_color);

    // Border — direct theme query
    renderer_draw_rect(r, self->screen_bounds,
                       theme_engine_get_color(THEME_COLOR_BUTTON_BORDER), 1);

    // Text — direct theme query
    // ... draw text with theme_engine_get_color(THEME_COLOR_BUTTON_TEXT)
}

// Runtime theme change (e.g., from a settings dialog):
theme_engine_set_color(THEME_COLOR_BACKGROUND, 0xFF1A1A2E);
theme_engine_set_color(THEME_COLOR_WINDOW_BG, 0xCC16213E);
compositor_invalidate_full(g_compositor);  // trigger full repaint
```
