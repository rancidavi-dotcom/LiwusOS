#include "dock.h"
#include "video.h"
#include "gui.h"
#include "mouse.h"
#include "syscall.h"
#include "terminal.h"
#include "settings.h"
#include "explorer.h"
#include "editor.h"
#include "launcher.h"
#include "timer.h"

extern bool is_live_mode;
extern uint32_t screen_width;
extern uint32_t screen_height;

static int dock_y = -50;
static int target_y = -50;

static void launch_program(const char *name) {
    launch_initrd_program(name);
}

void init_dock() {
    dock_y = screen_height - 50;
    target_y = screen_height - 50;
}

void update_dock(int mx, int my) {
    dock_y = screen_height - 50;
    target_y = screen_height - 50;

    /* Detecção de Clique na Dock */
    if (is_left_clicked()) {
        int dock_w = 580;
        int dock_x = (screen_width - dock_w) / 2;
        
        if (my >= dock_y) {
            if (is_inside(mx, my, dock_x + 10, dock_y + 5, 80, 35))
                toggle_launcher();
            else if (is_inside(mx, my, dock_x + 100, dock_y + 5, 80, 35))
                open_terminal();
            else if (is_inside(mx, my, dock_x + 190, dock_y + 5, 80, 35))
                open_settings();
            else if (is_inside(mx, my, dock_x + 280, dock_y + 5, 80, 35))
                open_explorer();
            else if (is_inside(mx, my, dock_x + 370, dock_y + 5, 80, 35))
                open_editor();
            else if (is_inside(mx, my, dock_x + 460, dock_y + 5, 80, 35))
                launch_program("calc");
            else if (is_inside(mx, my, dock_x + 550, dock_y + 5, 80, 35))
                launch_program("doomgeneric");
        }
    }
}

void draw_dock() {
    int dock_w = 580;
    int dock_h = 50;
    int dock_x = (screen_width - dock_w) / 2;
    dock_y = screen_height - dock_h;

    draw_rect(dock_x, dock_y, dock_w, dock_h, 0x111111);
    draw_rect(dock_x, dock_y, dock_w, 1, 0x444444);

    draw_button_visual(dock_x + 10, dock_y + 5, 80, 35, "Apps", 0xFF00FF);
    draw_button_visual(dock_x + 100, dock_y + 5, 80, 35, "Term", 0x00AA00);
    draw_button_visual(dock_x + 190, dock_y + 5, 80, 35, "Config", 0x5555AA);
    draw_button_visual(dock_x + 280, dock_y + 5, 80, 35, "Files", 0x333333);
    draw_button_visual(dock_x + 370, dock_y + 5, 80, 35, "Liwim", 0x3A6EA5);
    draw_button_visual(dock_x + 460, dock_y + 5, 80, 35, "Calc", 0xCC8800);
    draw_button_visual(dock_x + 550, dock_y + 5, 80, 35, "Doom", 0xAA5500);
    
    // Relogio Real baseado no Timer do Kernel
    int total_seconds = timer_ticks / 100;
    int mins = (total_seconds / 60) % 60;
    int secs = total_seconds % 60;
    
    char time_str[16];
    time_str[0] = '0' + (mins / 10);
    time_str[1] = '0' + (mins % 10);
    time_str[2] = ':';
    time_str[3] = '0' + (secs / 10);
    time_str[4] = '0' + (secs % 10);
    time_str[5] = '\0';

    draw_string(dock_x + dock_w - 80, dock_y + 15, time_str, 0x00FF00);
}
