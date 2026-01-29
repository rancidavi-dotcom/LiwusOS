/* gui/render/fb_renderer.h */
#ifndef GUI_FB_RENDERER_H
#define GUI_FB_RENDERER_H

#include "renderer.h"

/* Construct a software framebuffer renderer from the current VGA globals. */
gui_renderer_t *fb_renderer_create(void);

/* Returns the internal back-buffer (for direct pixel access by the compositor). */
uint32_t *fb_renderer_backbuf(gui_renderer_t *r);

/* Draw a single pixel into the renderer's backing buffer. */
void fb_renderer_draw_pixel(gui_renderer_t *r, int x, int y, uint32_t color);

#endif /* GUI_FB_RENDERER_H */
