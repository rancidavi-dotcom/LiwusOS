#include "gui_settings.h"
#include "../core/app_registry.h"
#include "../core/event_bus.h"
#include "../scene/node.h"
#include "../widgets/window_node.h"
#include "../widgets/button.h"
#include "../widgets/label.h"
#include "../widgets/panel.h"
#include "../layout/layout_engine.h"
#include "../window/window_manager.h"
#include "kheap.h"
#include "string.h"
#include "pmm.h"
#include "sdfs.h"
#include "timer.h"
#include "edid.h"
#include "pcspkr.h"

extern uint32_t vga_fb_width;
extern uint32_t vga_fb_height;

static inline void get_cpuid_string(char *brand) {
    uint32_t *ptr = (uint32_t *)brand;
    uint32_t max_ext;
    
    // Check if CPU supports extended features
    asm volatile("cpuid" : "=a"(max_ext) : "a"(0x80000000) : "ebx", "ecx", "edx");
    if (max_ext >= 0x80000004) {
        asm volatile("cpuid" : "=a"(ptr[0]), "=b"(ptr[1]), "=c"(ptr[2]), "=d"(ptr[3]) : "a"(0x80000002));
        asm volatile("cpuid" : "=a"(ptr[4]), "=b"(ptr[5]), "=c"(ptr[6]), "=d"(ptr[7]) : "a"(0x80000003));
        asm volatile("cpuid" : "=a"(ptr[8]), "=b"(ptr[9]), "=c"(ptr[10]), "=d"(ptr[11]) : "a"(0x80000004));
        brand[48] = '\0';
        
        // Clean up leading spaces
        char *p = brand;
        while (*p == ' ') p++;
        if (p != brand) {
            memmove(brand, p, strlen(p) + 1);
        }
    } else {
        strcpy(brand, "Generic x86_64 Processor");
    }
}

static node_t *s_settings_win = NULL;
static node_t *s_content_panel = NULL;

static void clear_content_panel() {
    if (!s_content_panel) return;
    for (uint32_t i = 0; i < s_content_panel->child_count; i++) {
        // In a full implementation, we'd recursively free nodes. 
        // For now, setting child_count to 0 is enough if we don't care about the small memory leak of UI nodes until reboot.
        // Or we could implement node_destroy(s_content_panel->children[i]);
    }
    s_content_panel->child_count = 0;
}

static void show_system_settings(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    clear_content_panel();
    
    // Header
    node_t *title = label_create("sys_title", 0, 0, "System Information", 0xFFFFFFFF);
    title->margin[2] = 20; // Bottom margin
    node_add_child(s_content_panel, title);
    
    node_add_child(s_content_panel, label_create("sys_os", 0, 0, "OS: LiwusOS x86_64", 0xFF00FF00));
    node_add_child(s_content_panel, label_create("sys_ver", 0, 0, "Version: 1.0.0 (Pre-Alpha)", 0xFF00FF00));
    
    // CPU Info
    char cpu_brand[64] = "CPU: ";
    char brand_buf[49];
    get_cpuid_string(brand_buf);
    strcat(cpu_brand, brand_buf);
    node_add_child(s_content_panel, label_create("sys_cpu", 0, 0, cpu_brand, 0xFFAAAAAA));
    
    // Memory Info
    extern char *itoa(int value, char *str, int base);
    char mem_str[64] = "Memory: ";
    char buf[16];
    uint32_t mem_total_mb = pmm_get_total_memory() * 4096 / (1024 * 1024);
    uint32_t mem_used_mb = pmm_get_used_memory() * 4096 / (1024 * 1024);
    itoa(mem_used_mb, buf, 10); strcat(mem_str, buf); strcat(mem_str, " MB / ");
    itoa(mem_total_mb, buf, 10); strcat(mem_str, buf); strcat(mem_str, " MB");
    node_add_child(s_content_panel, label_create("sys_mem", 0, 0, mem_str, 0xFFAAAAAA));
    
    // Disk Info
    char disk_str[64] = "SDFS Disk: ";
    uint32_t total_blks = 0, used_blks = 0;
    sdfs_get_usage(&total_blks, &used_blks);
    uint32_t disk_used_mb = (used_blks * 4096) / (1024 * 1024);
    uint32_t disk_total_mb = (total_blks * 4096) / (1024 * 1024);
    itoa(disk_used_mb, buf, 10); strcat(disk_str, buf); strcat(disk_str, " MB / ");
    itoa(disk_total_mb, buf, 10); strcat(disk_str, buf); strcat(disk_str, " MB");
    node_add_child(s_content_panel, label_create("sys_disk", 0, 0, disk_str, 0xFFAAAAAA));
    
    // Display Info
    char disp_str[64] = "Display: ";
    itoa(vga_fb_width, buf, 10); strcat(disp_str, buf); strcat(disp_str, "x");
    itoa(vga_fb_height, buf, 10); strcat(disp_str, buf);
    node_add_child(s_content_panel, label_create("sys_disp", 0, 0, disp_str, 0xFFAAAAAA));
    
    // Uptime
    char up_str[64] = "Uptime: ";
    uint32_t up_secs = timer_ticks / 100;
    itoa(up_secs, buf, 10); strcat(up_str, buf); strcat(up_str, " seconds");
    node_add_child(s_content_panel, label_create("sys_up", 0, 0, up_str, 0xFFAAAAAA));
    
    layout_engine_compute(s_settings_win);
}

static void show_display_settings(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    clear_content_panel();
    
    node_t *title = label_create("disp_title", 0, 0, "Display Settings", 0xFFFFFFFF);
    title->margin[2] = 20;
    node_add_child(s_content_panel, title);
    
    // Fetch EDID Data
    edid_info_t edid;
    if (edid_get_monitor_info(&edid)) {
        extern char *itoa(int value, char *str, int base);
        char buf[32];
        
        // Monitor Name
        char mon_str[64] = "Monitor: ";
        strcat(mon_str, edid.manufacturer);
        strcat(mon_str, " ");
        strcat(mon_str, edid.monitor_name);
        node_add_child(s_content_panel, label_create("disp_mon", 0, 0, mon_str, 0xFF00FF00));
        
        // Year
        char year_str[64] = "Manufactured: Year ";
        itoa(edid.year_of_manufacture, buf, 10);
        strcat(year_str, buf);
        node_add_child(s_content_panel, label_create("disp_year", 0, 0, year_str, 0xFFAAAAAA));
        
        // Max Resolution
        char max_str[64] = "Max Resolution: ";
        itoa(edid.max_resolution_x, buf, 10); strcat(max_str, buf); strcat(max_str, "x");
        itoa(edid.max_resolution_y, buf, 10); strcat(max_str, buf);
        node_add_child(s_content_panel, label_create("disp_max", 0, 0, max_str, 0xFFAAAAAA));
        
        // Min Resolution
        char min_str[64] = "Min Resolution: ";
        itoa(edid.min_resolution_x, buf, 10); strcat(min_str, buf); strcat(min_str, "x");
        itoa(edid.min_resolution_y, buf, 10); strcat(min_str, buf);
        node_add_child(s_content_panel, label_create("disp_min", 0, 0, min_str, 0xFFAAAAAA));
        
        // Refresh Rate
        char hz_str[64] = "Refresh Rate: ";
        itoa(edid.refresh_rate_hz, buf, 10); strcat(hz_str, buf); strcat(hz_str, " Hz");
        node_add_child(s_content_panel, label_create("disp_hz", 0, 0, hz_str, 0xFFAAAAAA));
    } else {
        node_add_child(s_content_panel, label_create("disp_err", 0, 0, "EDID: Monitor Detection Failed", 0xFFFF0000));
    }
    
    // Some margin before buttons
    node_t *spacer = label_create("disp_spacer", 0, 0, "", 0);
    spacer->margin[2] = 20;
    node_add_child(s_content_panel, spacer);
    
    node_t *btn_800 = button_create("btn_800", 0, 0, 150, 30, "800x600");
    btn_800->margin[0] = 10;
    node_add_child(s_content_panel, btn_800);
    
    node_t *btn_1024 = button_create("btn_1024", 0, 0, 150, 30, "1024x768");
    btn_1024->margin[0] = 10;
    node_add_child(s_content_panel, btn_1024);
    
    layout_engine_compute(s_settings_win);
}

static void btn_play_beep(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    pcspkr_beep();
}

static void btn_play_mario(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    // Super Mario Bros - Overworld Theme (Snippet)
    static const note_t mario_notes[] = {
        {NOTE_E5, 150}, {NOTE_E5, 150}, {NOTE_REST, 150}, {NOTE_E5, 150}, 
        {NOTE_REST, 150}, {NOTE_C5, 150}, {NOTE_E5, 150}, {NOTE_REST, 150},
        {NOTE_G5, 300}, {NOTE_REST, 300}, {NOTE_G4, 300}, {NOTE_REST, 300}
    };
    pcspkr_play_melody(mario_notes, sizeof(mario_notes)/sizeof(note_t));
}

static void btn_play_smw_ending(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    // Super Mario World - Stage Clear (Often referred to as the ending jingle)
    // RTTTL: d=4,o=5,b=70:8g6,8g6,16e6,8g6,16e6,16g6,16e6,16d6,8g.6,16p,32d6,16d7,16e7,16d7,16e7,16d.7,32d6,32c7,32b6,16a6,8g.6,16p,8g7
    static const note_t smw_notes[] = {
        {NOTE_G5, 428}, {NOTE_G5, 428}, {NOTE_E5, 214}, {NOTE_G5, 428}, 
        {NOTE_E5, 214}, {NOTE_G5, 214}, {NOTE_E5, 214}, {NOTE_D5, 214}, 
        {NOTE_G5, 642}, {NOTE_REST, 214}, {NOTE_D5, 107}, {NOTE_D6, 214}, 
        {NOTE_E6, 214}, {NOTE_D6, 214}, {NOTE_E6, 214}, {NOTE_D6, 321}, 
        {NOTE_D5, 107}, {NOTE_C6, 107}, {NOTE_B5, 107}, {NOTE_A5, 214}, 
        {NOTE_G5, 642}, {NOTE_REST, 214}, {NOTE_G6, 428}
    };
    pcspkr_play_melody(smw_notes, sizeof(smw_notes)/sizeof(note_t));
}

static void btn_play_imperial(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    // Imperial March - Long version
    static const note_t imperial_notes[] = {
        {NOTE_G4, 600}, {NOTE_G4, 600}, {NOTE_G4, 600}, 
        {NOTE_DS4, 450}, {NOTE_AS4, 150}, {NOTE_G4, 600},
        {NOTE_DS4, 450}, {NOTE_AS4, 150}, {NOTE_G4, 1200},
        
        {NOTE_D5, 600}, {NOTE_D5, 600}, {NOTE_D5, 600}, 
        {NOTE_DS5, 450}, {NOTE_AS4, 150}, {NOTE_FS4, 600},
        {NOTE_DS4, 450}, {NOTE_AS4, 150}, {NOTE_G4, 1200}
    };
    pcspkr_play_melody(imperial_notes, sizeof(imperial_notes)/sizeof(note_t));
}

static void show_sound_settings(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    clear_content_panel();
    
    node_t *title = label_create("snd_title", 0, 0, "Sound Settings", 0xFFFFFFFF);
    title->margin[2] = 20;
    node_add_child(s_content_panel, title);
    
    node_t *warn1 = label_create("snd_warn1", 0, 0, "NOTE: High Definition Audio (PCM) is not yet supported.", 0xFFF59E0B);
    node_t *warn2 = label_create("snd_warn2", 0, 0, "Audio output is currently routed through the 8-bit PC Speaker (Beeper).", 0xFFAAAAAA);
    warn2->margin[2] = 20;
    
    node_add_child(s_content_panel, warn1);
    node_add_child(s_content_panel, warn2);
    
    node_t *vol_lbl = label_create("snd_vol", 0, 0, "Volume: 100% (Fixed by Hardware)", 0xFFFFFFFF);
    vol_lbl->margin[2] = 20;
    node_add_child(s_content_panel, vol_lbl);
    
    node_t *btn_beep = button_create("btn_beep", 0, 0, 200, 30, "Test System Beep");
    btn_beep->margin[2] = 10;
    button_set_on_click(btn_beep, btn_play_beep, NULL);
    node_add_child(s_content_panel, btn_beep);
    
    node_t *btn_mario = button_create("btn_mario", 0, 0, 200, 30, "Play Super Mario");
    btn_mario->margin[2] = 10;
    button_set_on_click(btn_mario, btn_play_mario, NULL);
    node_add_child(s_content_panel, btn_mario);
    
    node_t *btn_smw = button_create("btn_smw", 0, 0, 250, 30, "Play SMW Ending Theme");
    btn_smw->margin[2] = 10;
    button_set_on_click(btn_smw, btn_play_smw_ending, NULL);
    node_add_child(s_content_panel, btn_smw);
    
    node_t *btn_imp = button_create("btn_imp", 0, 0, 200, 30, "Play Imperial March");
    btn_imp->margin[2] = 10;
    button_set_on_click(btn_imp, btn_play_imperial, NULL);
    node_add_child(s_content_panel, btn_imp);
    
    layout_engine_compute(s_settings_win);
}

static void show_network_settings(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    clear_content_panel();
    
    node_t *title = label_create("net_title", 0, 0, "Network Settings", 0xFFFFFFFF);
    title->margin[2] = 20;
    node_add_child(s_content_panel, title);
    
    extern uint32_t netstack_get_my_ip(void);
    uint32_t ip = netstack_get_my_ip();
    char ip_str[32] = "IP: ";
    if (ip == 0) {
        strcat(ip_str, "Offline");
    } else {
        extern char *itoa(int value, char *str, int base);
        char buf[8];
        itoa((int)(ip & 0xFF), buf, 10); strcat(ip_str, buf); strcat(ip_str, ".");
        itoa((int)((ip >> 8) & 0xFF), buf, 10); strcat(ip_str, buf); strcat(ip_str, ".");
        itoa((int)((ip >> 16) & 0xFF), buf, 10); strcat(ip_str, buf); strcat(ip_str, ".");
        itoa((int)((ip >> 24) & 0xFF), buf, 10); strcat(ip_str, buf);
    }
    
    node_add_child(s_content_panel, label_create("net_ip", 0, 0, ip_str, 0xFFAAAAAA));
    node_add_child(s_content_panel, label_create("net_dhcp", 0, 0, "DHCP: Enabled", 0xFFAAAAAA));
    node_add_child(s_content_panel, label_create("net_mac", 0, 0, "MAC: QEMU Default", 0xFFAAAAAA));
    
    layout_engine_compute(s_settings_win);
}

static void settings_win_close(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    if (s_settings_win) {
        extern gui_event_bus_t *g_event_bus;
        gui_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = GUI_EVENT_WIN_CLOSE;
        ev.generic.a = (uint64_t)s_settings_win;
        event_bus_post(g_event_bus, &ev);
        s_settings_win = NULL;
    }
}

static void settings_app_start(void) {
    if (s_settings_win) return; // Already open
    
    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return;
    
    s_settings_win = window_node_create("win_settings", 150, 100, 600, 400, "Settings");
    if (!s_settings_win) return;
    
    s_settings_win->layout_type = LAYOUT_HBOX;
    s_settings_win->padding[0] = 30; // Title bar height
    s_settings_win->padding[1] = 0;
    s_settings_win->padding[2] = 0;
    s_settings_win->padding[3] = 0;
    
    // Sidebar
    node_t *sidebar = panel_create("settings_sidebar", 0, 0, 150, 400, 0x441A1A1A);
    sidebar->layout_type = LAYOUT_VBOX;
    sidebar->padding[0] = 10;
    sidebar->padding[1] = 10;
    sidebar->padding[2] = 10;
    sidebar->padding[3] = 10;
    sidebar->layout_align = ALIGN_STRETCH; // Stretch vertically
    node_add_child(s_settings_win, sidebar);
    
    // Sidebar Buttons
    node_t *btn_sys = button_create("btn_sys", 0, 0, 130, 40, "System");
    btn_sys->layout_align = ALIGN_STRETCH;
    btn_sys->margin[2] = 5;
    button_set_on_click(btn_sys, show_system_settings, NULL);
    node_add_child(sidebar, btn_sys);
    
    node_t *btn_disp = button_create("btn_disp", 0, 0, 130, 40, "Display");
    btn_disp->layout_align = ALIGN_STRETCH;
    btn_disp->margin[2] = 5;
    button_set_on_click(btn_disp, show_display_settings, NULL);
    node_add_child(sidebar, btn_disp);
    
    node_t *btn_snd = button_create("btn_snd", 0, 0, 130, 40, "Sound");
    btn_snd->layout_align = ALIGN_STRETCH;
    btn_snd->margin[2] = 5;
    button_set_on_click(btn_snd, show_sound_settings, NULL);
    node_add_child(sidebar, btn_snd);
    
    node_t *btn_net = button_create("btn_net", 0, 0, 130, 40, "Network");
    btn_net->layout_align = ALIGN_STRETCH;
    btn_net->margin[2] = 5;
    button_set_on_click(btn_net, show_network_settings, NULL);
    node_add_child(sidebar, btn_net);
    
    // Content Panel
    s_content_panel = panel_create("settings_content", 0, 0, 450, 400, 0x44252525);
    s_content_panel->layout_type = LAYOUT_VBOX;
    s_content_panel->flex_weight = 1; // Take remaining horizontal space
    s_content_panel->layout_align = ALIGN_STRETCH; // Stretch vertically
    s_content_panel->padding[0] = 20;
    s_content_panel->padding[1] = 20;
    s_content_panel->padding[2] = 20;
    s_content_panel->padding[3] = 20;
    node_add_child(s_settings_win, s_content_panel);
    
    node_add_child(g_scene->root, s_settings_win);
    window_manager_bring_to_front(s_settings_win);
    
    // Show default tab
    show_system_settings(NULL, NULL);
}

void app_settings_init(void) {
    app_registry_add("Settings", "settings_icon", settings_app_start);
}
