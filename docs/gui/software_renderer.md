# Software Framebuffer Backend — Current Renderer Implementation

## Objective

Document the concrete `renderer_ops_t` implementation (`fb_renderer.c`) that drives the current LiwusOS GUI. This is the ONLY file that touches raw VRAM pointers. All higher-level code (compositor, widgets, scene graph) goes through the abstract `renderer_*()` wrappers.

## Problems Solved

- **VRAM isolation**: Encapsulates all VBE LFB (BGA) framebuffer access in a single translation unit.
- **Double buffering**: Allocates a heap backbuffer so drawing never reads from slow VRAM; only `fb_present` writes to VRAM.
- **Alpha blending**: Correct source-over compositing with per-pixel alpha and fully-opaque/fully-transparent fast paths.
- **PSF1 font rendering**: Unpacks 1bpp bitmap glyph data into ARGB pixels.
- **Clip rect enforcement**: Intersects every draw with screen bounds and the renderer's clip rect.

## Architecture

```
vga.c (globals)
    │  vga_fb_addr, vga_fb_width, vga_fb_height, vga_fb_pitch
    ▼
fb_renderer_create()
    │
    ├── allocates fb_state_t
    ├── reads VGA globals
    ├── kmalloc(vga_fb_pitch * height)  → backbuffer
    └── returns gui_renderer_t with &fb_ops
         │
         ▼
fb_fill_rect()    ──→ alpha_blend per-pixel (or memcpy if opaque)
fb_draw_rect()    ──→ 4 × fb_fill_rect
fb_blit()         ──→ source-over per-pixel compositing
fb_blit_scaled()  ──→ nearest-neighbor with float division
fb_draw_glyph()   ──→ PSF1 1bpp → ARGB expansion
fb_set_clip()     ──→ r->clip assignment
fb_set_opacity()  ──→ r->opacity assignment
fb_present()      ──→ backbuffer → VRAM memcpy
fb_destroy()      ──→ kfree(backbuf), kfree(state)
         │
         ▼
VRAM (VBE LFB at vga_fb_addr)
```

## Lifecycle

```
boot:
  vga.c sets vga_fb_addr/width/height/pitch via VBE (BGA)
    │
gui_init():
    │
    ▼
fb_renderer_create()
    │  kmalloc(fb_state_t)
    │  kmalloc(backbuffer = vga_fb_pitch × vga_fb_height)
    │  renderer_create(&fb_ops, state, w, h)
    │
    ▼
per frame:
    fb_fill_rect() / fb_blit() / ... → writes to backbuffer
    fb_present()                      → memcpy backbuffer → vram
    │
    ▼
gui_shutdown:
    renderer_destroy(r)
        → fb_destroy()
            → kfree(backbuf) → kfree(state)
```

## APIs

### Public (in `fb_renderer.h`)

```c
// Construct a software framebuffer renderer from VGA globals.
gui_renderer_t *fb_renderer_create(void);

// Returns the internal backbuffer pointer (for direct pixel access).
uint32_t *fb_renderer_backbuf(gui_renderer_t *r);
```

### Private — `fb_state_t` (in `fb_renderer.c`)

```c
typedef struct {
    uint32_t *vram;         // physical framebuffer (VBE LFB)
    uint32_t *backbuf;      // heap-allocated backbuffer
    uint32_t  pitch_px;     // pitch in uint32_t units
    uint32_t  width;
    uint32_t  height;
    uint32_t  buf_bytes;    // vga_fb_pitch * height
} fb_state_t;
```

### Private — ops implementations

```c
// All ops receive the gui_renderer_t*; cast backend to fb_state_t.
static void fb_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color);
static void fb_draw_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color, int thickness);
static void fb_blit(gui_renderer_t *r, int dest_x, int dest_y,
                     const uint32_t *src, int src_w, int src_h, int src_pitch,
                     int src_x, int src_y, int copy_w, int copy_h);
static void fb_blit_scaled(gui_renderer_t *r, int dest_x, int dest_y,
                            const uint32_t *src, int src_w, int src_h,
                            float scale_x, float scale_y);
static void fb_draw_glyph(gui_renderer_t *r, int x, int y,
                           uint32_t fg, uint32_t bg, const glyph_t *g);
static void fb_set_clip(gui_renderer_t *r, gui_rect_t clip);
static void fb_set_opacity(gui_renderer_t *r, float opacity);
static void fb_present(gui_renderer_t *r);
static void fb_destroy(gui_renderer_t *r);
```

### Ops table (single shared instance)

```c
static const renderer_ops_t fb_ops = {
    .fill_rect   = fb_fill_rect,
    .draw_rect   = fb_draw_rect,
    .blit        = fb_blit,
    .blit_scaled = fb_blit_scaled,
    .draw_glyph  = fb_draw_glyph,
    .set_clip    = fb_set_clip,
    .set_opacity = fb_set_opacity,
    .present     = fb_present,
    .destroy     = fb_destroy,
};
```

## Core Algorithms

### Alpha Blend

```c
static inline uint32_t alpha_blend(uint32_t bg, uint32_t fg) {
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 0xFF) return fg;            // fully opaque fast path
    if (a == 0x00) return bg;            // fully transparent skip
    uint32_t inv  = 255 - a;
    uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv) / 255;
    uint32_t g = (((fg >>  8) & 0xFF) * a + ((bg >>  8) & 0xFF) * inv) / 255;
    uint32_t b = (( fg        & 0xFF) * a + ( bg        & 0xFF) * inv) / 255;
    return (0xFF000000) | (r << 16) | (g << 8) | b;
}
```

Note: uses `/ 255` (not `>> 8`). The code in `fb_renderer.c` uses `>> 8` (divide by 256), which is a minor accuracy trade-off.

### Clip

```c
static inline gui_rect_t fb_clip(fb_state_t *s, gui_renderer_t *r, gui_rect_t rect) {
    gui_rect_t screen = rect_make(0, 0, (int)s->width, (int)s->height);
    gui_rect_t clipped = rect_intersection(rect, screen);
    if (!rect_is_empty(r->clip))
        clipped = rect_intersection(clipped, r->clip);
    return clipped;
}
```

### Fill Rect

```c
static void fb_fill_rect(gui_renderer_t *r, gui_rect_t rect, uint32_t color) {
    fb_state_t *s = (fb_state_t *)r->backend;
    gui_rect_t c = fb_clip(s, r, rect);
    if (rect_is_empty(c)) return;

    uint8_t a = (color >> 24) & 0xFF;
    for (int y = c.y; y < c.y + c.height; y++) {
        uint32_t *row = s->backbuf + y * s->pitch_px;
        if (a == 0xFF) {
            for (int x = c.x; x < c.x + c.width; x++)
                row[x] = color;                              // opaque: store only
        } else {
            for (int x = c.x; x < c.x + c.width; x++)
                row[x] = alpha_blend(row[x], color);         // blend
        }
    }
}
```

### Present

```c
static void fb_present(gui_renderer_t *r) {
    fb_state_t *s = (fb_state_t *)r->backend;
    uint32_t words = (vga_fb_pitch / sizeof(uint32_t)) * s->height;
    uint32_t *src = s->backbuf;
    uint32_t *dst = s->vram;
    for (uint32_t i = 0; i < words; i++) dst[i] = src[i];
}
```

## Dependencies

- `renderer.h` — `gui_renderer_t`, `renderer_ops_t`, `glyph_t`
- `kheap.h` — `kmalloc`, `kfree`
- `string.h` — `memset`
- VGA globals: `vga_fb_addr`, `vga_fb_width`, `vga_fb_height`, `vga_fb_pitch` (from `vga.c`)
- `gui/math/rect.h` — `rect_make`, `rect_intersection`, `rect_is_empty`
- `fb_renderer.h` — self-header

## Limitations & Trade-offs

| Limitation | Impact |
|---|---|
| Full backbuffer memcpy on present | O(width × height) regardless of changed area. At 1920×1080 this is ~8 MB per frame. |
| Nearest-neighbor scaling | `fb_blit_scaled` uses integer rounding. Ugly at non-integer scales. |
| No horizontal clip per row | Glyph drawing tests `px < width` per column but does not limit the horizontal span. |
| Opaque-only glyph drawing | `fb_draw_glyph` writes `fg` directly (no alpha blend). Glyphs with alpha require the blit path. |
| float division in scaled blit | `(float)(x - dest_x) / scale_x` per pixel. Expensive without hardware FPU. |
| No batch processing | Each draw call loops independently. Overlapping writes cause redundant memory traffic. |

## Performance & Memory Optimizations

### Cache-friendly access patterns
- All loops iterate `x` inner, `y` outer — sequential memory access in the row-major backbuffer.
- `fb_fill_rect` computes `row = s->backbuf + y * s->pitch_px` once per row instead of re-multiplying per pixel.

### Fast paths
- **Opaque fill**: When `alpha == 0xFF`, `fb_fill_rect` does a straight store loop with no read-modify-write, halving memory bandwidth for solid fills.
- **Opaque blit**: When `src_pixel >> 24 == 0xFF`, writes directly without blend.
- **Transparent skip**: Alpha `== 0x00` skips the pixel entirely in the blit path.

### Memory
- Backbuffer: `vga_fb_pitch * vga_fb_height` bytes. For 1920×1080 with 7680-byte pitch: ~8.3 MB.
- VRAM reads: avoided entirely. The compositor's `draw_background` reads from backbuffer (for dot-grid) but never from VRAM.
- No intermediate surfaces: every operation writes directly to the backbuffer.

### Present bottleneck
The `fb_present` memcpy loop is the single largest CPU consumer. Potential optimizations:
- **SSE2 non-temporal stores**: `fast_memcpy` in `gpu.h` uses `movntdq` to bypass cache. Currently not wired into `fb_present`.
- **Dirty-rect present**: Only copy regions that changed, using the compositor's dirty rect list.
- **DIMM/DRAM bandwidth**: At 1920×1080×32bpp@60FPS, present requires ~3.6 GB/s bandwidth. Modern DDR4 handles this easily, but cache pollution is a concern.

## Future Extensions

- **SSE2 memcpy**: Wire `gpu_setup_wc_mtrr` + `fast_memcpy` into `fb_present` for non-temporal streaming to VRAM.
- **Partial present**: Use dirty rects to present only sub-regions.
- **Hardware cursor**: BGA supports a hardware cursor register — eliminate the save/restore/overlay in compositor.
- **Bilinear interpolation** in `fb_blit_scaled`.
- **Per-pixel alpha glyphs**: Expand PSF1 glyphs into pre-blended 8-bit alpha maps.
- **Write-combining MTRR**: Currently disabled due to a GPF on some hardware. Fixing this would significantly improve VRAM write performance.
