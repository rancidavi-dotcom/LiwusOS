#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <liwus_gui.h>
#include <libliw.h>
#include "system_bridge.h"
#include "debug.h"
#include "tile.h"

int errno = 0;
int* __errno(void) { return &errno; }

// Use inline assembly for syscall 8 to get timer ticks
static inline uint64_t sys_timer_ticks(void) {
    uint64_t ret;
    asm volatile("mov $8, %%rax\n\tint $0x80\n\tmov %%rax, %0\n" : "=r"(ret) :: "rax", "memory");
    return ret;
}

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define TILE_SIZE 16
#define GRID_WIDTH 128
#define GRID_HEIGHT 128

typedef enum {
    ZONE_EMPTY = 0,
    ZONE_ROAD = 1,
    ZONE_RIVER = 2,
    ZONE_PARK = 3,
    ZONE_COMMERCIAL = 4,
    ZONE_INDUSTRIAL = 5,
    ZONE_INFRASTRUCTURE = 6
} zone_type_t;

static zone_type_t city_map[GRID_WIDTH][GRID_HEIGHT];
static uint32_t frame_buffer[WINDOW_WIDTH * WINDOW_HEIGHT];

// Camera state (auto panning)
float cam_x = 0;
float cam_y = 0;
float zoom = 1.0f;
float cam_dx = 1.0f;
float cam_dy = 1.0f;

// Simple LCG for procedural generation
static uint32_t proc_seed = 0;
static uint32_t proc_rand() {
    proc_seed = (proc_seed * 1103515245 + 12345) & 0x7fffffff;
    return proc_seed;
}

static void generate_city(uint32_t seed) {
    proc_seed = seed;
    
    for (int x = 0; x < GRID_WIDTH; x++) {
        for (int y = 0; y < GRID_HEIGHT; y++) {
            city_map[x][y] = ZONE_EMPTY;
        }
    }
}

static void generate_chunk(int px, int py) {
    // Generate a 9x9 block of city around this process
    int radius = 4;
    for (int x = px - radius; x <= px + radius; x++) {
        for (int y = py - radius; y <= py + radius; y++) {
            if (x >= 0 && x < GRID_WIDTH && y >= 0 && y < GRID_HEIGHT) {
                if (city_map[x][y] == ZONE_EMPTY) {
                    // Create roads on the border of the 9x9 chunk
                    if (x == px - radius || x == px + radius || y == py - radius || y == py + radius) {
                        city_map[x][y] = ZONE_ROAD;
                    } else if (x == px && y == py) {
                        // The process itself is in a park or something, or just commercial
                        city_map[x][y] = ZONE_PARK;
                    } else {
                        // Random building zone
                        zone_type_t zt = (proc_rand() % 4) + 3;
                        city_map[x][y] = zt;
                    }
                }
            }
        }
    }
}

static void fill_rect(int rx, int ry, int rw, int rh, uint32_t color) {
    if (rx >= WINDOW_WIDTH || ry >= WINDOW_HEIGHT || rx + rw <= 0 || ry + rh <= 0) return;
    
    int start_x = rx < 0 ? 0 : rx;
    int start_y = ry < 0 ? 0 : ry;
    int end_x = rx + rw;
    int end_y = ry + rh;
    if (end_x > WINDOW_WIDTH) end_x = WINDOW_WIDTH;
    if (end_y > WINDOW_HEIGHT) end_y = WINDOW_HEIGHT;

    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            // Alpha blend
            uint32_t bg = frame_buffer[y * WINDOW_WIDTH + x];
            uint32_t a = (color >> 24) & 0xFF;
            if (a == 0xFF) {
                frame_buffer[y * WINDOW_WIDTH + x] = color;
            } else if (a > 0) {
                uint32_t inv = 255 - a;
                uint32_t r = (((color >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv) >> 8;
                uint32_t g = (((color >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * inv) >> 8;
                uint32_t b = ((color & 0xFF) * a + (bg & 0xFF) * inv) >> 8;
                frame_buffer[y * WINDOW_WIDTH + x] = (0xFF000000) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

static void draw_tile(int screen_x, int screen_y, int w, int h) {
    int start_x = screen_x < 0 ? 0 : screen_x;
    int start_y = screen_y < 0 ? 0 : screen_y;
    int end_x = screen_x + w > WINDOW_WIDTH ? WINDOW_WIDTH : screen_x + w;
    int end_y = screen_y + h > WINDOW_HEIGHT ? WINDOW_HEIGHT : screen_y + h;
    
    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            int tx = ((x - screen_x) * tile_width) / w;
            int ty = ((y - screen_y) * tile_height) / h;
            uint32_t color = tile_data[ty * tile_width + tx];
            uint32_t bg = frame_buffer[y * WINDOW_WIDTH + x];
            uint32_t a = (color >> 24) & 0xFF;
            if (a == 0xFF) {
                frame_buffer[y * WINDOW_WIDTH + x] = color;
            } else if (a > 0) {
                uint32_t inv = 255 - a;
                uint32_t r = (((color >> 16) & 0xFF) * a + ((bg >> 16) & 0xFF) * inv) >> 8;
                uint32_t g = (((color >> 8) & 0xFF) * a + ((bg >> 8) & 0xFF) * inv) >> 8;
                uint32_t b = ((color & 0xFF) * a + (bg & 0xFF) * inv) >> 8;
                frame_buffer[y * WINDOW_WIDTH + x] = (0xFF000000) | (r << 16) | (g << 8) | b;
            }
        }
    }
}

static void render_world(const system_state_t* state) {
    // Clear screen
    fill_rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT, 0xFF141419);

    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            int screen_x = (x * TILE_SIZE - cam_x) * zoom;
            int screen_y = (y * TILE_SIZE - cam_y) * zoom;
            int screen_w = TILE_SIZE * zoom;
            int screen_h = TILE_SIZE * zoom;

            if (screen_x + screen_w < 0 || screen_x >= WINDOW_WIDTH ||
                screen_y + screen_h < 0 || screen_y >= WINDOW_HEIGHT) {
                continue;
            }

            zone_type_t zt = city_map[x][y];
            if (zt == ZONE_EMPTY) continue;
            
            uint32_t color = 0xFF1E1E1E;
            if (zt == ZONE_RIVER) color = 0xFF0064C8;
            else if (zt == ZONE_ROAD) color = 0xFF3C3C3C;
            else if (zt == ZONE_PARK) color = 0xFF228B22;
            else if (zt == ZONE_COMMERCIAL) color = 0xFF6495ED;
            else if (zt == ZONE_INDUSTRIAL) color = 0xFFCD853F;
            else if (zt == ZONE_INFRASTRUCTURE) color = 0xFFA9A9A9;
            
            fill_rect(screen_x, screen_y, screen_w - 1, screen_h - 1, color);
        }
    }

    for (int i = 0; i < state->num_processes; i++) {
        int center_x = GRID_WIDTH / 2;
        int center_y = GRID_HEIGHT / 2;
        int px = center_x + ((state->processes[i].pid * 13) % 40) - 20;
        int py = center_y + ((state->processes[i].pid * 17) % 30) - 15;
        
        int screen_x = (px * TILE_SIZE - cam_x) * zoom;
        int screen_y = (py * TILE_SIZE - cam_y) * zoom;
        int screen_w = TILE_SIZE * zoom;
        int screen_h = TILE_SIZE * zoom;
        
        if (screen_x + screen_w < 0 || screen_x >= WINDOW_WIDTH ||
            screen_y + screen_h < 0 || screen_y >= WINDOW_HEIGHT) {
            continue;
        }
        
        uint32_t color = 0xFFFFFFFF;
        if (state->processes[i].category == PROC_CAT_TERMINAL) color = 0xFFFF6464;
        else if (state->processes[i].category == PROC_CAT_BROWSER) color = 0xFF6464FF;
        else if (state->processes[i].category == PROC_CAT_GAME) color = 0xFF64FF64;
        else if (state->processes[i].category == PROC_CAT_SYSTEM) color = 0xFFC8C8C8;
        
        float height_factor = (state->processes[i].memory_mb / 10.0f);
        if (height_factor < 0.5f) height_factor = 0.5f;
        if (height_factor > 3.0f) height_factor = 3.0f;
        
        // Draw image instead of rect for the process/building
        draw_tile(screen_x - (screen_w/2), screen_y - (screen_h * height_factor) + screen_h, screen_w * 2, screen_h * height_factor + screen_h);
        
        // Draw the color indicator
        fill_rect(screen_x + 2, screen_y + screen_h - 10, screen_w - 4, 8, color);
        
        if (state->processes[i].cpu_usage > 1) {
            fill_rect(screen_x + 4, screen_y - (screen_h * height_factor) + screen_h, 4, 4, 0xFFFFFF00);
        }
    }
}

int main() {
    debug_print("[LDE] Starting Living Desktop Engine\n");
    Canvas canvas = canvas_create(WINDOW_WIDTH, WINDOW_HEIGHT, "Living Desktop Engine (LiwusOS)");
    if (!canvas) {
        debug_print("[LDE] Failed to create canvas\n");
        exit(1);
    }
    debug_print("[LDE] Canvas created successfully\n");
    
    // Create image node
    Node img = image_create(canvas, WINDOW_WIDTH, WINDOW_HEIGHT, frame_buffer);
    if (!img) {
        debug_print("[LDE] Failed to create image\n");
        exit(1);
    }
    debug_print("[LDE] Image created successfully\n");
    
    node_move(img, 0, 0);
    canvas_add(canvas, img);

    system_bridge_init();
    generate_city(1234567);
    debug_print("[LDE] City generated\n");

    // Initial camera pos (Locked to center)
    cam_x = GRID_WIDTH * TILE_SIZE / 2 - WINDOW_WIDTH / 2;
    cam_y = GRID_HEIGHT * TILE_SIZE / 2 - WINDOW_HEIGHT / 2;

    int tick = 0;
    uint64_t last_render_tick = sys_timer_ticks();
    
    debug_print("[LDE] Entering main loop\n");
    while (1) {
        uint64_t current_tick = sys_timer_ticks();
        // Render every 20 ticks (e.g. 100ms if 1 tick = 5ms) to avoid CPU hogging
        if (current_tick - last_render_tick >= 20) {
            last_render_tick = current_tick;
            
            if (tick % 10 == 0) {
                system_bridge_update();
                
                // Dynamically generate city around active processes
                const system_state_t* state = system_bridge_get_state();
                for (int i = 0; i < state->num_processes; i++) {
                    int center_x = GRID_WIDTH / 2;
                    int center_y = GRID_HEIGHT / 2;
                    int px = center_x + ((state->processes[i].pid * 13) % 40) - 20;
                    int py = center_y + ((state->processes[i].pid * 17) % 30) - 15;
                    
                    if (px >= 0 && px < GRID_WIDTH && py >= 0 && py < GRID_HEIGHT) {
                        if (city_map[px][py] == ZONE_EMPTY) {
                            generate_chunk(px, py);
                        }
                    }
                }
            }
            
            // Camera is locked, no WASD
            
            render_world(system_bridge_get_state());
            image_update(img, frame_buffer, WINDOW_WIDTH * WINDOW_HEIGHT);
            tick++;
        } else {
            // Busy wait lightly
            for (volatile int i = 0; i < 5000; i++) {}
        }
    }
    
    return 0;
}
