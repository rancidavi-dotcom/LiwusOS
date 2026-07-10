# Asset Manager

## Objective

Centralize loading, caching, and lifetime management of GUI assets (fonts, images, cursors, icon themes) in the LiwusOS kernel. The asset manager provides a uniform API for widgets to retrieve glyphs and future pixel data without knowing the underlying storage format or memory layout.

## Problems Solved

- **Single point of loading**: One `asset_manager_get_font()` call replaces scattered font parsing across widgets.
- **Embedded binary access**: The PSF1 font is linked as a raw binary (`_binary_src_drivers_font_psf_start`). The asset manager handles pointer arithmetic to extract glyph bitmaps.
- **No redundant copies**: Glyph bitmaps point directly to the embedded ROM region — no per-widget copy.
- **Lazy initialization**: Font is parsed on first access (`asset_manager_get_font` calls `asset_manager_init` if needed).
- **Future extensibility**: The `glyph_t` interface abstracts the font format. Adding TrueType or bitmap fonts does not change widget code.

## Architecture

### Font Loading Flow

```
Kernel Binary
  ┌─────────────────────────────────────┐
  │  _binary_src_drivers_font_psf_start │  (embedded PSF1 blob)
  └──────────┬──────────────────────────┘
             │
             ▼
  asset_manager_init()
     │
     ├── Parse PSF1 header:
     │   offset[0..1] = magic 0x0436
     │   offset[2]    = mode (0)
     │   offset[3]    = bytes_per_glyph (bpg)
     │   offset[4+]   = glyph bitmap data
     │
     └── For each glyph i (0..255):
         s_default_font[i].bitmap = font_data + (i * bpg)
         s_default_font[i].cell_w  = 8
         s_default_font[i].cell_h  = 16
             │
             ▼
  asset_manager_get_font("system")
             │
             ▼
  Returns glyph_t[256] array — widgets index by codepoint
```

### PSF1 Format

```
Offset  Size  Field
──────  ────  ─────────────────
  0      2    Magic number: 0x0436
  2      1    Mode (0 = 256-glyph, 1 = 512-glyph)
  3      1    Bytes per glyph (typically 16 for 8×16 font)
  4+     N    Glyph bitmap data (bpg bytes per glyph)

Each glyph: bpg bytes, each byte is one row of 8 pixels.
Bit 0x80 = pixel on, 0x00 = pixel off.
```

### Data Structures

```c
// Font glyph — one per character in the font
typedef struct {
    const uint8_t *bitmap;  // 16 rows × 1 byte each (8×16 monospace)
    int cell_w;             // = 8 (fixed for PSF1)
    int cell_h;             // = 16 (fixed for PSF1)
} glyph_t;

// Internal asset manager state
static glyph_t s_default_font[256];  // 256 codepoints
static bool    s_font_loaded = false;
```

### Memory Layout

```
.binary section (ROM)
  ┌──────────────────────┐
  │ PSF1 header (4 bytes)│
  ├──────────────────────┤
  │ Glyph 0 bitmap (16B) │ ← s_default_font[0].bitmap
  │ Glyph 1 bitmap (16B) │ ← s_default_font[1].bitmap
  │ ...                  │
  │ Glyph 255 bitmap(16B)│ ← s_default_font[255].bitmap
  └──────────────────────┘

BSS/Data (RAM)
  ┌──────────────────────┐
  │ s_default_font[256]  │ ← 256 × (ptr + 2 ints) = ~2 KB
  │ s_default_font[0].   │
  │   bitmap ────────────┼────► ROM address (no copy!)
  │   cell_w = 8         │
  │   cell_h = 16        │
  └──────────────────────┘
```

**Key property**: Glyph bitmaps point directly to the embedded binary in ROM. No heap allocation for font data. The `s_default_font` array is 256 × 16 bytes ≈ 4 KB (on 64-bit: 8 byte ptr + 4 + 4 = 16 bytes per entry).

## APIs

### Public

```c
// Initialize the asset manager and parse embedded font
void asset_manager_init(void);

// Clean up (currently a no-op since nothing is dynamically allocated)
void asset_manager_destroy(void);

// Get a font by name. Returns the default font if name is NULL or unknown.
// Currently only one font exists.
const glyph_t *asset_manager_get_font(const char *name);
```

### Private / Internal

```c
extern char _binary_src_drivers_font_psf_start[];  // linker symbol

// PSF1 header parsing (inlined in asset_manager_init):
uint8_t *font_hdr = (uint8_t*)&_binary_src_drivers_font_psf_start;
bool valid = (font_hdr[0] == 0x36 && font_hdr[1] == 0x04);
int bpg = font_hdr[3];
uint8_t *font_data = font_hdr + 4;
```

### Widget Usage

```c
// Label draws text by indexing the font array:
const glyph_t *font = asset_manager_get_font(NULL);
for (int i = 0; d->text[i] != '\0'; i++) {
    unsigned char c = d->text[i];
    renderer_draw_glyph(r, cx, cy, d->color, 0x00000000, &font[c]);
    cx += 8;  // fixed cell width
}
```

## Dependencies

- Linker script provides `_binary_src_drivers_font_psf_start` (from `src/drivers/font.psf`).
- `string.h` — `memset` for fallback font initialization.
- No file system, no disk I/O, no dynamic loading.

## Limitations / Trade-offs

| Limitation | Rationale |
|------------|-----------|
| Single font (8×16 monospace) | Only one PSF1 font is embedded. No font switching, no variable-width, no Unicode > 255. |
| No font file loading | Font is linked into the kernel binary. Adding fonts requires recompilation. |
| 256 glyphs only | PSF1 mode 0. Codepoints 0x80–0xFF map to any glyph, but no Unicode surrogate pairs. |
| No glyph scaling | Text is always 8×16 pixels regardless of camera zoom. Scaled text requires SDF or bitmap scaling (future). |
| Pointer to ROM | Safe as long as font data is in the kernel's address space. Portable to other architectures requires address translation. |
| No reference counting | `s_default_font` is a static array — always alive. Future dynamically-loaded fonts need ref-counted handles. |

## Performance / Memory Optimizations

- **Zero-copy font access**: Glyph bitmaps are not copied. Widgets read directly from the ROM mapping.
- **Lazy init**: Font parsing happens once, on first use. If no GUI widget ever calls `asset_manager_get_font`, the loop is skipped.
- **Static array** (`s_default_font[256]`): No heap fragmentation. O(1) lookup for any codepoint.
- **Asymptotic**: Font lookup is `O(1)` — array indexing by codepoint. Rendering a string of length N is `O(N)`.

## Future Extensions

| Feature | Approach |
|---------|----------|
| Multiple fonts | `s_font_cache` hash table keyed by font name. `asset_manager_get_font("Sans")` returns different `glyph_t*` array. |
| TrueType support | Integrate a stripped-down stb_truetype or hand-written TTF parser. Renders to a `glyph_t`-compatible bitmap on load. |
| Variable-width fonts | Add `advance_x` field to `glyph_t`. Widgets sum advances instead of assuming 8px per glyph. |
| Unicode (UTF-8) | Add codepoint → glyph index mapping table. Surrogate pair support for codepoints > 255. |
| Image loading | `asset_manager_load_image("button_sprite.png")` returns `image_t { uint32_t *pixels, int w, int h }`. Uses libpng stripped for kernel. |
| Cursor themes | Load animated cursor sprites from disk. `asset_manager_get_cursor(CURSOR_ARROW)` returns multi-frame cursor sequence. |
| Icon themes | Named icons (`asset_manager_get_icon("close")`) loaded from a zip/bundle. |
| Hot reload | Watch file modification time, re-parse on change, update all references. |
| Ref counting | `asset_t` with `acquire()/release()` for shared font and image references. |

## Usage Examples

```c
// Init (automatic on first get, or explicit during GUI bootstrap)
asset_manager_init();

// Retrieve font for drawing
const glyph_t *font = asset_manager_get_font(NULL);
if (!font) return;

// Draw a character
renderer_draw_glyph(r, screen_x, screen_y,
                    0xFFFFFFFF,   // white foreground
                    0x00000000,   // transparent background
                    &font['A']);  // glyph for 'A'

// Widgets cache the font pointer locally
// button_data_t, label_data_t, etc. all call asset_manager_get_font(NULL)
// once during their first draw and cache it.
```
