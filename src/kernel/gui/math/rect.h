/*
 * gui/math/rect.h
 *
 * Axis-Aligned Bounding Box (AABB) — base de todo cálculo de layout,
 * culling, dirty regions e hit-testing do LiwusOS GUI.
 */
#ifndef GUI_RECT_H
#define GUI_RECT_H

#include <stdint.h>
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * Types
 * -------------------------------------------------------------------------- */

typedef struct {
    int x, y;          /* top-left in local coordinate space */
    int width, height;
} gui_rect_t;

typedef struct {
    float x, y;
} gui_point_t;

typedef struct {
    int x, y;
} gui_pointi_t;

/* --------------------------------------------------------------------------
 * Construction helpers
 * -------------------------------------------------------------------------- */

static inline gui_rect_t rect_make(int x, int y, int w, int h) {
    gui_rect_t r; r.x = x; r.y = y; r.width = w; r.height = h;
    return r;
}

static inline gui_rect_t rect_zero(void) {
    return rect_make(0, 0, 0, 0);
}

/* --------------------------------------------------------------------------
 * Predicates
 * -------------------------------------------------------------------------- */

static inline bool rect_is_empty(gui_rect_t r) {
    return r.width <= 0 || r.height <= 0;
}

static inline bool rect_contains_point(gui_rect_t r, int px, int py) {
    return (px >= r.x && px < r.x + r.width &&
            py >= r.y && py < r.y + r.height);
}

static inline bool rect_intersects(gui_rect_t a, gui_rect_t b) {
    return !(a.x + a.width  <= b.x ||
             b.x + b.width  <= a.x ||
             a.y + a.height <= b.y ||
             b.y + b.height <= a.y);
}

/* --------------------------------------------------------------------------
 * Operations
 * -------------------------------------------------------------------------- */

/* Returns the intersection of two rectangles, or a zero rect if they don't
 * overlap.  Callers should check rect_is_empty() on the result. */
static inline gui_rect_t rect_intersection(gui_rect_t a, gui_rect_t b) {
    int x1 = a.x > b.x ? a.x : b.x;
    int y1 = a.y > b.y ? a.y : b.y;
    int x2 = (a.x + a.width)  < (b.x + b.width)  ? (a.x + a.width)  : (b.x + b.width);
    int y2 = (a.y + a.height) < (b.y + b.height) ? (a.y + a.height) : (b.y + b.height);
    if (x2 <= x1 || y2 <= y1) return rect_zero();
    return rect_make(x1, y1, x2 - x1, y2 - y1);
}

/* Smallest rectangle enclosing both. */
static inline gui_rect_t rect_union(gui_rect_t a, gui_rect_t b) {
    if (rect_is_empty(a)) return b;
    if (rect_is_empty(b)) return a;
    int x1 = a.x < b.x ? a.x : b.x;
    int y1 = a.y < b.y ? a.y : b.y;
    int x2a = a.x + a.width,  x2b = b.x + b.width;
    int y2a = a.y + a.height, y2b = b.y + b.height;
    int x2 = x2a > x2b ? x2a : x2b;
    int y2 = y2a > y2b ? y2a : y2b;
    return rect_make(x1, y1, x2 - x1, y2 - y1);
}

/* Translate a rect by (dx, dy). */
static inline gui_rect_t rect_offset(gui_rect_t r, int dx, int dy) {
    return rect_make(r.x + dx, r.y + dy, r.width, r.height);
}

/* Expand rect outward by margin pixels on every side. */
static inline gui_rect_t rect_inflate(gui_rect_t r, int margin) {
    return rect_make(r.x - margin, r.y - margin,
                     r.width + 2*margin, r.height + 2*margin);
}

/* Clip rect so it does not exceed bounds. */
static inline gui_rect_t rect_clip_to(gui_rect_t r, gui_rect_t bounds) {
    return rect_intersection(r, bounds);
}

#endif /* GUI_RECT_H */
