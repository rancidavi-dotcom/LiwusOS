#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdbool.h>
#include "mock_bridge.h"

#define WINDOW_WIDTH 1024
#define WINDOW_HEIGHT 768
#define TILE_SIZE 32
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

zone_type_t city_map[GRID_WIDTH][GRID_HEIGHT];

// Camera state
float cam_x = GRID_WIDTH * TILE_SIZE / 2 - WINDOW_WIDTH / 2;
float cam_y = GRID_HEIGHT * TILE_SIZE / 2 - WINDOW_HEIGHT / 2;
float zoom = 1.0f;

// Timers
Uint32 last_update_time = 0;

// Simple LCG for procedural generation
static uint32_t proc_seed = 0;
static uint32_t proc_rand() {
    proc_seed = (proc_seed * 1103515245 + 12345) & 0x7fffffff;
    return proc_seed;
}

void generate_city(uint32_t seed) {
    proc_seed = seed;
    
    // Clear map
    for (int x = 0; x < GRID_WIDTH; x++) {
        for (int y = 0; y < GRID_HEIGHT; y++) {
            city_map[x][y] = ZONE_EMPTY;
        }
    }

    // Generate River
    int river_x = GRID_WIDTH / 2;
    for (int y = 0; y < GRID_HEIGHT; y++) {
        river_x += (proc_rand() % 3) - 1;
        if (river_x < 10) river_x = 10;
        if (river_x > GRID_WIDTH - 10) river_x = GRID_WIDTH - 10;
        
        city_map[river_x][y] = ZONE_RIVER;
        city_map[river_x+1][y] = ZONE_RIVER;
        city_map[river_x+2][y] = ZONE_RIVER;
    }

    // Generate main roads
    for (int x = 0; x < GRID_WIDTH; x += 16) {
        for (int y = 0; y < GRID_HEIGHT; y++) {
            if (city_map[x][y] != ZONE_RIVER) city_map[x][y] = ZONE_ROAD;
        }
    }
    for (int y = 0; y < GRID_HEIGHT; y += 16) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            if (city_map[x][y] != ZONE_RIVER) city_map[x][y] = ZONE_ROAD;
        }
    }

    // Assign zones to blocks
    for (int x = 1; x < GRID_WIDTH; x += 16) {
        for (int y = 1; y < GRID_HEIGHT; y += 16) {
            zone_type_t zt = (proc_rand() % 4) + 3; // PARK, COMMERCIAL, INDUSTRIAL, INFRASTRUCTURE
            
            // Fill the block
            for (int bx = x; bx < x + 15 && bx < GRID_WIDTH; bx++) {
                for (int by = y; by < y + 15 && by < GRID_HEIGHT; by++) {
                    if (city_map[bx][by] == ZONE_EMPTY) {
                        city_map[bx][by] = zt;
                    }
                }
            }
        }
    }
}

void handle_input(bool* running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            *running = false;
        } else if (event.type == SDL_MOUSEWHEEL) {
            if (event.wheel.y > 0) zoom += 0.1f;
            else if (event.wheel.y < 0) zoom -= 0.1f;
            if (zoom < 0.2f) zoom = 0.2f;
            if (zoom > 3.0f) zoom = 3.0f;
        }
    }

    const Uint8* state = SDL_GetKeyboardState(NULL);
    float move_speed = 15.0f / zoom;
    if (state[SDL_SCANCODE_W] || state[SDL_SCANCODE_UP]) cam_y -= move_speed;
    if (state[SDL_SCANCODE_S] || state[SDL_SCANCODE_DOWN]) cam_y += move_speed;
    if (state[SDL_SCANCODE_A] || state[SDL_SCANCODE_LEFT]) cam_x -= move_speed;
    if (state[SDL_SCANCODE_D] || state[SDL_SCANCODE_RIGHT]) cam_x += move_speed;
}

void render_minimap(SDL_Renderer* renderer, const mock_system_state_t* state) {
    int map_w = GRID_WIDTH * 2;
    int map_h = GRID_HEIGHT * 2;
    int map_x = WINDOW_WIDTH - map_w - 20;
    int map_y = WINDOW_HEIGHT - map_h - 20;

    // Background
    SDL_Rect bg = { map_x - 2, map_y - 2, map_w + 4, map_h + 4 };
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 200);
    SDL_RenderFillRect(renderer, &bg);
    
    SDL_Rect bg2 = { map_x, map_y, map_w, map_h };
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, &bg2);

    // Draw Map
    for (int x = 0; x < GRID_WIDTH; x++) {
        for (int y = 0; y < GRID_HEIGHT; y++) {
            zone_type_t zt = city_map[x][y];
            if (zt == ZONE_RIVER) SDL_SetRenderDrawColor(renderer, 0, 100, 200, 255);
            else if (zt == ZONE_ROAD) SDL_SetRenderDrawColor(renderer, 50, 50, 50, 255);
            else if (zt == ZONE_PARK) SDL_SetRenderDrawColor(renderer, 30, 120, 30, 255);
            else if (zt == ZONE_COMMERCIAL) SDL_SetRenderDrawColor(renderer, 100, 100, 200, 255);
            else if (zt == ZONE_INDUSTRIAL) SDL_SetRenderDrawColor(renderer, 150, 100, 50, 255);
            else if (zt == ZONE_INFRASTRUCTURE) SDL_SetRenderDrawColor(renderer, 100, 100, 100, 255);
            else SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);

            SDL_RenderDrawPoint(renderer, map_x + x * 2, map_y + y * 2);
            SDL_RenderDrawPoint(renderer, map_x + x * 2 + 1, map_y + y * 2);
            SDL_RenderDrawPoint(renderer, map_x + x * 2, map_y + y * 2 + 1);
            SDL_RenderDrawPoint(renderer, map_x + x * 2 + 1, map_y + y * 2 + 1);
        }
    }

    // Draw processes on minimap
    for (int i = 0; i < state->num_processes; i++) {
        int px = (state->processes[i].pid * 13) % GRID_WIDTH;
        int py = (state->processes[i].pid * 17) % GRID_HEIGHT;
        SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
        SDL_RenderDrawPoint(renderer, map_x + px * 2, map_y + py * 2);
    }

    // Draw camera frustum
    float fx = (cam_x / (GRID_WIDTH * TILE_SIZE)) * map_w;
    float fy = (cam_y / (GRID_HEIGHT * TILE_SIZE)) * map_h;
    float fw = (WINDOW_WIDTH / zoom / (GRID_WIDTH * TILE_SIZE)) * map_w;
    float fh = (WINDOW_HEIGHT / zoom / (GRID_HEIGHT * TILE_SIZE)) * map_h;
    
    SDL_Rect frustum = { map_x + (int)fx, map_y + (int)fy, (int)fw, (int)fh };
    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    SDL_RenderDrawRect(renderer, &frustum);
}

void render_world(SDL_Renderer* renderer, const mock_system_state_t* state) {
    // Clear background
    SDL_SetRenderDrawColor(renderer, 20, 20, 25, 255);
    SDL_RenderClear(renderer);

    // Draw the grid
    for (int y = 0; y < GRID_HEIGHT; y++) {
        for (int x = 0; x < GRID_WIDTH; x++) {
            float screen_x = (x * TILE_SIZE - cam_x) * zoom;
            float screen_y = (y * TILE_SIZE - cam_y) * zoom;
            float screen_w = TILE_SIZE * zoom;
            float screen_h = TILE_SIZE * zoom;

            // Only draw if on screen
            if (screen_x + screen_w < 0 || screen_x > WINDOW_WIDTH ||
                screen_y + screen_h < 0 || screen_y > WINDOW_HEIGHT) {
                continue;
            }

            SDL_Rect rect = { (int)screen_x, (int)screen_y, (int)screen_w, (int)screen_h };
            
            zone_type_t zt = city_map[x][y];
            if (zt == ZONE_RIVER) SDL_SetRenderDrawColor(renderer, 0, 100, 200, 255);
            else if (zt == ZONE_ROAD) SDL_SetRenderDrawColor(renderer, 60, 60, 60, 255);
            else if (zt == ZONE_PARK) SDL_SetRenderDrawColor(renderer, 34, 139, 34, 255);
            else if (zt == ZONE_COMMERCIAL) SDL_SetRenderDrawColor(renderer, 100, 149, 237, 255);
            else if (zt == ZONE_INDUSTRIAL) SDL_SetRenderDrawColor(renderer, 205, 133, 63, 255);
            else if (zt == ZONE_INFRASTRUCTURE) SDL_SetRenderDrawColor(renderer, 169, 169, 169, 255);
            else SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
            
            SDL_RenderFillRect(renderer, &rect);
            
            // Draw grid outline slightly darker
            if (zoom > 0.5f) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 50);
                SDL_RenderDrawRect(renderer, &rect);
            }
        }
    }
    
    // Draw processes as buildings
    for (int i = 0; i < state->num_processes; i++) {
        int px = (state->processes[i].pid * 13) % GRID_WIDTH;
        int py = (state->processes[i].pid * 17) % GRID_HEIGHT;
        
        float screen_x = (px * TILE_SIZE - cam_x) * zoom;
        float screen_y = (py * TILE_SIZE - cam_y) * zoom;
        float screen_w = TILE_SIZE * zoom;
        float screen_h = TILE_SIZE * zoom;
        
        if (screen_x + screen_w < 0 || screen_x > WINDOW_WIDTH ||
            screen_y + screen_h < 0 || screen_y > WINDOW_HEIGHT) {
            continue;
        }
        
        // Base color based on category
        int r = 255, g = 255, b = 255;
        if (state->processes[i].category == PROC_CAT_TERMINAL) { r = 255; g = 100; b = 100; } // Industrial -> Red/Orange
        else if (state->processes[i].category == PROC_CAT_BROWSER) { r = 100; g = 100; b = 255; } // Commercial -> Blue
        else if (state->processes[i].category == PROC_CAT_GAME) { r = 100; g = 255; b = 100; } // Park -> Green
        else if (state->processes[i].category == PROC_CAT_SYSTEM) { r = 200; g = 200; b = 200; } // Infra -> Grey
        
        // Height proportional to memory (mocked)
        float height_factor = (state->processes[i].memory_mb / 128.0f) * 2.0f;
        if (height_factor < 0.5f) height_factor = 0.5f;
        
        SDL_Rect bldg = { (int)screen_x + 4, (int)(screen_y - (screen_h * height_factor) + screen_h), (int)screen_w - 8, (int)(screen_h * height_factor) };
        
        // Draw building body
        SDL_SetRenderDrawColor(renderer, r, g, b, 255);
        SDL_RenderFillRect(renderer, &bldg);
        
        // Draw roof outline
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderDrawRect(renderer, &bldg);
        
        // Activity indicator (CPU)
        if (state->processes[i].cpu_usage > 10) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
            SDL_Rect light = { bldg.x + 4, bldg.y + 4, 4, 4 };
            SDL_RenderFillRect(renderer, &light);
        }
    }
    
    // Draw Minimap
    render_minimap(renderer, state);
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    
    // Enable alpha blending for minimap
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");

    SDL_Window* window = SDL_CreateWindow(
        "LDE - Host Prototype (Fase 1/2)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        WINDOW_WIDTH, WINDOW_HEIGHT,
        SDL_WINDOW_SHOWN
    );

    if (!window) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    mock_bridge_init();
    
    // Generate procedural city with a fixed seed so it's always the same for this OS
    generate_city(1234567);

    bool running = true;
    last_update_time = SDL_GetTicks();

    while (running) {
        // Handle input (60 FPS loop)
        handle_input(&running);

        // Update logic (1-2 Hz simulation)
        Uint32 current_time = SDL_GetTicks();
        if (current_time - last_update_time > 1000) { // 1 Hz
            mock_bridge_update();
            last_update_time = current_time;
        }

        // Render
        const mock_system_state_t* state = mock_bridge_get_state();
        render_world(renderer, state);
        
        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
