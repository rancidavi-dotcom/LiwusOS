# Rendering Pipeline — Widget to Pixel

## Objective

Document the complete end-to-end transformation chain from a widget's local coordinate space to the final ARGB pixel in VRAM. Every visible element on screen passes through: local coordinate framing → world transform → camera projection → screen-space clipping → alpha-blended rasterization → backbuffer composition → VRAM flip.

## Problems Solved

- **Coordinate space isolation**: Widgets think in local coordinates. The pipeline transparently projects them through world/camera space without per-widget math.
- **Correct alpha compositing**: Source-over blending with per-pixel alpha, global opacity multiplier, and 0xFF (fully opaque) fast path.
- **Clip hierarchy enforcement**: Screen bounds, renderer clip, and backend clip are intersected to guarantee no pixel is written outside the visible area.
- **Fixed-point math**: No SSE/FPU required. Camera projection and transform concatenation use 16.16 and 10.22 fixed-point arithmetic.

## Pipeline Stages

```
┌──────────────────────────────────────────────────────────────┐
│                     RENDERING PIPELINE                       │
│                                                              │
│  WIDGET SPACE                                                │
│  ┌───────────────────────┐                                   │
│  │ Node.local_x/y        │  (relative to parent)             │
│  │ Node.width/height     │  (logical dimensions)             │
│  └───────────┬───────────┘                                   │
│              │                                               │
│              ▼   node_update_transforms()                    │
│  ┌───────────────────────┐                                   │
│  │ WORLD SPACE           │                                   │
│  │ Node.world_transform  │  (parent ∘ local = 3×3 matrix)   │
│  │ Node.screen_bounds    │  (updated by widget draw)         │
│  └───────────┬───────────┘                                   │
│              │                                               │
│              ▼   camera_world_to_screen_x/y()                │
│  ┌───────────────────────┐                                   │
│  │ SCREEN SPACE          │                                   │
│  │ (pixel coordinates)   │  (zoom_fp / CAMERA_ZOOM_SCALE)    │
│  └───────────┬───────────┘                                   │
│              │                                               │
│              ▼   rect_intersection()                         │
│  ┌───────────────────────┐                                   │
│  │ CLIP STAGE            │                                   │
│  │ 1. Screen bounds      │  (0,0)-(screen_w,screen_h)       │
│  │ 2. Renderer clip rect │  (set_clip by widget/compositor)  │
│  │ 3. Backend clip       │  (fb_clip intersects both)        │
│  └───────────┬───────────┘                                   │
│              │                                               │
│              ▼   alpha_blend()                               │
│  ┌───────────────────────┐                                   │
│  │ RASTERIZE             │                                   │
│  │ src-over compositing  │  dst = fg*a + bg*(255-a) / 255   │
│  │ 0xFF opaque fast path │  memcpy for fully opaque          │
│  └───────────┬───────────┘                                   │
│              │                                               │
│              ▼                                               │
│  ┌───────────────────────┐                                   │
│  │ BACKBUFFER            │                                   │
│  │ (heap-allocated)      │  (W × H pixels, 32-bit ARGB)     │
│  └───────────┬───────────┘                                   │
│              │                                               │
│              ▼   renderer_present() → memcpy                 │
│  ┌───────────────────────┐                                   │
│  │ VRAM (VBE LFB)        │                                   │
│  │ (physical framebuffer)│  (BGA graphics card LFB)         │
│  └───────────────────────┘                                   │
└──────────────────────────────────────────────────────────────┘
```

## Coordinate Space Chain

### 1. Local (Widget) Space

Each node has a `local_x`, `local_y`, `width`, `height` in its parent's coordinate space. No scaling or rotation is applied at this level.

```
Node A at (10, 20) with size (100, 50)
  └─ Child B at (5, 5)
```

### 2. World Space

`node_update_transforms()` concatenates the local translation transform with the parent's `world_transform`:

```c
gui_transform_t local = transform_translation(node->local_x, node->local_y);
node->world_transform = transform_concat(local, parent_world);
```

The transform is a 3×3 affine matrix with 16.16 fixed-point:

```
| a  c  tx |   | scale_x  shear_x  trans_x |
| b  d  ty | = | shear_y  scale_y  trans_y |
| 0  0   1 |   |    0        0        1     |
```

### 3. Screen Space

Widget draw functions project their world position to screen pixels using the camera:

```c
int screen_x = camera_world_to_screen_x(cam, world_x);
int screen_y = camera_world_to_screen_y(cam, world_y);
int screen_w = camera_scale(cam, node->width);
int screen_h = camera_scale(cam, node->height);
```

Camera conversion formula (fixed-point integer, no floats):

```c
// world_to_screen_x:
diff = (world_x * CAMERA_POS_SCALE) - cam->pos_x_fp
screen_x = (diff * cam->zoom_fp) / (CAMERA_ZOOM_SCALE * CAMERA_POS_SCALE)
```

### 4. Clip Stage

Before any pixel write, the backend clips against up to three rectangles, intersected via `rect_intersection()`:

```c
gui_rect_t screen = rect_make(0, 0, (int)s->width, (int)s->height);
gui_rect_t clipped = rect_intersection(rect, screen);
if (!rect_is_empty(r->clip))
    clipped = rect_intersection(clipped, r->clip);
return clipped;
```

### 5. Rasterize Stage

For each pixel in the clipped destination rectangle:

**Solid fill (`fb_fill_rect`):**
- If `alpha == 0xFF` → direct store (no read-modify-write)
- If `alpha == 0x00` → skip
- Else → `alpha_blend(backbuf[pixel], color)`

**Blit (`fb_blit`):**
- Source pixel alpha determines blending:
  - `0xFF` → direct store (fast path)
  - `0x00` → skip
  - `[1..254]` → `alpha_blend(dst, src)`

**Glyph (`fb_draw_glyph`):**
- 1bpp bitmap unpacking: each byte column selects `fg` (bit=1) or `bg` (bit=0)
- `bg=0` means transparent background

### 6. Present Stage

`fb_present()` copies the entire backbuffer to VRAM:

```c
uint32_t words = (vga_fb_pitch / sizeof(uint32_t)) * s->height;
for (uint32_t i = 0; i < words; i++)
    dst[i] = src[i];
```

This is a linear 1:1 copy. Future: SSE2 non-temporal stores via `fast_memcpy`.

## Alpha Blending Pipeline

```c
static inline uint32_t alpha_blend(uint32_t bg, uint32_t fg) {
    uint32_t a = (fg >> 24) & 0xFF;     // source alpha
    if (a == 0xFF) return fg;            // fully opaque
    if (a == 0x00) return bg;            // fully transparent
    uint32_t inv = 255 - a;              // dest alpha contribution
    // Per-channel blend: (fg*a + bg*(255-a)) >> 8
    uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv) >> 8;
    uint32_t g = (((fg >>  8) & 0xFF) * a + ((bg >>  8) & 0xFF) * inv) >> 8;
    uint32_t b = (( fg        & 0xFF) * a + ( bg        & 0xFF) * inv) >> 8;
    return (0xFF000000) | (r << 16) | (g << 8) | b;
}
```

The result alpha is always `0xFF` (destination stays opaque).

## Backbuffer Strategy

```
┌─────────────────────────────────────────┐
│  Each Frame:                            │
│  1. draw_background()                   │
│     → memset entire backbuffer          │
│     → plot dot-grid dots                │
│  2. node_draw_recursive()               │
│     → each widget writes to backbuffer  │
│  3. cursor_draw()                       │
│     → cursor sprite overlaid            │
│  4. renderer_present()                  │
│     → memcpy backbuffer → VRAM          │
└─────────────────────────────────────────┘
```

## Dependencies

- `gui/math/rect.h` — rect intersection, union, containment
- `gui/math/transform.h` — 3×3 affine fixed-point transforms
- `gui/scene/camera.h` — world↔screen conversion
- `gui/render/renderer.h` — abstract render interface
- `gui/render/fb_renderer.c` — concrete pixel pipeline
- `gui/scene/node.h` — scene graph traversal

## Limitations & Trade-offs

| Stage | Limitation |
|---|---|
| Transform | Simple translate+scale only. No rotation, no skew. `transform_invert_simple` uses floats and assumes no shear. |
| Glyph | Nearest-neighbor only (no sub-pixel positioning). PSF1 fixed 8×16. |
| Blit scaled | Nearest-neighbor pixel sampling. No bilinear/trilinear filtering. |
| Present | Full-screen memcpy every frame — same bandwidth regardless of how many pixels changed. |
| Background | O(W×H) fill plus O(dots) grid. At 1920×1080 the dot loop visits ~2600 dots. |

## Performance & Memory Optimizations

- **Opaque fast path**: `alpha == 0xFF` branches to a simple store loop, avoiding the read-modify-write of alpha blending.
- **Row-major access**: All loops iterate x-inner, y-outer for cache-friendly sequential memory access.
- **Backbuffer size**: `vga_fb_pitch × height` bytes; for 1920×1080×32bpp with 7680-byte pitch ≈ 8.3 MB.
- **No overdraw elimination**: Widgets are drawn back-to-front; every pixel behind a widget is painted twice (background + widget). Future: depth sorting, occlusion culling.

## Future Extensions

- **GPU pipeline**: Vertex shaders for transform, fragment shaders for blend modes.
- **Signed Distance Fields** for resolution-independent font rendering.
- **Bilinear filtering** for `blit_scaled`.
- **Triple buffering** via flip-chain.
- **Hardware cursor** using BGA's hardware cursor register.
- **Per-window textures** for compositor-side caching of static windows.
