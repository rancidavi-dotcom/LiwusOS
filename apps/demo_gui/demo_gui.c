#include <stdint.h>
#include <stdlib.h>
#include <lgx.h>
#include <libliw.h>

int errno = 0;
int* __errno(void) { return &errno; }

int main() {
    int w = 200;
    int h = 150;
    
    // Create a window via liblgx
    lgx_window_t *win = lgx_init(w, h, 0);
    if (!win) {
        exit(1);
    }
    
    // Draw an animated colorful pattern using our new API
    int offset = 0;
    while (1) {
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                uint8_t r = (x + offset) % 255;
                uint8_t g = (y + offset) % 255;
                uint8_t b = (x * y + offset) % 255;
                
                uint32_t color = (0xFF << 24) | (r << 16) | (g << 8) | b;
                // Directly drawing to buffer is still possible for speed, 
                // but let's just do it directly for this demo's speed.
                win->buffer[y * w + x] = color;
            }
        }
        
        // Draw a rectangle in the middle using the new LGX API function!
        lgx_draw_rect(win, 50, 50, 100, 50, 0xFFFFFFFF); // White box
        
        lgx_refresh(win);
        
        offset += 5;
        
        // Small delay loop
        for (volatile int i = 0; i < 1000000; i++) {}
    }
    
    return 0;
}
