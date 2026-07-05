#include "lgx.h"
#include "vmm.h"
#include "string.h"
#include "mouse.h"
#include "keyboard.h"
#include "gpu.h"
#include "wallpaper.h"
#include "ui_assets.h"
#include "timer.h"

extern uint64_t vga_fb_addr;
extern uint32_t vga_fb_width;
extern uint32_t vga_fb_height;
extern uint32_t vga_fb_pitch;
extern uint8_t vga_fb_bpp;

struct VideoMode current_video_mode;

static uint32_t* backbuffer = NULL;
static uint32_t fb_size_bytes = 0;

window_t* window_list_head = NULL;
static window_t* window_list_tail = NULL;
static uint32_t next_window_id = 1;

static bool lgx_is_dragging = false;
static window_t *lgx_drag_window = NULL;
static int lgx_drag_offset_x = 0;
static int lgx_drag_offset_y = 0;
static bool lgx_prev_click = false;
static bool lgx_prev_right_click = false;

// Radial Menu State
static bool radial_menu_open = false;
static int radial_menu_x = 0;
static int radial_menu_y = 0;
static window_t* radial_menu_target_win = NULL;

bool lgx_app_menu_open = false;
static int app_menu_selected = -1; // -1 means none selected by keyboard initially
static uint32_t lgx_last_click_ticks = 0;
static window_t* lgx_last_clicked_win = NULL;
const char *lgx_error_popup_msg = NULL;
extern const char *get_launch_last_error();
// Infinite Canvas Camera
int camera_x = 0;
int camera_y = 0;
float camera_zoom = 1.0f;
bool lgx_dragging_bg = false;
int lgx_drag_bg_start_cx = 0;
int lgx_drag_bg_start_cy = 0;
int lgx_drag_bg_start_mx = 0;
int lgx_drag_bg_start_my = 0;

// Wallpaper asset (from Asset Pipeline)
#include "wallpaper.h"

// Alpha blending helper
static inline uint32_t blend_color(uint32_t bg, uint32_t fg) {
    uint8_t alpha = (fg >> 24) & 0xFF;
    if (alpha == 0) return bg;
    if (alpha == 255) return fg;
    
    uint32_t rb = bg & 0xFF00FF;
    uint32_t g  = bg & 0x00FF00;
    uint32_t fg_rb = fg & 0xFF00FF;
    uint32_t fg_g  = fg & 0x00FF00;
    
    uint32_t out_rb = rb + ((fg_rb - rb) * alpha) / 256;
    uint32_t out_g  = g + ((fg_g - g) * alpha) / 256;
    
    return (out_rb & 0xFF00FF) | (out_g & 0x00FF00);
}

void graphics_blit(uint32_t* dest_buffer, int dest_pitch, int dest_x, int dest_y, 
                   uint32_t* src_buffer, int src_w, int src_h, int src_pitch, 
                   int src_x, int src_y, int width, int height) {
    // 1. Clip Rectangles
    if (dest_x < 0) {
        width += dest_x;
        src_x -= dest_x;
        dest_x = 0;
    }
    if (dest_y < 0) {
        height += dest_y;
        src_y -= dest_y;
        dest_y = 0;
    }
    if (dest_x + width > (int)current_video_mode.width) width = current_video_mode.width - dest_x;
    if (dest_y + height > (int)current_video_mode.height) height = current_video_mode.height - dest_y;
    
    if (src_x < 0) {
        width += src_x;
        dest_x -= src_x;
        src_x = 0;
    }
    if (src_y < 0) {
        height += src_y;
        dest_y -= src_y;
        src_y = 0;
    }
    if (src_x + width > src_w) width = src_w - src_x;
    if (src_y + height > src_h) height = src_h - src_y;
    
    if (width <= 0 || height <= 0) return;
    
    // 2. Cópia / Blit
    for (int y = 0; y < height; y++) {
        uint32_t* dst_row = (uint32_t*)((uint8_t*)dest_buffer + (dest_y + y) * dest_pitch);
        uint32_t* src_row = (uint32_t*)((uint8_t*)src_buffer + (src_y + y) * src_pitch);
        
        for (int x = 0; x < width; x++) {
            uint32_t src_pixel = src_row[src_x + x];
            uint8_t alpha = (src_pixel >> 24) & 0xFF;
            if (alpha == 255) {
                dst_row[dest_x + x] = src_pixel;
            } else if (alpha > 0) {
                dst_row[dest_x + x] = blend_color(dst_row[dest_x + x], src_pixel);
            }
        }
    }
}

void lgx_init(void) {
    if (vga_fb_addr == 0) return;
    
    current_video_mode.vram_address = (uint32_t*)vga_fb_addr;
    current_video_mode.width = vga_fb_width;
    current_video_mode.height = vga_fb_height;
    current_video_mode.pitch = vga_fb_pitch;
    current_video_mode.bpp = vga_fb_bpp;
    
    fb_size_bytes = current_video_mode.pitch * current_video_mode.height;
    backbuffer = (uint32_t*)kmalloc(fb_size_bytes);
    
    if (backbuffer) {
        memset(backbuffer, 0, fb_size_bytes);
    }
}

window_t* window_create(int x, int y, int width, int height, uint32_t flags) {
    window_t* win = (window_t*)kmalloc(sizeof(window_t));
    if (!win) return NULL;
    
    win->id = next_window_id++;
    win->x = x;
    win->y = y;
    win->width = width;
    win->height = height;
    win->flags = flags;
    win->buffer = (uint32_t*)kmalloc(width * height * 4);
    if (win->buffer) {
        memset(win->buffer, 0, width * height * 4);
    }
    
    win->next = NULL;
    win->prev = window_list_tail;
    if (window_list_tail) {
        window_list_tail->next = win;
    } else {
        window_list_head = win;
    }
    window_list_tail = win;
    
    return win;
}

void window_bring_to_front(window_t* win) {
    if (!win || win == window_list_tail) return; // Already at front
    
    // Remove from current position
    if (win->prev) win->prev->next = win->next;
    else window_list_head = win->next;
    
    if (win->next) win->next->prev = win->prev;
    
    // Append to tail
    win->prev = window_list_tail;
    win->next = NULL;
    if (window_list_tail) window_list_tail->next = win;
    window_list_tail = win;
    
    if (!window_list_head) window_list_head = win;
}

void window_destroy(window_t* win) {
    if (!win) return;
    
    if (win->prev) win->prev->next = win->next;
    else window_list_head = win->next;
    
    if (win->next) win->next->prev = win->prev;
    else window_list_tail = win->prev;
    
    if (lgx_drag_window == win) {
        lgx_is_dragging = false;
        lgx_drag_window = NULL;
    }
    
    if (win->buffer) kfree(win->buffer);
    kfree(win);
}

static inline uint32_t graphics_alpha_blend(uint32_t bg, uint32_t fg) {
    uint32_t a = (fg >> 24) & 0xFF;
    if (a == 255) return fg;
    if (a == 0) return bg;
    
    uint32_t inv_a = 255 - a;
    uint32_t r = (((fg >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv_a) >> 8;
    uint32_t g = (((fg >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * inv_a) >> 8;
    uint32_t b = ((fg & 0xFF) * a + (bg & 0xFF) * inv_a) >> 8;
    
    return (0xFF000000) | (r << 16) | (g << 8) | b;
}

static void graphics_blit_scaled(uint32_t* dest, uint32_t dest_pitch, int dest_x, int dest_y,
                                 const uint32_t* src, int src_w, int src_h,
                                 float zoom) {
    int scaled_w = (int)(src_w * zoom);
    int scaled_h = (int)(src_h * zoom);
    
    int start_x = dest_x < 0 ? -dest_x : 0;
    int start_y = dest_y < 0 ? -dest_y : 0;
    int end_x = dest_x + scaled_w > (int)current_video_mode.width ? (int)current_video_mode.width - dest_x : scaled_w;
    int end_y = dest_y + scaled_h > (int)current_video_mode.height ? (int)current_video_mode.height - dest_y : scaled_h;

    for (int y = start_y; y < end_y; y++) {
        int src_y = (int)(y / zoom);
        if (src_y >= src_h) src_y = src_h - 1;
        uint32_t* dest_row = dest + ((dest_y + y) * (dest_pitch / 4));
        const uint32_t* src_row = src + (src_y * src_w);
        
        for (int x = start_x; x < end_x; x++) {
            int src_x = (int)(x / zoom);
            if (src_x >= src_w) src_x = src_w - 1;
            
            uint32_t color = src_row[src_x];
            if (color >> 24) {
                dest_row[dest_x + x] = graphics_alpha_blend(dest_row[dest_x + x], color);
            }
        }
    }
}

static void lgx_draw_background() {
    // 1. Fill solid color (Slate 900)
    uint32_t *p = backbuffer;
    uint32_t count = current_video_mode.height * (current_video_mode.pitch/4);
    while (count--) {
        *p++ = 0xFF0F172A;
    }

    // 2. Draw a dot grid (Slate 700)
    int dot_spacing = (int)(40 * camera_zoom);
    if (dot_spacing < 5) dot_spacing = 5; // prevent infinite loops
    int start_x = (int)(-camera_x * camera_zoom) % dot_spacing;
    if (start_x > 0) start_x -= dot_spacing;
    int start_y = (int)(-camera_y * camera_zoom) % dot_spacing;
    if (start_y > 0) start_y -= dot_spacing;

    for (int y = start_y; y < (int)current_video_mode.height; y += dot_spacing) {
        if (y < 0 || y >= (int)current_video_mode.height) continue;
        for (int x = start_x; x < (int)current_video_mode.width; x += dot_spacing) {
            if (x < 0 || x >= (int)current_video_mode.width) continue;
            backbuffer[y * (current_video_mode.pitch/4) + x] = 0xFF334155;
        }
    }

    // 3. Draw wallpaper only once at the origin
    graphics_blit_scaled(backbuffer, current_video_mode.pitch, 
                         (int)(-camera_x * camera_zoom), (int)(-camera_y * camera_zoom),
                         (uint32_t*)wallpaper_data, wallpaper_width, wallpaper_height, camera_zoom);
}

static void lgx_draw_cursor(int mx, int my) {
    static const uint8_t cursor_pattern[12] = {
        0b10000000, 0b11000000, 0b11100000, 0b11110000,
        0b11111000, 0b11111100, 0b11111110, 0b11111111,
        0b11111000, 0b10111000, 0b00011000, 0b00011000
    };
    
    for (int y = 0; y < 12; y++) {
        if (my + y >= (int)current_video_mode.height) break;
        uint8_t row = cursor_pattern[y];
        for (int x = 0; x < 8; x++) {
            if (mx + x >= (int)current_video_mode.width) break;
            if (row & (1 << (7 - x))) {
                uint32_t offset = ((my + y) * (current_video_mode.pitch/4)) + (mx + x);
                backbuffer[offset] = 0xFFFFFFFF; // Opaque white
            }
        }
    }
}

static void lgx_draw_minimap() {
    int radar_w = 160;
    int radar_h = 120;
    int radar_x = current_video_mode.width - radar_w - 20;
    int radar_y = current_video_mode.height - radar_h - 20;
    
    // Background 50% opacity
    uint32_t bg_color = 0x800F172A; // Slate 900
    for (int y = 0; y < radar_h; y++) {
        for (int x = 0; x < radar_w; x++) {
            uint32_t offset = (radar_y + y) * (current_video_mode.pitch/4) + (radar_x + x);
            backbuffer[offset] = graphics_alpha_blend(backbuffer[offset], bg_color);
        }
    }
    
    // Map scale: assumes an infinite canvas of roughly 8000x6000 bounds for the minimap
    float map_scale_x = (float)radar_w / 8000.0f;
    float map_scale_y = (float)radar_h / 6000.0f;
    
    // Draw windows as small rectangles
    window_t* win = window_list_head;
    while(win) {
        int wx = radar_x + radar_w/2 + (int)(win->x * map_scale_x);
        int wy = radar_y + radar_h/2 + (int)(win->y * map_scale_y);
        int ww = (int)(win->width * map_scale_x);
        int wh = (int)(win->height * map_scale_y);
        if (ww < 2) ww = 2;
        if (wh < 2) wh = 2;
        
        for (int y = 0; y < wh; y++) {
            for (int x = 0; x < ww; x++) {
                if (wx+x >= radar_x && wx+x < radar_x+radar_w && wy+y >= radar_y && wy+y < radar_y+radar_h) {
                    backbuffer[(wy+y)*(current_video_mode.pitch/4) + (wx+x)] = 0xFF94A3B8; // Slate 400
                }
            }
        }
        win = win->next;
    }
    
    // Draw camera viewport outline
    int vx = radar_x + radar_w/2 + (int)(camera_x * map_scale_x);
    int vy = radar_y + radar_h/2 + (int)(camera_y * map_scale_y);
    int vw = (int)((current_video_mode.width / camera_zoom) * map_scale_x);
    int vh = (int)((current_video_mode.height / camera_zoom) * map_scale_y);
    
    for (int y = 0; y < vh; y++) {
        for (int x = 0; x < vw; x++) {
            if (y == 0 || y == vh-1 || x == 0 || x == vw-1) {
                if (vx+x >= radar_x && vx+x < radar_x+radar_w && vy+y >= radar_y && vy+y < radar_y+radar_h) {
                    backbuffer[(vy+y)*(current_video_mode.pitch/4) + (vx+x)] = 0xFF3B82F6; // Blue 500
                }
            }
        }
    }
}

static void lgx_draw_offscreen_indicators() {
    window_t* win = window_list_head;
    while(win) {
        int win_screen_x = (int)((win->x - camera_x) * camera_zoom);
        int win_screen_y = (int)((win->y - camera_y) * camera_zoom);
        int win_screen_w = (int)(win->width * camera_zoom);
        int win_screen_h = (int)(win->height * camera_zoom);
        
        bool is_off_left = (win_screen_x + win_screen_w < 0);
        bool is_off_right = (win_screen_x >= (int)current_video_mode.width);
        bool is_off_top = (win_screen_y + win_screen_h < 0);
        bool is_off_bottom = (win_screen_y >= (int)current_video_mode.height);
        
        if (is_off_left || is_off_right || is_off_top || is_off_bottom) {
            // Center of window in screen space
            int cx = win_screen_x + win_screen_w / 2;
            int cy = win_screen_y + win_screen_h / 2;
            
            int ind_x = cx;
            int ind_y = cy;
            
            // Clamp to edges
            if (ind_x < 5) ind_x = 5;
            if (ind_x > (int)current_video_mode.width - 15) ind_x = current_video_mode.width - 15;
            if (ind_y < 5) ind_y = 5;
            if (ind_y > (int)current_video_mode.height - 15) ind_y = current_video_mode.height - 15;
            
            uint32_t color = 0xFFF59E0B; // Amber 500
            for (int y = 0; y < 10; y++) {
                for (int x = 0; x < 10; x++) {
                    backbuffer[(ind_y+y)*(current_video_mode.pitch/4) + (ind_x+x)] = color;
                }
            }
        }
        win = win->next;
    }
}

static void lgx_draw_app_menu() {
    int dock_w = 120; // 120 is the new width
    int dock_h = 80;
    int dock_x = (current_video_mode.width - dock_w) / 2;
    int dock_y = current_video_mode.height - dock_h - 20;

    // Fundo do Dock
    graphics_blit(backbuffer, current_video_mode.pitch, dock_x, dock_y,
                  (uint32_t*)ui_app_menu_bg_data, dock_w, dock_h, dock_w * 4,
                  0, 0, dock_w, dock_h);
                  
    // Ícones
    int icon_size = 48;
    int start_x = dock_x + (dock_w - icon_size) / 2;
    int icon_y = dock_y + (dock_h - icon_size) / 2;
    
    // 1. Demo GUI
    graphics_blit(backbuffer, current_video_mode.pitch, start_x, icon_y,
                  (uint32_t*)ui_icon_demo_data, icon_size, icon_size, icon_size * 4,
                  0, 0, icon_size, icon_size);
                  
    // Highlight da seleção pelo teclado
    if (app_menu_selected == 0) {
        int hl_x = start_x - 4;
        int hl_y = icon_y - 4;
        int hl_w = icon_size + 8;
        int hl_h = icon_size + 8;
        for (int y = 0; y < hl_h; y++) {
            for (int x = 0; x < hl_w; x++) {
                if (x < 3 || x >= hl_w - 3 || y < 3 || y >= hl_h - 3) { // 3px border
                    int sy = hl_y + y;
                    int sx = hl_x + x;
                    if (sx >= 0 && sx < (int)current_video_mode.width && sy >= 0 && sy < (int)current_video_mode.height) {
                        // Sky 400 Color for highlight
                        backbuffer[sy * (current_video_mode.pitch/4) + sx] = graphics_alpha_blend(backbuffer[sy * (current_video_mode.pitch/4) + sx], 0xFF38BDF8);
                    }
                }
            }
        }
    }
}

static void lgx_draw_error_popup() {
    if (!lgx_error_popup_msg) return;
    
    int w = 200;
    int h = 100;
    int x = (current_video_mode.width - w) / 2;
    int y = (current_video_mode.height - h) / 2;
    
    // Draw Red Box
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) {
            if (i < 2 || i >= w - 2 || j < 2 || j >= h - 2) {
                backbuffer[(y + j) * (current_video_mode.pitch/4) + (x + i)] = 0xFFFFFFFF; // White border
            } else {
                backbuffer[(y + j) * (current_video_mode.pitch/4) + (x + i)] = 0xFFEF4444; // Red 500
            }
        }
    }
    
    // Draw White Cross
    for (int j = -20; j <= 20; j++) {
        for (int i = -3; i <= 3; i++) {
            backbuffer[(y + h/2 + j) * (current_video_mode.pitch/4) + (x + w/2 + i)] = 0xFFFFFFFF;
            backbuffer[(y + h/2 + i) * (current_video_mode.pitch/4) + (x + w/2 + j)] = 0xFFFFFFFF;
        }
    }
}

void lgx_compositor_task(void) {
    bool lgx_prev_enter = false;
    
    while (1) {
        bool current_click = is_left_clicked();
        bool right_click = is_right_clicked(); // Placeholder para o menu radial
        int mx = get_mouse_x();
        int my = get_mouse_y();
        
        bool key_enter = keyboard_is_pressed(0x1C); // Enter
        
        // --- Atalho: App Launcher ---
        if (keyboard_consume_win_key()) {
            lgx_app_menu_open = !lgx_app_menu_open;
            if (lgx_app_menu_open) {
                radial_menu_open = false; // Fecha radial menu se abrir app menu
                app_menu_selected = 0; // Seleciona o primeiro por padrão
            }
        }
        
        if (lgx_app_menu_open) {
            app_menu_selected = 0; // Only 1 app
            if (key_enter && !lgx_prev_enter) {
                extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
                char *argv[] = {NULL, NULL};
                argv[0] = "/demo_gui";
                if (launch_initrd_program_argv(argv[0], argv) < 0) {
                    lgx_error_popup_msg = get_launch_last_error();
                }
                lgx_app_menu_open = false;
            }
        }
        
        lgx_prev_enter = key_enter;
        
        // --- Atalhos de Câmera (Zoom, Home, Fit) ---
        if (!lgx_app_menu_open) {
            if (keyboard_is_pressed(0x0D) || keyboard_is_pressed(0x4E)) { // + ou Numpad +
            camera_zoom += 0.02f;
            if (camera_zoom > 2.0f) camera_zoom = 2.0f;
        }
        if (keyboard_is_pressed(0x0C) || keyboard_is_pressed(0x4A)) { // - ou Numpad -
            camera_zoom -= 0.02f;
            if (camera_zoom < 0.25f) camera_zoom = 0.25f;
        }
        if (keyboard_is_pressed(0x23)) { // H (Home)
            camera_x = 0;
            camera_y = 0;
            camera_zoom = 1.0f;
        }
        if (keyboard_is_pressed(0x21)) { // F (Fit)
            if (window_list_head) {
                int min_x = 9999999, min_y = 9999999;
                int max_x = -9999999, max_y = -9999999;
                window_t* w = window_list_head;
                while(w) {
                    if (w->x < min_x) min_x = w->x;
                    if (w->y < min_y) min_y = w->y;
                    if (w->x + w->width > max_x) max_x = w->x + w->width;
                    if (w->y + w->height > max_y) max_y = w->y + w->height;
                    w = w->next;
                }
                
                int w_w = max_x - min_x;
                int w_h = max_y - min_y;
                if (w_w < 100) w_w = 100;
                if (w_h < 100) w_h = 100;
                
                float zoom_x = (float)current_video_mode.width / (float)(w_w + 100);
                float zoom_y = (float)current_video_mode.height / (float)(w_h + 100);
                camera_zoom = zoom_x < zoom_y ? zoom_x : zoom_y;
                if (camera_zoom > 1.0f) camera_zoom = 1.0f;
                if (camera_zoom < 0.25f) camera_zoom = 0.25f;
                
                int center_x = min_x + w_w / 2;
                int center_y = min_y + w_h / 2;
                camera_x = center_x - (int)((current_video_mode.width / 2.0f) / camera_zoom);
                camera_y = center_y - (int)((current_video_mode.height / 2.0f) / camera_zoom);
            }
        }
        } // fecha if (!lgx_app_menu_open)
        
        if (right_click && !lgx_prev_right_click) {
            radial_menu_open = !radial_menu_open;
            if (radial_menu_open) {
                radial_menu_x = mx;
                radial_menu_y = my;
                radial_menu_target_win = NULL;
                
                // Descobre se clicou numa janela (para menu de janela)
                window_t *win = window_list_tail;
                while (win) {
                    int win_screen_x = (int)((win->x - camera_x) * camera_zoom);
                    int win_screen_y = (int)((win->y - camera_y) * camera_zoom);
                    int win_screen_w = (int)(win->width * camera_zoom);
                    int win_screen_h = (int)(win->height * camera_zoom);
                    
                    if (mx >= win_screen_x && mx < win_screen_x + win_screen_w &&
                        my >= win_screen_y && my < win_screen_y + win_screen_h) {
                        radial_menu_target_win = win;
                        break;
                    }
                    win = win->prev;
                }
            }
        }
        
        // --- Atalho: App Launcher ---
        if (keyboard_consume_win_key()) {
            lgx_app_menu_open = !lgx_app_menu_open;
            if (lgx_app_menu_open) radial_menu_open = false; // Fecha radial menu se abrir app menu
        }

        // --- 4.3: Interatividade (Movimentação e Foco) ---
        if (current_click && !lgx_prev_click) {
            bool handled_by_app_menu = false;
            
            if (lgx_app_menu_open) {
                int dock_w = 120; // smaller dock
                int dock_h = 80;
                int dock_x = (current_video_mode.width - dock_w) / 2;
                int dock_y = current_video_mode.height - dock_h - 20;
                
                if (mx >= dock_x && mx <= dock_x + dock_w && my >= dock_y && my <= dock_y + dock_h) {
                    handled_by_app_menu = true;
                    int icon_size = 48;
                    int start_x = dock_x + (dock_w - icon_size) / 2;
                    int icon_y = dock_y + (dock_h - icon_size) / 2;
                    
                    extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
                    char *argv[] = {NULL, NULL};
                    
                    if (my >= icon_y && my <= icon_y + icon_size) {
                        if (mx >= start_x && mx <= start_x + icon_size) {
                            argv[0] = "/demo_gui";
                            if (launch_initrd_program_argv("/demo_gui", argv) < 0) {
                                lgx_error_popup_msg = get_launch_last_error();
                            }
                            lgx_app_menu_open = false;
                        }
                    }
                } else {
                    lgx_app_menu_open = false;
                }
            }

            if (!handled_by_app_menu) {
                if (radial_menu_open) {
                    int dx = mx - radial_menu_x;
                    int dy = my - radial_menu_y;
                    int dist_sq = dx*dx + dy*dy;
                    
                    if (dist_sq <= 60*60) {
                        if (radial_menu_target_win) {
                            if (dx >= -16 && dx <= 16 && dy >= -40 && dy <= -8) {
                                window_destroy(radial_menu_target_win);
                                radial_menu_open = false;
                            }
                        } else {
                            if (dx >= -16 && dx <= 16 && dy >= -40 && dy <= -8) {
                                radial_menu_open = false;
                            }
                        }
                        radial_menu_open = false;
                    } else {
                        radial_menu_open = false;
                    }
                } else {
                    window_t *win = window_list_tail;
                    bool clicked_window = false;
                    
                    while (win) {
                        int win_screen_x = (int)((win->x - camera_x) * camera_zoom);
                        int win_screen_y = (int)((win->y - camera_y) * camera_zoom);
                        int win_screen_w = (int)(win->width * camera_zoom);
                        int win_screen_h = (int)(win->height * camera_zoom);
                        
                        if (mx >= win_screen_x && mx < win_screen_x + win_screen_w &&
                            my >= win_screen_y && my < win_screen_y + win_screen_h) {
                            uint32_t current_ticks = timer_ticks;
                            if (lgx_last_clicked_win == win && (current_ticks - lgx_last_click_ticks < 30)) {
                                // Double Click Detectado!
                                radial_menu_open = true;
                                radial_menu_target_win = win;
                                radial_menu_x = mx;
                                radial_menu_y = my;
                                clicked_window = true;
                            } else {
                                window_bring_to_front(win);
                                clicked_window = true;
                                
                                lgx_is_dragging = true;
                                lgx_drag_window = win;
                                
                                int mx_world = (int)(mx / camera_zoom) + camera_x;
                                int my_world = (int)(my / camera_zoom) + camera_y;
                                lgx_drag_offset_x = mx_world - win->x;
                                lgx_drag_offset_y = my_world - win->y;
                                
                                lgx_last_clicked_win = win;
                                lgx_last_click_ticks = current_ticks;
                            }
                            
                            break;
                        }
                        win = win->prev;
                    }
                    
                    if (!clicked_window) {
                        lgx_dragging_bg = true;
                        lgx_drag_bg_start_cx = camera_x;
                        lgx_drag_bg_start_cy = camera_y;
                        lgx_drag_bg_start_mx = mx;
                        lgx_drag_bg_start_my = my;
                        lgx_last_clicked_win = NULL; // reset double click se clicou no void
                    }
                }
            }
        } else if (current_click && lgx_is_dragging && lgx_drag_window && !radial_menu_open) {
            int mx_world = (int)(mx / camera_zoom) + camera_x;
            int my_world = (int)(my / camera_zoom) + camera_y;
            lgx_drag_window->x = mx_world - lgx_drag_offset_x;
            lgx_drag_window->y = my_world - lgx_drag_offset_y;
        } else if (current_click && lgx_dragging_bg && !radial_menu_open) {
            camera_x = lgx_drag_bg_start_cx - (int)((mx - lgx_drag_bg_start_mx) / camera_zoom);
            camera_y = lgx_drag_bg_start_cy - (int)((my - lgx_drag_bg_start_my) / camera_zoom);
        } else if (!current_click) {
            lgx_is_dragging = false;
            lgx_drag_window = NULL;
            lgx_dragging_bg = false;
        }
        
        lgx_prev_click = current_click;
        lgx_prev_right_click = right_click;
        
        // --- 4.1: O Laço do Compositor ---
        if (backbuffer && current_video_mode.vram_address) {
            
            // 1. Limpar Backbuffer
            lgx_draw_background();
            
            // 2 & 3. Percorrer lista de janelas e disparar graphics_blit
            window_t *win = window_list_head;
            while (win) {
                // 4.2: Decoração de Janelas Automatizada
                if (!(win->flags & WIN_FLAG_NO_BORDER)) {
                    int win_bx = (int)((win->x - camera_x) * camera_zoom);
                    int win_by = (int)((win->y - camera_y) * camera_zoom);
                    int win_bw = (int)(win->width * camera_zoom);
                    int win_bh = (int)(win->height * camera_zoom);
                    int shadow_size = (int)(12 * camera_zoom);
                    if (shadow_size < 1) shadow_size = 1;
                    
                    // Fast Solid Border Instead of Heavy Shadows
                    uint32_t border_color = 0xFF334155; // Slate 700
                    
                    // Top and Bottom
                    for (int tx = -1; tx <= win_bw; tx++) {
                        int screen_x = win_bx + tx;
                        if (screen_x >= 0 && screen_x < (int)current_video_mode.width) {
                            if (win_by - 1 >= 0 && win_by - 1 < (int)current_video_mode.height)
                                backbuffer[(win_by - 1) * (current_video_mode.pitch/4) + screen_x] = border_color;
                            if (win_by + win_bh >= 0 && win_by + win_bh < (int)current_video_mode.height)
                                backbuffer[(win_by + win_bh) * (current_video_mode.pitch/4) + screen_x] = border_color;
                        }
                    }
                    
                    // Left and Right
                    for (int ty = -1; ty <= win_bh; ty++) {
                        int screen_y = win_by + ty;
                        if (screen_y >= 0 && screen_y < (int)current_video_mode.height) {
                            if (win_bx - 1 >= 0 && win_bx - 1 < (int)current_video_mode.width)
                                backbuffer[screen_y * (current_video_mode.pitch/4) + (win_bx - 1)] = border_color;
                            if (win_bx + win_bw >= 0 && win_bx + win_bw < (int)current_video_mode.width)
                                backbuffer[screen_y * (current_video_mode.pitch/4) + (win_bx + win_bw)] = border_color;
                        }
                    }
                }
                
                // Blit do conteúdo da janela
                if (win->buffer) {
                    graphics_blit_scaled(backbuffer, current_video_mode.pitch, 
                                         (int)((win->x - camera_x) * camera_zoom), (int)((win->y - camera_y) * camera_zoom),
                                         win->buffer, win->width, win->height, camera_zoom);
                }
                
                win = win->next;
            }
            
            // Desenha o Radial Menu
            if (radial_menu_open) {
                // Fundo do Menu Radial (centrado em radial_menu_x, radial_menu_y)
                int rx = radial_menu_x - 60;
                int ry = radial_menu_y - 60;
                graphics_blit(backbuffer, current_video_mode.pitch, rx, ry,
                              (uint32_t*)ui_pie_menu_data, 120, 120, 120 * 4,
                              0, 0, 120, 120);
                
                // Botões internos
                if (radial_menu_target_win) {
                    // Botão Fechar no topo
                    graphics_blit(backbuffer, current_video_mode.pitch, radial_menu_x - 16, radial_menu_y - 40,
                                  (uint32_t*)ui_pie_btn_close_data, 32, 32, 32 * 4,
                                  0, 0, 32, 32);
                } else {
                    // Botão Terminal no topo
                    graphics_blit(backbuffer, current_video_mode.pitch, radial_menu_x - 16, radial_menu_y - 40,
                                  (uint32_t*)ui_pie_btn_app_data, 32, 32, 32 * 4,
                                  0, 0, 32, 32);
                }
            }
            
            // Desenha o Minimapa (Radar HUD)
            lgx_draw_minimap();
            
            // Indicadores de borda
            lgx_draw_offscreen_indicators();
            
            // App Launcher
            if (lgx_app_menu_open) {
                lgx_draw_app_menu();
            }
            
            if (lgx_error_popup_msg) {
                lgx_draw_error_popup();
                if (current_click && !lgx_prev_click) {
                    lgx_error_popup_msg = NULL; // Dismiss
                }
            }
            
            // 4. Desenhar o cursor com Alpha Blending
            lgx_draw_cursor(mx, my);
            
            // 5. Invocar screen_swap() via fast_memcpy
            fast_memcpy(current_video_mode.vram_address, backbuffer, fb_size_bytes);
        }
        
        asm volatile("hlt");
    }
}
