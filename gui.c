#include "gui.h"
#include "video.h"
#include "kheap.h"
#include "mouse.h"
#include "string.h"
#include "lgx.h"

extern uint32_t screen_width, screen_height;
extern uint32_t* backbuffer;
extern uint32_t* framebuffer;
extern lg_device_t global_lg_device;
extern lg_queue_t global_lg_queue;
extern lg_command_pool_t global_lg_pool;
extern lg_swapchain_t global_sw;

static lg_command_buffer_t gui_persistent_cmd = NULL;
bool gui_is_dirty = true;
static int last_gui_mx = 0, last_gui_my = 0;

void gui_mark_dirty() { 
    gui_is_dirty = true;
}

void gui_handle_mouse_update(int mx, int my) {
    (void)mx; (void)my;
    // O mouse agora é desenhado em overlay absoluto
}

#define COLOR_WIN     0xFFFFFF
#define COLOR_TITLE   0xE0E0E0
#define COLOR_INACTIVE 0xF0F0F0
#define COLOR_TXT     0x333333
#define COLOR_BG      0x222222

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
    win->lg_image = NULL;

    if (global_lg_device) {
        lg_image_create_info_t img_info = {(uint32_t)w, (uint32_t)h, LGX_FORMAT_B8G8R8A8_UNORM, LGX_IMAGE_USAGE_SAMPLED_BIT};
        lg_create_image(global_lg_device, &img_info, &win->lg_image);
        extern void lg_set_image_raw_ptr(lg_image_t image, void* ptr);
        lg_set_image_raw_ptr(win->lg_image, win->backing_store);
    }

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
        extern void draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
        extern void draw_filled_circle(int xm, int ym, int r, uint32_t color);
        draw_rounded_rect(abs_x, abs_y, w->w, w->h, 10, COLOR_WIN);
        uint32_t t_col = w->focused ? COLOR_TITLE : COLOR_INACTIVE;
        draw_rounded_rect(abs_x, abs_y, w->w, 30, 10, t_col);
        draw_rect(abs_x, abs_y + 20, w->w, 10, t_col);
        draw_string(abs_x + (w->w/2) - (strlen(w->text)*4), abs_y + 8, w->text, COLOR_TXT);
        
        // Traffic Lights - Vermelho (Fechar), Amarelo (Minimizar), Verde (Maximizar)
        draw_filled_circle(abs_x + 15, abs_y + 15, 6, 0xFF5F57);
        draw_filled_circle(abs_x + 35, abs_y + 15, 6, 0xFEBC2E);
        draw_filled_circle(abs_x + 55, abs_y + 15, 6, 0x28C840);

        if (ev->type == EVENT_MOUSE_CLICK) {
            // Check Close
            if (is_inside(ev->mx, ev->my, abs_x + 15 - 8, abs_y + 15 - 8, 16, 16)) { w->visible = false; gui_mark_dirty(); return; }
            // Check Min
            if (is_inside(ev->mx, ev->my, abs_x + 35 - 8, abs_y + 15 - 8, 16, 16)) { w->minimized = true; gui_mark_dirty(); return; }
            // Check Max
            if (is_inside(ev->mx, ev->my, abs_x + 55 - 8, abs_y + 15 - 8, 16, 16)) {
                if (!w->maximized) {
                    w->old_x = w->x; w->old_y = w->y; w->old_w = w->w; w->old_h = w->h;
                    w->x = 0; w->y = 0; w->w = screen_width; w->h = screen_height - 50;
                    w->maximized = true;
                } else {
                    w->x = w->old_x; w->y = w->old_y; w->w = w->old_w; w->h = w->old_h;
                    w->maximized = false;
                }
                w->dirty = true; gui_mark_dirty(); return;
            }
        }

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

void gui_render_all(widget_t* root_widgets[], int count, event_t* ev) {
    int mx = get_mouse_x(), my = get_mouse_y();
    bool clicked = is_left_clicked();

    // 1. PROCESSAMENTO DE EVENTOS (Crítico: Antes do desenho)
    if (ev->type == EVENT_MOUSE_CLICK || ev->type == EVENT_KEY_PRESS) {
        int top_idx = -1;
        for (int i = count - 1; i >= 0; i--) {
            widget_t* w = root_widgets[i];
            if (w && w->visible && !w->minimized && is_inside(mx, my, w->x, w->y, w->w, w->h)) {
                top_idx = i; break;
            }
        }
        if (top_idx != -1) {
            widget_t* active = root_widgets[top_idx];
            for (int j = top_idx; j < count - 1; j++) root_widgets[j] = root_widgets[j+1];
            root_widgets[count - 1] = active;
            active->focused = true; active->dirty = true; gui_mark_dirty();

            if (ev->type == EVENT_MOUSE_CLICK && is_inside(mx, my, active->x, active->y, active->w, 30)) {
                active->is_dragging = true;
                active->drag_off_x = mx - active->x;
                active->drag_off_y = my - active->y;
            }
        }
    }

    if (root_widgets[count-1] && root_widgets[count-1]->is_dragging) {
        if (clicked) {
            root_widgets[count-1]->x = mx - root_widgets[count-1]->drag_off_x;
            root_widgets[count-1]->y = my - root_widgets[count-1]->drag_off_y;
            gui_mark_dirty();
        } else { root_widgets[count-1]->is_dragging = false; }
    }

    // 2. COMPOSIÇÃO LGX
    if (gui_is_dirty) {
        for (int i = 0; i < count; i++) {
            widget_t* w = root_widgets[i];
            if (!w || !w->visible || w->minimized) continue;
            w->focused = (i == count - 1);
            if (w->dirty || (w->focused && ev->type != EVENT_NONE)) {
                video_set_target(w->backing_store, w->w, w->h);
                clear_screen(0xFFFFFFFF);
                event_t local_ev = *ev;
                local_ev.mx -= w->x; local_ev.my -= w->y;
                render_widget(w, -w->x, -w->y, &local_ev);
                w->dirty = false;
            }
        }

        video_set_target(backbuffer, screen_width, screen_height);
        clear_screen(COLOR_BG);

        if (!gui_persistent_cmd) {
            lg_command_buffer_allocate_info_t cb_info = {global_lg_pool, 1};
            lg_allocate_command_buffers(global_lg_device, &cb_info, &gui_persistent_cmd);
        }
        lg_begin_command_buffer(gui_persistent_cmd, NULL);
        lg_image_t sw_img = lg_get_swapchain_image(global_sw, 0);
        for (int i = 0; i < count; i++) {
            widget_t* w = root_widgets[i];
            if (w && w->visible && !w->minimized && w->lg_image) {
                lg_cmd_copy_image(gui_persistent_cmd, w->lg_image, sw_img, w->x, w->y);
            }
        }
        lg_end_command_buffer(gui_persistent_cmd);
        lg_submit_info_t submit = {1, &gui_persistent_cmd};
        lg_queue_submit(global_lg_queue, 1, &submit);

        extern void draw_dock(); draw_dock();
        gui_is_dirty = false;
    }

    // 3. APRESENTAÇÃO (Final do frame)
    refresh_screen(); 

    // Mouse Overlay (Camada superior absoluta)
    video_set_target(framebuffer, screen_width, screen_height);
    draw_mouse_cursor(mx, my, 0);
    draw_string(5, 5, "LiwusOS LGX Compositor", 0xFFFFFF);
    video_reset_target();
    
    last_gui_mx = mx; last_gui_my = my;
}