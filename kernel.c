#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "io.h"
#include "gdt.h"
#include "idt.h"
#include "multiboot.h"
#include "pmm.h"
#include "timer.h"
#include "task.h"
#include "video.h"
#include "mouse.h"
#include "gui.h"
#include "string.h"
#include "pci.h"
#include "rtl8139.h"
#include "net.h"
#include "wifi.h"
#include "installer.h"
#include "terminal.h"
#include "settings.h"
#include "browser.h"
#include "welcome.h"
#include "launcher.h"

typedef enum { MENU, LIVE_MODE, INSTALL_MODE } state_t;
state_t current_state = MENU;

extern uint32_t end;
extern void refresh_screen();
extern char get_last_key();
extern widget_t* init_calculator();
extern widget_t* init_book();
extern void init_dock();

extern bool gui_is_dirty;
extern void gui_mark_dirty();

void task_gui_main() {
    widget_t* apps[10];
    apps[0] = init_book();
    apps[1] = init_calculator();
    apps[2] = init_terminal();
    apps[3] = init_settings();
    apps[4] = init_browser();
    apps[5] = init_welcome();
    apps[6] = init_launcher(apps, 6);
    int app_count = 7;

    init_dock();

    uint32_t last_render = 0;
    const uint32_t TICKS_PER_FRAME = 3; // ~33 FPS (Timer a 100Hz)
    char pending_key = 0;

    while(1) {
        int mx = get_mouse_x(); int my = get_mouse_y();
        bool clicked = is_left_clicked();
        char key = get_last_key();
        
        if (key != 0) {
            pending_key = key;
            gui_mark_dirty();
        }

        gui_handle_mouse_update(mx, my);
        if (clicked) gui_mark_dirty();

        if (timer_ticks - last_render >= TICKS_PER_FRAME) {
            if (gui_is_dirty) {
                gui_is_dirty = false; // Reset flag at start of frame
                
                event_t ev = {EVENT_NONE, mx, my, 0, false};
                if (clicked) ev.type = EVENT_MOUSE_CLICK;
                if (pending_key != 0) {
                    ev.type = EVENT_KEY_PRESS;
                    ev.key = pending_key;
                    pending_key = 0; // Consume key
                }

                if (current_state == MENU) {
                    clear_screen(0x000000);
                    draw_string(450, 150, "LiwusOS Network Edition", 0x00FF00);
                    draw_button_visual(450, 220, 350, 40, "1. Iniciar Modo Live", 0x0055AA);
                    refresh_screen();
                    if (ev.type == EVENT_KEY_PRESS && ev.key == '1') {
                        current_state = LIVE_MODE;
                        gui_mark_dirty(); // Force full redraw for transition
                    }
                } 
                else if (current_state == LIVE_MODE) {
                    if (ev.type == EVENT_KEY_PRESS) {
                        extern void update_terminal_key(char k);
                        update_terminal_key(ev.key);
                    }

                    gui_render_all(apps, app_count, &ev);
                    
                    extern void update_browser(); update_browser();
                    extern void update_dock(int mx, int my);
                    update_dock(mx, my); 
                }
                last_render = timer_ticks;
            }
        }
        asm volatile("hlt");
    }
}

void kernel_main(uint32_t magic, multiboot_info_t* mbi) {
    (void)magic;
    init_gdt(); init_idt();
    pmm_init((uint32_t)&end + 0x1000, mbi->mem_upper * 1024);
    init_video(mbi); 
    
    pci_init();
    net_init();

    pci_device_t* net = pci_get_net();
    if (net) init_rtl8139(net);

    pci_device_t* wifi = pci_get_wireless();
    if (wifi) {
        wifi_init(wifi);
    } else {
        wifi_init((void*)0); 
    }

    init_timer(100);
    init_tasking();
    init_mouse();

    create_task(task_gui_main);
    asm volatile("sti");
    while (1) { asm volatile("hlt"); }
}