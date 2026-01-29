#ifndef GUI_H
#define GUI_H

#include <stdint.h>
#include <stdbool.h>
#include "events.h"

typedef enum { TYPE_WINDOW, TYPE_BUTTON, TYPE_LABEL } widget_type_t;

struct widget;
typedef void (*on_click_listener)(struct widget* self);

typedef struct widget {
    widget_type_t type;
    int x, y, w, h;
    int old_x, old_y, old_w, old_h;
    const char* text;
    uint32_t color;
    bool visible;
    bool focused;
    bool maximized;
    bool minimized;
    
    // Compositor Support
    uint32_t* backing_store;
    bool dirty; // Does the surface need redrawing?
    
    bool is_dragging;
    int drag_off_x, drag_off_y;
    int drag_visual_x, drag_visual_y; // New: visual position of the wireframe
    struct widget* children[15];
    int child_count;

    on_click_listener on_click;
} widget_t;

void draw_button_visual(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const char* label, uint32_t color);
widget_t* create_window(const char* title, int x, int y, int w, int h);
void add_widget(widget_t* parent, widget_t* child);
widget_t* create_button(const char* label, int x, int y, int w, int h, on_click_listener listener);
widget_t* create_label(const char* text, int x, int y, uint32_t color);
bool is_inside(int mx, int my, int x, int y, int w, int h);

void gui_mark_dirty();
void gui_handle_mouse_update(int mx, int my);

void gui_render_all(widget_t* root_widgets[], int count, event_t* ev);

#endif
