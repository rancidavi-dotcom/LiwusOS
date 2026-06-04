#include "video.h"
#include "io.h"
#include "kheap.h"
#include "string.h"
#include "vmm.h"

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
  clip_x1 = (x < 0) ? 0 : x;
  clip_y1 = (y < 0) ? 0 : y;
  clip_x2 = (uint32_t)(x + w) > target_width ? target_width : (x + w);
  clip_y2 = (uint32_t)(y + h) > target_height ? target_height : (y + h);
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
  uint32_t fb_addr;
  uint32_t fb_size;
  uint32_t map_start;
  uint32_t map_end;

  if (!mbi || !(mbi->flags & (1 << 12)) || mbi->framebuffer_addr == 0 ||
      mbi->framebuffer_width == 0 || mbi->framebuffer_height == 0 ||
      mbi->framebuffer_bpp != 32) {
    framebuffer = 0;
    backbuffer = 0;
    screen_width = 0;
    screen_height = 0;
    screen_size = 0;
    target_buffer = 0;
    target_width = 0;
    target_height = 0;
    return;
  }

  framebuffer = (uint32_t *)((uint32_t)mbi->framebuffer_addr);
  screen_width = mbi->framebuffer_width;
  screen_height = mbi->framebuffer_height;
  screen_size = screen_width * screen_height;

  fb_addr = (uint32_t)mbi->framebuffer_addr;
  fb_size = mbi->framebuffer_pitch * mbi->framebuffer_height;
  if (fb_size == 0) {
    fb_size = screen_size * 4;
  }
  map_start = fb_addr & 0xFFFFF000;
  map_end = (fb_addr + fb_size + 0xFFF) & 0xFFFFF000;
  for (uint32_t addr = map_start; addr < map_end; addr += 4096) {
    vmm_map_page((void *)addr, (void *)addr, 0x7);
  }

  backbuffer = (uint32_t *)kmalloc(screen_size * 4);
  video_reset_target();
}

void draw_pixel(int x, int y, uint32_t color) {
  if (x < clip_x1 || x >= clip_x2 || y < clip_y1 || y >= clip_y2)
    return;
  target_buffer[y * target_width + x] = color;
}

void draw_rect(int x, int y, int w, int h, uint32_t color) {
  if (x < clip_x1) { w -= (clip_x1 - x); x = clip_x1; }
  if (y < clip_y1) { h -= (clip_y1 - y); y = clip_y1; }
  if (x + w > clip_x2) w = clip_x2 - x;
  if (y + h > clip_y2) h = clip_y2 - y;
  if (w <= 0 || h <= 0) return;

  uint32_t *dest = &target_buffer[y * target_width + x];
  for (int i = 0; i < h; i++) {
    memset32(dest, color, w);
    dest += target_width;
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
    
    // Fast path: memcpy for row
    memcpy(&dest_row[start_c], &src_row[start_c], (end_c - start_c) * 4);
  }
}

void draw_filled_circle(int xm, int ym, int r, uint32_t color) {
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      if (x * x + y * y <= r * r) {
          int px = xm + x;
          int py = ym + y;
          if (px >= clip_x1 && px < clip_x2 && py >= clip_y1 && py < clip_y2)
            target_buffer[py * target_width + px] = color;
      }
    }
  }
}

void clear_screen(uint32_t color) {
  memset32(target_buffer, color, target_width * target_height);
}

void refresh_screen() {
  if (!framebuffer)
    return;
  memcpy(framebuffer, backbuffer, screen_size * 4);
}

void draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color) {
  (void)r;
  draw_rect(x, y, w, h, color);
}

void draw_mouse_cursor(int x, int y, int type) {
  (void)type;
  draw_filled_circle(x + 3, y + 3, 3, 0xFFF4F4F4);
  draw_filled_circle(x + 3, y + 3, 1, 0xFF111111);
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
  if (r.x < 0) { r.w += r.x; r.x = 0; }
  if (r.y < 0) { r.h += r.y; r.y = 0; }
  if ((uint32_t)(r.x + r.w) > screen_width) r.w = screen_width - r.x;
  if ((uint32_t)(r.y + r.h) > screen_height) r.h = screen_height - r.y;
  if (r.w <= 0 || r.h <= 0) return;

  for (int i = 0; i < r.h; i++) {
    uint32_t *src = &backbuffer[(r.y + i) * screen_width + r.x];
    uint32_t *dest = &framebuffer[(r.y + i) * screen_width + r.x];
    memcpy(dest, src, r.w * 4);
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

/* Alpha Blending Helper - Optimized */
void draw_pixel_alpha(int x, int y, uint32_t color, uint8_t alpha) {
  if (x < clip_x1 || x >= clip_x2 || y < clip_y1 || y >= clip_y2)
    return;
  if (alpha == 0) return;
  if (alpha == 255) { target_buffer[y * target_width + x] = color; return; }

  uint32_t *ptr = &target_buffer[y * target_width + x];
  uint32_t bg = *ptr;
  uint32_t rb = bg & 0xFF00FF;
  uint32_t g = bg & 0x00FF00;
  uint32_t rb_fg = color & 0xFF00FF;
  uint32_t g_fg = color & 0x00FF00;
  rb += ((rb_fg - rb) * alpha) >> 8;
  g += ((g_fg - g) * alpha) >> 8;
  *ptr = (rb & 0xFF00FF) | (g & 0x00FF00);
}

void draw_box_shadow(int x, int y, int w, int h, int r, int blur, uint32_t color) {
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)r;
  (void)blur;
  (void)color;
}

void draw_button_visual(int x, int y, int w, int h, const char *text, uint32_t color) {
  draw_rounded_rect(x, y, w, h, 8, color);
  draw_rect(x, y + h - 1, w, 1, 0x50000000);
  draw_string(x + 10, y + 9, text, 0xFFFFFF);
}
