/*
 * gui/widgets/button.c
 */
#include "button.h"
#include "../render/renderer.h"
#include "../render/compositor.h"
#include "kheap.h"
#include "string.h"
#include "../assets/asset_manager.h"
#include "../core/theme_engine.h"
#include "../core/animation_engine.h"
typedef struct {
    char              *text;
    bool               hovered;
    bool               pressed;
    button_click_cb_t  on_click;
    void              *click_ud;
    button_click_cb_t  on_double_click;
    void              *double_click_ud;
    uint32_t           last_click_time;
    const glyph_t     *font;
    uint32_t           current_bg_color;
} button_data_t;

static void button_draw(node_t *self, struct gui_renderer *r) {
    button_data_t *d = (button_data_t *)self->userdata;
    if (!d) return;

    if (!d->font) {
        d->font = asset_manager_get_font(NULL);
    }

    extern compositor_t *g_compositor;
    if (!g_compositor) return;
    camera_t *cam = g_compositor->camera;

    gui_pointi_t pt = transform_apply(self->world_transform, 0, 0);
    int screen_x = camera_world_to_screen_x(cam, pt.x);
    int screen_y = camera_world_to_screen_y(cam, pt.y);
    int screen_w = camera_scale(cam, self->width);
    int screen_h = camera_scale(cam, self->height);
    
    self->screen_bounds = rect_make(screen_x, screen_y, screen_w, screen_h);

    /* Background — solid dark green (animated color) */
    renderer_fill_rect(r, self->screen_bounds, d->current_bg_color);

    /* 3D DOS border: top/left bright, bottom/right dark */
    uint32_t bright = theme_engine_get_color(THEME_COLOR_BUTTON_BORDER);
    uint32_t dark   = theme_engine_get_color(THEME_COLOR_BUTTON_BG_PRESS);

    if (!d->pressed) {
        /* Normal: raised 3D look */
        /* Top edge */
        renderer_fill_rect(r, rect_make(screen_x, screen_y, screen_w, 1), bright);
        /* Left edge */
        renderer_fill_rect(r, rect_make(screen_x, screen_y, 1, screen_h), bright);
        /* Bottom edge */
        renderer_fill_rect(r, rect_make(screen_x, screen_y + screen_h - 1, screen_w, 1), dark);
        /* Right edge */
        renderer_fill_rect(r, rect_make(screen_x + screen_w - 1, screen_y, 1, screen_h), dark);
    } else {
        /* Pressed: inverted 3D look (sunk) */
        renderer_fill_rect(r, rect_make(screen_x, screen_y, screen_w, 1), dark);
        renderer_fill_rect(r, rect_make(screen_x, screen_y, 1, screen_h), dark);
        renderer_fill_rect(r, rect_make(screen_x, screen_y + screen_h - 1, screen_w, 1), bright);
        renderer_fill_rect(r, rect_make(screen_x + screen_w - 1, screen_y, 1, screen_h), bright);
    }

    /* Focus ring — bright green outline when focused */
    extern node_t *focus_manager_get_focus(void *);
    extern void *g_focus_manager;
    if (g_focus_manager && focus_manager_get_focus(g_focus_manager) == self) {
        renderer_draw_rect(r, rect_make(screen_x - 1, screen_y - 1, screen_w + 2, screen_h + 2),
            theme_engine_get_color(THEME_COLOR_TEXT_PRIMARY), 1);
    }

    /* Text — centered, bright green */
    if (d->text) {
        int text_len = strlen(d->text);
        int text_w = text_len * 8;
        int text_h = 16;

        int cx = screen_x + (screen_w - text_w) / 2;
        int cy = screen_y + (screen_h - text_h) / 2;

        for (int i = 0; i < text_len; i++) {
            unsigned char c = d->text[i];
            renderer_draw_glyph(r, cx, cy, theme_engine_get_color(THEME_COLOR_BUTTON_TEXT), 0x00000000, &d->font[c]);
            cx += 8;
        }
    }
}

static bool button_on_event(node_t *self, const gui_event_t *e) {
    button_data_t *d = (button_data_t *)self->userdata;
    if (!d) return false;

    if (e->type == GUI_EVENT_MOUSE_MOVE) {
        bool inside = rect_contains_point(self->screen_bounds, e->mouse.x, e->mouse.y);
        if (inside != d->hovered) {
            d->hovered = inside;
            uint32_t target_color = inside ? theme_engine_get_color(THEME_COLOR_BUTTON_BG_HOVER) : theme_engine_get_color(THEME_COLOR_BUTTON_BG);
            if (d->pressed) target_color = theme_engine_get_color(THEME_COLOR_BUTTON_BG_PRESS);
            animation_start(self, ANIM_PROP_COLOR, &d->current_bg_color, d->current_bg_color, target_color, 15);
        }
    } else if (e->type == GUI_EVENT_MOUSE_DOWN && e->mouse.button == 1) {
        if (d->hovered) {
            d->pressed = true;
            animation_start(self, ANIM_PROP_COLOR, &d->current_bg_color, d->current_bg_color, theme_engine_get_color(THEME_COLOR_BUTTON_BG_PRESS), 5);
            return true;
        }
    } else if (e->type == GUI_EVENT_MOUSE_UP && e->mouse.button == 1) {
        if (d->pressed) {
            d->pressed = false;
            uint32_t target_color = d->hovered ? theme_engine_get_color(THEME_COLOR_BUTTON_BG_HOVER) : theme_engine_get_color(THEME_COLOR_BUTTON_BG);
            animation_start(self, ANIM_PROP_COLOR, &d->current_bg_color, d->current_bg_color, target_color, 15);
            
            /* Double-click detection: if last click was within 400ms */
            extern uint32_t timer_ticks;
            uint32_t now = timer_ticks;
            if (d->on_double_click && d->hovered && (now - d->last_click_time) <= 40) {  // 100Hz ticks, 40 = ~400ms
                d->on_double_click(self, d->double_click_ud);
            } else if (d->hovered && d->on_click) {
                d->on_click(self, d->click_ud);
            }
            d->last_click_time = now;
            return true;
        }
    }
    return false;
}

static void button_destroy(node_t *self) {
    if (self->userdata) {
        button_data_t *d = (button_data_t *)self->userdata;
        if (d->text) kfree(d->text);
        kfree(d);
        self->userdata = NULL;
    }
}

static const node_vtable_t button_vtable = {
    .draw     = button_draw,
    .on_event = button_on_event,
    .layout   = NULL,
    .destroy  = button_destroy,
};

node_t *button_create(const char *name, int x, int y, int w, int h, const char *text) {
    node_t *n = node_create(NODE_BUTTON, name);
    if (!n) return NULL;

    button_data_t *d = (button_data_t *)kmalloc(sizeof(button_data_t));
    if (!d) { node_destroy(n); return NULL; }
    memset(d, 0, sizeof(button_data_t));

    if (text) {
        d->text = (char *)kmalloc(strlen(text) + 1);
        strcpy(d->text, text);
    }
    
    d->current_bg_color = theme_engine_get_color(THEME_COLOR_BUTTON_BG);
    
    n->userdata = d;
    n->vtable = &button_vtable;
    n->local_x = x;
    n->local_y = y;
    n->width = w;
    n->height = h;
    n->interactive = true;

    return n;
}

void button_set_text(node_t *button, const char *text) {
    if (button && button->type == NODE_BUTTON) {
        button_data_t *d = (button_data_t *)button->userdata;
        if (d->text) kfree(d->text);
        if (text) {
            d->text = (char *)kmalloc(strlen(text) + 1);
            strcpy(d->text, text);
        } else {
            d->text = NULL;
        }
        node_mark_dirty(button, NODE_DIRTY_PAINT);
    }
}

void button_set_on_click(node_t *button, button_click_cb_t cb, void *userdata) {
    if (button && button->type == NODE_BUTTON) {
        button_data_t *d = (button_data_t *)button->userdata;
        d->on_click = cb;
        d->click_ud = userdata;
    }
}

void button_set_on_double_click(node_t *button, button_click_cb_t cb, void *userdata) {
    if (button && button->type == NODE_BUTTON) {
        button_data_t *d = (button_data_t *)button->userdata;
        d->on_double_click = cb;
        d->double_click_ud = userdata;
    }
}

void button_set_highlight(node_t *button, bool highlighted) {
    if (!button || button->type != NODE_BUTTON || !button->userdata) return;
    button_data_t *d = (button_data_t *)button->userdata;
    d->hovered = highlighted;
    uint32_t target_color = highlighted ? theme_engine_get_color(THEME_COLOR_BUTTON_BG_HOVER) : theme_engine_get_color(THEME_COLOR_BUTTON_BG);
    animation_start(button, ANIM_PROP_COLOR, &d->current_bg_color, d->current_bg_color, target_color, 15);
}
