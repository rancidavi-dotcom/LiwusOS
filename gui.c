#include "gui.h"
#include "video.h"
#include "kheap.h"
#include "mouse.h"
#include "string.h"

extern uint32_t screen_width, screen_height;

bool gui_is_dirty = true;
static rect_t damage_list[128];
static int damage_count = 0;

void gui_invalidate_rect(rect_t r) {
    if (r.w <= 0 || r.h <= 0) return;
    if (damage_count < 128) damage_list[damage_count++] = r;
    gui_is_dirty = true;
}

void gui_mark_dirty() { 
    damage_count = 0; 
    gui_invalidate_rect((rect_t){0, 0, (int)screen_width, (int)screen_height}); 
}

static int last_gui_mx = 0, last_gui_my = 0;
void gui_handle_mouse_update(int mx, int my) {
    if (mx != last_gui_mx || my != last_gui_my) {
        gui_invalidate_rect((rect_t){last_gui_mx - 2, last_gui_my - 2, 28, 28});
        gui_invalidate_rect((rect_t){mx - 2, my - 2, 28, 28});
        last_gui_mx = mx;
        last_gui_my = my;
    }
}

#define COLOR_WIN     0xFFFFFF
#define COLOR_TITLE   0xE0E0E0
#define COLOR_INACTIVE 0xF0F0F0
#define COLOR_TXT     0x333333
#define COLOR_BG      0x222222 // Neutral Dark Gray

bool is_inside(int mx, int my, int x, int y, int w, int h) {
    return (mx >= x && mx <= x + w && my >= y && my <= y + h);
}

widget_t* create_window(const char* title, int x, int y, int w, int h) {
    widget_t* win = (widget_t*)kmalloc(sizeof(widget_t));
    win->type = TYPE_WINDOW; win->x = x; win->y = y; win->w = w; win->h = h;
    win->old_x = x; win->old_y = y; win->old_w = w; win->old_h = h;
    win->text = title; win->visible = true; win->focused = false;
    win->maximized = false; win->minimized = false; win->is_dragging = false;
    win->child_count = 0;
    win->backing_store = (uint32_t*)kmalloc(w * h * 4);
    win->dirty = true;
    return win;
}

widget_t* create_button(const char* label, int x, int y, int w, int h, on_click_listener listener) {
    widget_t* btn = (widget_t*)kmalloc(sizeof(widget_t));
    btn->type = TYPE_BUTTON; btn->x = x; btn->y = y; btn->w = w; btn->h = h;
    btn->text = label; btn->color = 0xEEEEEE; btn->on_click = listener; btn->visible = true;
    return btn;
}

widget_t* create_label(const char* text, int x, int y, uint32_t color) {
    widget_t* lbl = (widget_t*)kmalloc(sizeof(widget_t));
    lbl->type = TYPE_LABEL; lbl->x = x; lbl->y = y;
    lbl->text = text; lbl->color = color; lbl->visible = true;
    return lbl;
}

void add_widget(widget_t* parent, widget_t* child) {
    if (parent && child && parent->child_count < 15) parent->children[parent->child_count++] = child;
}

void draw_button_visual(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* label, uint32_t color) {
    draw_rect(x, y, w, h, color);
    draw_rect(x, y, w, 1, 0xBBBBBB);
    draw_string(x + 10, y + (h/2) - 8, label, 0x000000);
}

void render_widget(widget_t* w, int ox, int oy, event_t* ev) {
    if (!w->visible || w->minimized) return;
    int abs_x = ox + w->x; int abs_y = oy + w->y;

    if (w->type == TYPE_WINDOW) {
        int close_x = abs_x + 15;
        int min_x   = abs_x + 35;
        int max_x   = abs_x + 55;

        if (ev->type == EVENT_MOUSE_CLICK) {
            if (is_inside(ev->mx, ev->my, close_x-6, abs_y+15-6, 12, 12)) { w->visible = false; gui_mark_dirty(); return; }
            if (is_inside(ev->mx, ev->my, min_x-6, abs_y+15-6, 12, 12))   { w->minimized = true; gui_mark_dirty(); return; }
            if (is_inside(ev->mx, ev->my, max_x-6, abs_y+15-6, 12, 12))   {
                if (!w->maximized) {
                    w->old_x = w->x; w->old_y = w->y; w->old_w = w->w; w->old_h = w->h;
                    w->x = 0; w->y = 0; w->w = screen_width; w->h = screen_height - 50;
                    w->maximized = true;
                } else {
                    w->x = w->old_x; w->y = w->old_y; w->w = w->old_w; w->h = w->old_h;
                    w->maximized = false;
                }
                w->dirty = true;
                gui_mark_dirty();
                return;
            }
        }

        extern void draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
        draw_rounded_rect(abs_x, abs_y, w->w, w->h, 10, COLOR_WIN);
        uint32_t t_col = w->focused ? COLOR_TITLE : COLOR_INACTIVE;
        draw_rounded_rect(abs_x, abs_y, w->w, 30, 10, t_col);
        draw_rect(abs_x, abs_y + 20, w->w, 10, t_col);
        draw_string(abs_x + (w->w/2) - (strlen(w->text)*4), abs_y + 8, w->text, COLOR_TXT);
        extern void draw_filled_circle(int xm, int ym, int r, uint32_t color);
        draw_filled_circle(close_x, abs_y + 15, 6, 0xFF5F57);
        draw_filled_circle(min_x, abs_y + 15, 6, 0xFEBC2E);
        draw_filled_circle(max_x, abs_y + 15, 6, 0x28C840);

        set_clip(abs_x, abs_y + 30, w->w, w->h - 30);
        for (int i=0; i < w->child_count; i++) render_widget(w->children[i], abs_x, abs_y + 30, ev);
        reset_clip();
    } 
    else if (w->type == TYPE_BUTTON) {
        draw_button_visual(abs_x, abs_y, w->w, w->h, w->text, w->color);
        if (ev->type == EVENT_MOUSE_CLICK && is_inside(ev->mx, ev->my, abs_x, abs_y, w->w, w->h)) {
            if (w->on_click) { w->on_click(w); w->dirty = true; gui_mark_dirty(); }
        }
    }
    else if (w->type == TYPE_LABEL) draw_string(abs_x, abs_y, w->text, w->color);
}

void draw_debug_overlay(widget_t* focused, int count) {
    draw_rect(0, 0, 200, 60, 0x000000);
    draw_string(5, 5, "LiwusOS GUI Debug", 0xFFFFFF);
    if (focused) draw_string(5, 20, "Focused Window OK", 0x00FF00);
    else draw_string(5, 20, "No Focus", 0xFF0000);
    if (gui_is_dirty) draw_string(5, 35, "Dirty: YES", 0xFFFF00);
    else draw_string(5, 35, "Dirty: NO", 0x00FF00);
    for(int i=0; i<count; i++) draw_pixel(5 + i*2, 50, 0xFFFFFF);
}

void gui_render_all(widget_t* root_widgets[], int count, event_t* ev) {
    if (gui_is_dirty && damage_count == 0) gui_mark_dirty();

    if (ev->type == EVENT_MOUSE_CLICK) {
        int top_idx = -1;
        for (int i = count - 1; i >= 0; i--) {
            widget_t* w = root_widgets[i];
            if (w && w->visible && !w->minimized && is_inside(ev->mx, ev->my, w->x, w->y, w->w, w->h)) {
                top_idx = i; break;
            }
        }
        if (top_idx != -1) {
            widget_t* active = root_widgets[top_idx];
            if (!active->focused) {
                active->dirty = true;
                gui_invalidate_rect((rect_t){active->x, active->y, active->w, active->h});
            }
            for (int j = top_idx; j < count - 1; j++) root_widgets[j] = root_widgets[j+1];
            root_widgets[count - 1] = active;
            
            active->is_dragging = true;
            active->drag_visual_x = active->x;
            active->drag_visual_y = active->y;
            active->drag_off_x = ev->mx - active->x;
            active->drag_off_y = ev->my - active->y;
            active->dirty = true;
        }
    }

    widget_t* focused = (count > 0) ? root_widgets[count - 1] : (void*)0;
    if (focused && focused->is_dragging) {
        if (is_left_clicked()) {
            int nvx = ev->mx - focused->drag_off_x;
            int nvy = ev->my - focused->drag_off_y;
            if (nvx != focused->drag_visual_x || nvy != focused->drag_visual_y) {
                gui_invalidate_rect((rect_t){focused->drag_visual_x, focused->drag_visual_y, focused->w, focused->h});
                focused->drag_visual_x = nvx; focused->drag_visual_y = nvy;
                gui_invalidate_rect((rect_t){focused->drag_visual_x, focused->drag_visual_y, focused->w, focused->h});
            }
        } else {
            gui_invalidate_rect((rect_t){focused->x, focused->y, focused->w, focused->h});
            focused->x = focused->drag_visual_x; focused->y = focused->drag_visual_y;
            gui_invalidate_rect((rect_t){focused->x, focused->y, focused->w, focused->h});
            focused->is_dragging = false; focused->dirty = true;
        }
    }

    for (int i = 0; i < count; i++) {
        widget_t* w = root_widgets[i];
        if (!w || !w->visible || w->minimized) continue;
        w->focused = (i == count - 1);
        if (w->dirty || (w->focused && (ev->type == EVENT_KEY_PRESS || ev->type == EVENT_MOUSE_CLICK))) {
            video_set_target(w->backing_store, w->w, w->h);
            clear_screen(0);
            event_t local_ev = *ev;
            local_ev.mx -= w->x; local_ev.my -= w->y;
            if (!w->focused) local_ev.type = EVENT_NONE;
            render_widget(w, -w->x, -w->y, &local_ev);
            video_reset_target();
            w->dirty = false;
            gui_invalidate_rect((rect_t){w->x, w->y, w->w, w->h});
        }
    }

    if (damage_count > 0) {
        extern rect_t video_rect_intersect(rect_t a, rect_t b);
        extern void draw_dock();
        for (int d = 0; d < damage_count; d++) {
            rect_t dr = damage_list[d];
            set_clip(dr.x, dr.y, dr.w, dr.h);
            draw_rect(dr.x, dr.y, dr.w, dr.h, COLOR_BG);
            for (int i = 0; i < count; i++) {
                widget_t* w = root_widgets[i];
                if (!w || !w->visible || w->minimized) continue;
                rect_t win_r = {w->x, w->y, w->w, w->h};
                rect_t inter = video_rect_intersect(dr, win_r);
                if (inter.w > 0 && inter.h > 0) {
                    video_blit(w->backing_store, w->w, inter.x - w->x, inter.y - w->y, inter.w, inter.h, inter.x, inter.y);
                }
            }
            if (focused && focused->is_dragging) {
                int wx = focused->drag_visual_x, wy = focused->drag_visual_y, ww = focused->w, wh = focused->h;
                draw_rect(wx, wy, ww, 2, 0xFFFFFF);
                draw_rect(wx, wy + wh - 2, ww, 2, 0xFFFFFF);
                draw_rect(wx, wy, 2, wh, 0xFFFFFF);
                draw_rect(wx + ww - 2, wy, 2, wh, 0xFFFFFF);
            }
            // Draw dock in damage rect if they overlap
            draw_dock();
            video_refresh_rect(dr);
        }
        damage_count = 0;
        reset_clip();
    }
    
    int mx = get_mouse_x(), my = get_mouse_y();
    draw_mouse_cursor(mx, my, 0);
    draw_debug_overlay(focused, count);
    video_refresh_rect((rect_t){0, 0, 200, 60});
    video_refresh_rect((rect_t){mx, my, 24, 24});
    // Refresh dock separately to be sure
    video_refresh_rect((rect_t){(int)(screen_width-600)/2, (int)screen_height-50, 600, 50});
    gui_is_dirty = false;
}