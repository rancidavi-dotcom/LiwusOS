#include "video.h"
#include "io.h"
#include "kheap.h"
#include "string.h"

uint32_t *framebuffer;
uint32_t *backbuffer;
uint32_t screen_width;
uint32_t screen_height;
uint32_t screen_size;

static uint32_t *target_buffer;
static uint32_t target_width;
static uint32_t target_height;

static int clip_x1, clip_y1, clip_x2, clip_y2;
extern unsigned char _binary_src_drivers_font_psf_start[];
#define _binary_font_psf_start _binary_src_drivers_font_psf_start
typedef struct {
  uint32_t magic, version, headersize, flags, numglyph, bytesperglyph, height,
      width;
} PSF2_header;
typedef struct {
  uint16_t magic;
  uint8_t mode, charsize;
} PSF1_header;

void set_clip(int x, int y, int w, int h) {
  clip_x1 = x;
  clip_y1 = y;
  clip_x2 = x + w;
  clip_y2 = y + h;
  if (clip_x1 < 0)
    clip_x1 = 0;
  if (clip_y1 < 0)
    clip_y1 = 0;
  if ((uint32_t)clip_x2 > target_width)
    clip_x2 = target_width;
  if ((uint32_t)clip_y2 > target_height)
    clip_y2 = target_height;
}

void reset_clip() {
  clip_x1 = 0;
  clip_y1 = 0;
  clip_x2 = target_width;
  clip_y2 = target_height;
}

void video_set_target(uint32_t *buffer, uint32_t width, uint32_t height) {
  target_buffer = buffer;
  target_width = width;
  target_height = height;
  reset_clip();
}

void video_reset_target() {
  target_buffer = backbuffer;
  target_width = screen_width;
  target_height = screen_height;
  reset_clip();
}

void init_video(multiboot_info_t *mbi) {
  framebuffer = (uint32_t *)((uint32_t)mbi->framebuffer_addr);
  screen_width = mbi->framebuffer_width;
  screen_height = mbi->framebuffer_height;
  screen_size = screen_width * screen_height;
  backbuffer = (uint32_t *)kmalloc(screen_size * 4);
  video_reset_target();
}

void draw_pixel(int x, int y, uint32_t color) {
  if (x < clip_x1 || x >= clip_x2 || y < clip_y1 || y >= clip_y2)
    return;
  target_buffer[y * target_width + x] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
  if (x < clip_x1) {
    w -= (clip_x1 - x);
    x = clip_x1;
  }
  if (y < clip_y1) {
    h -= (clip_y1 - y);
    y = clip_y1;
  }
  if (x + w > clip_x2)
    w = clip_x2 - x;
  if (y + h > clip_y2)
    h = clip_y2 - y;
  if (w <= 0 || h <= 0)
    return;

  for (int i = 0; i < h; i++) {
    uint32_t *dest = &target_buffer[(y + i) * target_width + x];
    int len = w;
    while (len >= 4) {
      dest[0] = color;
      dest[1] = color;
      dest[2] = color;
      dest[3] = color;
      dest += 4;
      len -= 4;
    }
    while (len--)
      *dest++ = color;
  }
}

void video_blit(uint32_t *source, int src_pitch, int sx, int sy, int w, int h,
                int dx, int dy) {
  for (int r = 0; r < h; r++) {
    int d_y = dy + r;
    if (d_y < clip_y1 || d_y >= clip_y2)
      continue;
    int s_y = sy + r;
    uint32_t *src_row = &source[s_y * src_pitch + sx];
    uint32_t *dest_row = &target_buffer[d_y * target_width + dx];

    int start_c = (dx < clip_x1) ? (clip_x1 - dx) : 0;
    int end_c = (dx + w > clip_x2) ? (clip_x2 - dx) : w;

    if (start_c >= end_c)
      continue;
    for (int c = start_c; c < end_c; c++) {
      uint32_t p = src_row[c];
      if (p != 0)
        dest_row[c] = p; // Chroma key: skip black
    }
  }
}

void draw_filled_circle(int xm, int ym, int r, uint32_t color) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r)
        draw_pixel(xm + x, ym + y, color);
    }
  }
}

void draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color) {
  draw_rect(x + r, y, w - 2 * r, h, color);
  draw_rect(x, y + r, w, h - 2 * r, color);
  draw_filled_circle(x + r, y + r, r, color);
  draw_filled_circle(x + w - r - 1, y + r, r, color);
  draw_filled_circle(x + r, y + h - r - 1, r, color);
  draw_filled_circle(x + w - r - 1, y + h - r - 1, r, color);
}

void draw_mouse_cursor(int x, int y, int type) {
  static const char *arrow[] = {
      "X           ", "XX          ", "X.X         ", "X..X        ",
      "X...X       ", "X....X      ", "X.....X     ", "X......X    ",
      "X.......X   ", "X........X  ", "X.....XXXXX ", "X..X..X     ",
      "X.X X..X    ", "XX   X..X   ", "     X..X   ", "      XX    "};
  const char **cursor = arrow;
  for (int i = 0; i < 16; i++) {
    for (int j = 0; j < 12; j++) {
      if (cursor[i][j] == 'X')
        draw_pixel(x + j, y + i, 0x000000);
      else if (cursor[i][j] == '.')
        draw_pixel(x + j, y + i, 0xFFFFFF);
    }
  }
}

void clear_screen(uint32_t color) {
  uint32_t size = target_width * target_height;
  uint32_t *ptr = target_buffer;
  while (size >= 8) {
    ptr[0] = color;
    ptr[1] = color;
    ptr[2] = color;
    ptr[3] = color;
    ptr[4] = color;
    ptr[5] = color;
    ptr[6] = color;
    ptr[7] = color;
    ptr += 8;
    size -= 8;
  }
  while (size--)
    *ptr++ = color;
}

void refresh_screen() {
  if (!framebuffer)
    return;
  uint32_t *src = backbuffer;
  uint32_t *dest = framebuffer;
  uint32_t n = screen_size;
  while (n >= 8) {
    dest[0] = src[0];
    dest[1] = src[1];
    dest[2] = src[2];
    dest[3] = src[3];
    dest[4] = src[4];
    dest[5] = src[5];
    dest[6] = src[6];
    dest[7] = src[7];
    dest += 8;
    src += 8;
    n -= 8;
  }
  while (n--)
    *dest++ = *src++;
}

void draw_loading_bar(int x, int y, int w, int h, int p) {
  draw_rect(x, y, w, h, 0x333333);
  draw_rect(x, y, (w * p) / 100, h, 0x00FF00);
}

void draw_char(int x, int y, char c, uint32_t color) {
  unsigned char *glyph;
  uint32_t height, width, bpl;
  PSF2_header *font2 = (PSF2_header *)_binary_font_psf_start;
  if (font2->magic == 0x864ab572) {
    height = font2->height;
    width = font2->width;
    bpl = (width + 7) / 8;
    glyph = _binary_font_psf_start + font2->headersize +
            ((unsigned char)c * font2->bytesperglyph);
  } else {
    PSF1_header *font1 = (PSF1_header *)_binary_font_psf_start;
    height = font1->charsize;
    width = 8;
    bpl = 1;
    glyph = _binary_font_psf_start + sizeof(PSF1_header) +
            ((unsigned char)c * font1->charsize);
  }
  for (uint32_t cy = 0; cy < height; cy++) {
    for (uint32_t cx = 0; cx < width; cx++) {
      if (glyph[cy * bpl + cx / 8] & (0x80 >> (cx % 8)))
        draw_pixel(x + cx, y + cy, color);
    }
  }
}

void draw_string(int x, int y, const char *str, uint32_t color) {
  int sx = x;
  while (*str) {
    if (*str == '\n') {
      x = sx;
      y += 16;
    } else {
      draw_char(x, y, *str, color);
      x += 8;
    }
    str++;
  }
}

void video_refresh_rect(rect_t r) {
  if (!framebuffer)
    return;
  if (r.x < 0) {
    r.w += r.x;
    r.x = 0;
  }
  if (r.y < 0) {
    r.h += r.y;
    r.y = 0;
  }
  if ((uint32_t)(r.x + r.w) > screen_width)
    r.w = screen_width - r.x;
  if ((uint32_t)(r.y + r.h) > screen_height)
    r.h = screen_height - r.y;
  if (r.w <= 0 || r.h <= 0)
    return;

  for (int i = 0; i < r.h; i++) {
    uint32_t *src = &backbuffer[(r.y + i) * screen_width + r.x];
    uint32_t *dest = &framebuffer[(r.y + i) * screen_width + r.x];
    int n = r.w;
    while (n >= 4) {
      dest[0] = src[0];
      dest[1] = src[1];
      dest[2] = src[2];
      dest[3] = src[3];
      dest += 4;
      src += 4;
      n -= 4;
    }
    while (n--)
      *dest++ = *src++;
  }
}

rect_t video_rect_intersect(rect_t a, rect_t b) {
  int x1 = (a.x > b.x) ? a.x : b.x;
  int y1 = (a.y > b.y) ? a.y : b.y;
  int x2 = (a.x + a.w < b.x + b.w) ? a.x + a.w : b.x + b.w;
  int y2 = (a.y + a.h < b.y + b.h) ? a.y + a.h : b.y + b.h;
  if (x2 <= x1 || y2 <= y1)
    return (rect_t){0, 0, 0, 0};
  return (rect_t){x1, y1, x2 - x1, y2 - y1};
}

/* Alpha Blending Helper */
void draw_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha) {
  if (x < clip_x1 || x >= clip_x2 || y < clip_y1 || y >= clip_y2)
    return;

  uint32_t *ptr = &target_buffer[y * target_width + x];
  uint32_t bg = *ptr;

  // Extract channels
  uint32_t r_bg = (bg >> 16) & 0xFF;
  uint32_t g_bg = (bg >> 8) & 0xFF;
  uint32_t b_bg = (bg) & 0xFF;

  uint32_t r_fg = (color >> 16) & 0xFF;
  uint32_t g_fg = (color >> 8) & 0xFF;
  uint32_t b_fg = (color) & 0xFF;

  // Blend: out = alpha * fg + (1-alpha) * bg
  // alpha 0..255
  uint32_t r_out = (alpha * r_fg + (255 - alpha) * r_bg) / 255;
  uint32_t g_out = (alpha * g_fg + (255 - alpha) * g_bg) / 255;
  uint32_t b_out = (alpha * b_fg + (255 - alpha) * b_bg) / 255;

  *ptr = (r_out << 16) | (g_out << 8) | b_out;
}

/* Simulated Box Shadow (Simplified for performance) */
void draw_box_shadow(int x, int y, int w, int h, int r, int blur,
                     uint32_t color) {
  // Draw 3 layers of fading rects to simulate blur shadow
  // Shadow 1 (Main): Offset Y=8, Blur=24 (Simulated by wide fading rects)

  (void)blur; // Use fixed mock blur steps for speed

  // Layer 1: Deep shadow (close)
  // Offset Y+4, Alpha ~30
  int offset_y = 6;
  int spread = 2; // Extra pixels out

  uint8_t alpha_base =
      (color >> 24) &
      0xFF; // Usually ignored in passed color int, treating as opaque RGB?
  // Color arg format not strictly ARGB in this OS drawing API usually?
  // `draw_pixel` does simple set. `draw_pixel_alpha` uses separate alpha arg.
  // Let's assume color is RGB and we use hardcoded alpha steps for shadow.

  uint32_t shadow_rgb = 0x000000;

  // Step 1: Wide faint (The "Blur")
  // Draw rounded rect with low alpha
  for (int i = 0; i < 3; i++) {
    int expand = 4 + (i * 3);
    uint8_t a = 10 - (i * 2); // Very faint
    // Using draw_rect with alpha per pixel... expensive?
    // Let's implement rect_alpha
    for (int py = y + offset_y - expand; py < y + h + offset_y + expand; py++) {
      // Optimized horizontal Scanline?
      // Just simple loop for now
      for (int px = x - expand; px < x + w + expand; px++) {
        // Skip center (covered by window) to save fill rate?
        if (px >= x && px < x + w && py >= y && py < y + h)
          continue;

        // Rounded mask check? Too complex for software "fast" shadow.
        // Just Rect Shadow for speed, maybe corners look weird.
        draw_pixel_alpha(px, py, shadow_rgb, a);
      }
    }
  }
}

void draw_button_visual(int x, int y, int w, int h, const char *text,
                        uint32_t color) {
  draw_rect(x, y, w, h, color);
  draw_string(x + 5, y + 5, text, 0xFFFFFF);
}
