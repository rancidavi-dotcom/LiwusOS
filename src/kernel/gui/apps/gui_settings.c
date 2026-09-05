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
#include "audio.h"
#include "mp3.h"
#include "pcspkr.h"
#include "task.h"

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
    node_t *title = label_create("sys_title", 0, 0, "System Information", 0xFF00FF41);
    title->margin[2] = 20; // Bottom margin
    node_add_child(s_content_panel, title);
    
    node_add_child(s_content_panel, label_create("sys_os", 0, 0, "OS: LiwusOS x86_64", 0xFF00FF41));
    node_add_child(s_content_panel, label_create("sys_ver", 0, 0, "Version: 1.0.0 (Pre-Alpha)", 0xFF00FF41));
    
    // CPU Info
    char cpu_brand[64] = "CPU: ";
    char brand_buf[49];
    get_cpuid_string(brand_buf);
    strcat(cpu_brand, brand_buf);
    node_add_child(s_content_panel, label_create("sys_cpu", 0, 0, cpu_brand, 0xFF00CC33));
    
    // Memory Info
    extern char *itoa(int value, char *str, int base);
    char mem_str[64] = "Memory: ";
    char buf[16];
    uint32_t mem_total_mb = pmm_get_total_memory() * 4096 / (1024 * 1024);
    uint32_t mem_used_mb = pmm_get_used_memory() * 4096 / (1024 * 1024);
    itoa(mem_used_mb, buf, 10); strcat(mem_str, buf); strcat(mem_str, " MB / ");
    itoa(mem_total_mb, buf, 10); strcat(mem_str, buf); strcat(mem_str, " MB");
    node_add_child(s_content_panel, label_create("sys_mem", 0, 0, mem_str, 0xFF00CC33));
    
    // Disk Info
    char disk_str[64] = "SDFS Disk: ";
    uint32_t total_blks = 0, used_blks = 0;
    sdfs_get_usage(&total_blks, &used_blks);
    uint32_t disk_used_mb = (used_blks * 4096) / (1024 * 1024);
    uint32_t disk_total_mb = (total_blks * 4096) / (1024 * 1024);
    itoa(disk_used_mb, buf, 10); strcat(disk_str, buf); strcat(disk_str, " MB / ");
    itoa(disk_total_mb, buf, 10); strcat(disk_str, buf); strcat(disk_str, " MB");
    node_add_child(s_content_panel, label_create("sys_disk", 0, 0, disk_str, 0xFF00CC33));
    
    // Display Info
    char disp_str[64] = "Display: ";
    itoa(vga_fb_width, buf, 10); strcat(disp_str, buf); strcat(disp_str, "x");
    itoa(vga_fb_height, buf, 10); strcat(disp_str, buf);
    node_add_child(s_content_panel, label_create("sys_disp", 0, 0, disp_str, 0xFF00CC33));
    
    // Uptime
    char up_str[64] = "Uptime: ";
    uint32_t up_secs = timer_ticks / 100;
    itoa(up_secs, buf, 10); strcat(up_str, buf); strcat(up_str, " seconds");
    node_add_child(s_content_panel, label_create("sys_up", 0, 0, up_str, 0xFF00CC33));
    
    layout_engine_compute(s_settings_win);
}

static void show_display_settings(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    clear_content_panel();
    
    node_t *title = label_create("disp_title", 0, 0, "Display Settings", 0xFF00FF41);
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
        node_add_child(s_content_panel, label_create("disp_mon", 0, 0, mon_str, 0xFF00FF41));
        
        // Year
        char year_str[64] = "Manufactured: Year ";
        itoa(edid.year_of_manufacture, buf, 10);
        strcat(year_str, buf);
        node_add_child(s_content_panel, label_create("disp_year", 0, 0, year_str, 0xFF00CC33));
        
        // Max Resolution
        char max_str[64] = "Max Resolution: ";
        itoa(edid.max_resolution_x, buf, 10); strcat(max_str, buf); strcat(max_str, "x");
        itoa(edid.max_resolution_y, buf, 10); strcat(max_str, buf);
        node_add_child(s_content_panel, label_create("disp_max", 0, 0, max_str, 0xFF00CC33));
        
        // Min Resolution
        char min_str[64] = "Min Resolution: ";
        itoa(edid.min_resolution_x, buf, 10); strcat(min_str, buf); strcat(min_str, "x");
        itoa(edid.min_resolution_y, buf, 10); strcat(min_str, buf);
        node_add_child(s_content_panel, label_create("disp_min", 0, 0, min_str, 0xFF00CC33));
        
        // Refresh Rate
        char hz_str[64] = "Refresh Rate: ";
        itoa(edid.refresh_rate_hz, buf, 10); strcat(hz_str, buf); strcat(hz_str, " Hz");
        node_add_child(s_content_panel, label_create("disp_hz", 0, 0, hz_str, 0xFF00CC33));
    } else {
        node_add_child(s_content_panel, label_create("disp_err", 0, 0, "EDID: Monitor Detection Failed", 0xFFFF4444));
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

/* ------------------------------------------------------------------ */
/* Sound tests (async, so the GUI keeps running during playback)       */
/* ------------------------------------------------------------------ */

static volatile int snd_test_req = 0;

static void snd_test_task(void) {
    for (;;) {
        if (!snd_test_req) {
            asm volatile("hlt");
            continue;
        }
        int req = snd_test_req;
        snd_test_req = 0;
        /* Wait (up to ~0.5s) for any playback in progress to finish. */
        for (int t = 0; t < 50 && audio_is_busy(); t++)
            switch_task();
        switch (req) {
        case 1: /* Test Tone (sine 880 Hz, 150 ms) */
            audio_play_tone(880, 150, 80);
            break;
        case 2: { /* Super Mario Bros - Overworld Theme (snippet) */
            static const audio_note_t mario_notes[] = {
                {NOTE_E5, 150}, {NOTE_E5, 150}, {NOTE_REST, 150}, {NOTE_E5, 150},
                {NOTE_REST, 150}, {NOTE_C5, 150}, {NOTE_E5, 150}, {NOTE_REST, 150},
                {NOTE_G5, 300}, {NOTE_REST, 300}, {NOTE_G4, 300}, {NOTE_REST, 300}
            };
            audio_play_notes(mario_notes, sizeof(mario_notes)/sizeof(audio_note_t),
                             AUDIO_DEFAULT_RATE, 80);
            break;
        }
        case 3: { /* Super Mario World - Stage Clear jingle */
            static const audio_note_t smw_notes[] = {
                {NOTE_G5, 428}, {NOTE_G5, 428}, {NOTE_E5, 214}, {NOTE_G5, 428},
                {NOTE_E5, 214}, {NOTE_G5, 214}, {NOTE_E5, 214}, {NOTE_D5, 214},
                {NOTE_G5, 642}, {NOTE_REST, 214}, {NOTE_D5, 107}, {NOTE_D6, 214},
                {NOTE_E6, 214}, {NOTE_D6, 214}, {NOTE_E6, 214}, {NOTE_D6, 321},
                {NOTE_D5, 107}, {NOTE_C6, 107}, {NOTE_B5, 107}, {NOTE_A5, 214},
                {NOTE_G5, 642}, {NOTE_REST, 214}, {NOTE_G6, 428}
            };
            audio_play_notes(smw_notes, sizeof(smw_notes)/sizeof(audio_note_t),
                             AUDIO_DEFAULT_RATE, 80);
            break;
        }
        case 4: { /* Imperial March - Long version */
            static const audio_note_t imperial_notes[] = {
                {NOTE_G4, 600}, {NOTE_G4, 600}, {NOTE_G4, 600},
                {NOTE_DS4, 450}, {NOTE_AS4, 150}, {NOTE_G4, 600},
                {NOTE_DS4, 450}, {NOTE_AS4, 150}, {NOTE_G4, 1200},
                {NOTE_D5, 600}, {NOTE_D5, 600}, {NOTE_D5, 600},
                {NOTE_DS5, 450}, {NOTE_AS4, 150}, {NOTE_FS4, 600},
                {NOTE_DS4, 450}, {NOTE_AS4, 150}, {NOTE_G4, 1200}
            };
            audio_play_notes(imperial_notes, sizeof(imperial_notes)/sizeof(audio_note_t),
                             AUDIO_DEFAULT_RATE, 80);
            break;
        }
        case 5: { /* Synthesize a small RIFF/WAV in RAM and play it. */
            if (!audio_available()) break;
            uint32_t rate = 24000;
            uint32_t samples = rate / 2; /* 0.5 s, mono/16-bit */
            uint32_t data_len = samples * 2;
            uint32_t wav_len = 44 + data_len;
            uint8_t *wav = (uint8_t *)kmalloc(wav_len);
            if (!wav) break;

            *(uint32_t *)(wav + 0)  = 0x46464952;          /* "RIFF" */
            *(uint32_t *)(wav + 4)  = 36 + data_len;
            *(uint32_t *)(wav + 8)  = 0x45564157;          /* "WAVE" */
            *(uint32_t *)(wav + 12) = 0x20746D66;          /* "fmt " */
            *(uint32_t *)(wav + 16) = 16;
            *(uint16_t *)(wav + 20) = 1;                   /* PCM */
            *(uint16_t *)(wav + 22) = 1;                   /* mono */
            *(uint32_t *)(wav + 24) = rate;
            *(uint32_t *)(wav + 28) = rate * 2;            /* byte rate */
            *(uint16_t *)(wav + 32) = 2;                   /* block align */
            *(uint16_t *)(wav + 34) = 16;                  /* bits/sample */
            *(uint32_t *)(wav + 36) = 0x61746164;          /* "data" */
            *(uint32_t *)(wav + 40) = data_len;

            /* Square wave at 220 Hz (no FP, no libm needed). */
            const uint16_t w = rate / 220; /* samples per half period */
            for (uint32_t i = 0; i < samples; i++) {
                int16_t v = ((i / w) & 1) ? 8000 : -8000;
                *(int16_t *)(wav + 44 + i * 2) = v;
            }

            audio_play_wav(wav, wav_len, 80);
            kfree(wav);
            break;
        }
        default:
            break;
        }
    }
}

static void btn_play_beep(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    snd_test_req = 1;
}

static void btn_play_wav(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    snd_test_req = 5;
}

/* Play the first MP3 synced at boot through the media task. */
static void btn_play_mp3(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    if (mp3_song_count() > 0) {
        audio_song_request(mp3_song_path(0));
    }
}

/* ------------------------------------------------------------------ */
/* Sound tab: hardware volume / rate configuration                    */
/* ------------------------------------------------------------------ */

#define SND_CFG_PATH "/system/snd.cfg"

void sound_config_apply(void);
static void sound_config_save(void);
static void show_sound_settings(node_t *btn, void *userdata);

/* Persist volume + rate + mute to SDFS so the choice survives reboot. */
static void sound_config_save(void) {
    if (!sdfs_is_mounted()) return;
    extern char *itoa(int value, char *str, int base);
    sdfs_create_dir("/system");

    char buf[56];
    char num[8];
    buf[0] = 'V';
    buf[1] = 'O';
    buf[2] = 'L';
    buf[3] = '=';
    itoa(audio_get_volume(), num, 10);
    strcpy(buf + 4, num);
    strcat(buf, "\nRATE=");
    itoa((int)audio_get_rate(), num, 10);
    strcat(buf, num);
    strcat(buf, "\nMUTE=");
    itoa(audio_get_muted(), num, 10);
    strcat(buf, num);
    strcat(buf, "\n");
    sdfs_write_file(SND_CFG_PATH, (uint8_t *)buf, (uint32_t)strlen(buf));
}

/* Load "VOL=nn\nRATE=nn\nMUTE=n\n" from SDFS and apply it to the AC'97
 * mixer. */
void sound_config_apply(void) {
    if (!sdfs_is_mounted()) return;
    uint32_t size = 0;
    char *data = (char *)sdfs_read_file(SND_CFG_PATH, &size);
    if (!data || size == 0) {
        if (data) kfree(data);
        return;
    }

    int vol = -1;
    int rate = -1;
    int mute = -1;
    char *p = data;
    char *end = data + size;
    while (p && p < end) {
        char *nl = p;
        while (nl < end && *nl != '\n') nl++;
        int n = (int)(nl - p);
        if (n >= 3 && p[0] == 'V' && p[1] == 'O' && p[2] == 'L' && p[3] == '=') {
            vol = 0;
            for (int i = 4; i < n && p[i] >= '0' && p[i] <= '9'; i++)
                vol = vol * 10 + (p[i] - '0');
        } else if (n >= 6 && p[0] == 'R' && p[1] == 'A' && p[2] == 'T' &&
                   p[3] == 'E' && p[4] == '=') {
            rate = 0;
            for (int i = 5; i < n && p[i] >= '0' && p[i] <= '9'; i++)
                rate = rate * 10 + (p[i] - '0');
        } else if (n >= 5 && p[0] == 'M' && p[1] == 'U' && p[2] == 'T' &&
                   p[3] == 'E' && p[4] == '=') {
            mute = 0;
            for (int i = 5; i < n && p[i] >= '0' && p[i] <= '9'; i++)
                mute = mute * 10 + (p[i] - '0');
        }
        p = nl + 1;
    }
    kfree(data);

    if (vol >= 0) audio_set_volume(vol);
    if (mute >= 0) audio_set_muted(mute ? 1 : 0);
    if (rate >= 0 && rate >= 8000 && rate <= 96000) audio_set_rate((uint32_t)rate);
}

/* Rebuild the sound panel after any change. */
static void sound_panel_refresh(void) {
    clear_content_panel();
    show_sound_settings(NULL, NULL);
}

static void snd_vol_down(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    int v = audio_get_volume();
    v = (v < 5) ? 0 : v - 5;
    audio_set_volume(v);
    if (!audio_get_muted())
        sound_config_save();
    sound_panel_refresh();
}

static void snd_vol_up(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    int v = audio_get_volume();
    v = (v > 95) ? 100 : v + 5;
    audio_set_volume(v);
    if (!audio_get_muted())
        sound_config_save();
    sound_panel_refresh();
}

static void snd_toggle_mute(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    audio_set_muted(!audio_get_muted());
    sound_config_save();
    sound_panel_refresh();
}

static void snd_set_rate(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    uint32_t hz = (uint32_t)(uint64_t)userdata;
    if (audio_set_rate(hz) == 0) {
        audio_set_volume(audio_get_volume());   /* re-apply mixer after rate */
        sound_config_save();
        sound_panel_refresh();
    }
}

static void show_sound_settings(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    clear_content_panel();
    extern char *itoa(int value, char *str, int base);

    /* Spawn the sound-test worker once, so test buttons never block the
     * GUI task for the whole duration of a sample. */
    static int snd_task_started = 0;
    if (!snd_task_started) {
        create_task_named(snd_test_task, "sndtest");
        snd_task_started = 1;
    }

    node_t *title = label_create("snd_title", 0, 0, "Sound", 0xFF00FF41);
    title->margin[2] = 8;
    node_add_child(s_content_panel, title);

    /* Hardware status */
    if (audio_available()) {
        node_t *hw = label_create("snd_hw", 0, 0,
            "AC'97 controller (QEMU Intel 82801AA) - Analog Line Out", 0xFF00FF41);
        hw->margin[2] = 12;
        node_add_child(s_content_panel, hw);
    } else {
        node_t *warn = label_create("snd_warn", 0, 0,
            "No AC'97 sound card. Start QEMU with: -device AC97,audiodev=aud0", 0xFFFF4444);
        warn->margin[2] = 12;
        node_add_child(s_content_panel, warn);
    }

    int vol = audio_get_volume();

    /* Volume bar (monospace block bar, 25 chars = 4% each) */
    char bar[26];
    int filled = (vol * 25) / 100;
    for (int i = 0; i < 25; i++) bar[i] = (i < filled) ? '#' : '.';
    bar[25] = '\0';
    node_t *bar_lbl = label_create("snd_bar", 0, 0, bar, 0xFF00FF41);
    node_add_child(s_content_panel, bar_lbl);

    char vol_str[48] = "Volume: ";
    char vbuf[8];
    itoa(vol, vbuf, 10);
    strcat(vol_str, vbuf);
    strcat(vol_str, "%  ");
    strcat(vol_str, audio_get_muted() ? "[MUTED]" : "[ON]");
    node_t *vol_lbl = label_create("snd_vol", 0, 0, vol_str, 0xFF00FF41);
    vol_lbl->margin[2] = 6;
    node_add_child(s_content_panel, vol_lbl);

    /* Volume row: - / + / Mute */
    node_t *row1 = panel_create("snd_volrow", 0, 0, 400, 34, 0x330A2E1A);
    row1->layout_type = LAYOUT_HBOX;
    row1->margin[2] = 10;
    row1->padding[0] = 2;
    row1->padding[1] = 4;
    row1->padding[2] = 2;
    row1->padding[3] = 4;

    node_t *btn_minus = button_create("snd_minus", 0, 0, 50, 30, "-");
    button_set_on_click(btn_minus, snd_vol_down, NULL);
    node_add_child(row1, btn_minus);

    node_t *btn_mute = button_create("snd_mute", 0, 0, 70, 30,
                                audio_get_muted() ? "Unmute" : "Mute");
    btn_mute->margin[0] = 8;
    button_set_on_click(btn_mute, snd_toggle_mute, NULL);
    node_add_child(row1, btn_mute);

    node_t *btn_plus = button_create("snd_plus", 0, 0, 50, 30, "+");
    btn_plus->margin[0] = 8;
    button_set_on_click(btn_plus, snd_vol_up, NULL);
    node_add_child(row1, btn_plus);
    node_add_child(s_content_panel, row1);

    /* Sample rate row */
    char rate_str[48] = "Sample rate: ";
    itoa((int)audio_get_rate(), vbuf, 10);
    strcat(rate_str, vbuf);
    strcat(rate_str, " Hz");
    node_t *rate_lbl = label_create("snd_rate", 0, 0, rate_str, 0xFF00CC33);
    node_add_child(s_content_panel, rate_lbl);

    node_t *row2 = panel_create("snd_rates", 0, 0, 400, 34, 0x330A2E1A);
    row2->layout_type = LAYOUT_HBOX;
    row2->margin[2] = 12;
    row2->padding[0] = 2;
    row2->padding[1] = 4;
    row2->padding[2] = 2;
    row2->padding[3] = 4;
    static const uint32_t rates[] = { 32000, 44100, 48000 };
    for (int i = 0; i < 3; i++) {
        char rtxt[8];
        itoa((int)rates[i], rtxt, 10);
        node_t *b = button_create("snd_rate_btn", 0, 0, 70, 30, rtxt);
        if (i > 0) b->margin[0] = 6;
        button_set_on_click(b, snd_set_rate, (void *)(uint64_t)rates[i]);
        node_add_child(row2, b);
    }
    /* PCM 16-bit at 48 kHz is the AC'97 hardware rate; MP3 resamples. */
    node_add_child(s_content_panel, row2);

    node_t *hint = label_create("snd_hint", 0, 0,
        "Output: AC'97 virtual card -> QEMU audio backend -> Windows speakers",
        0xFF00CC33);
    hint->margin[2] = 12;
    node_add_child(s_content_panel, hint);

    /* Playback tests */
    node_t *btn_beep = button_create("btn_beep", 0, 0, 180, 30, "Test Tone");
    btn_beep->margin[2] = 8;
    button_set_on_click(btn_beep, btn_play_beep, NULL);
    node_add_child(s_content_panel, btn_beep);

    node_t *btn_wav = button_create("btn_wav", 0, 0, 180, 30, "Play PCM WAV");
    btn_wav->margin[2] = 8;
    button_set_on_click(btn_wav, btn_play_wav, NULL);
    node_add_child(s_content_panel, btn_wav);

    node_t *btn_mp3 = button_create("btn_mp3", 0, 0, 180, 30, "Play MP3");
    btn_mp3->margin[2] = 8;
    button_set_on_click(btn_mp3, btn_play_mp3, NULL);
    node_add_child(s_content_panel, btn_mp3);

    layout_engine_compute(s_settings_win);
}

static void show_network_settings(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    clear_content_panel();
    
    node_t *title = label_create("net_title", 0, 0, "Network Settings", 0xFF00FF41);
    title->margin[2] = 20;
    node_add_child(s_content_panel, title);
    
    char ip_str[32] = "IP: Offline";
    
    node_add_child(s_content_panel, label_create("net_ip", 0, 0, ip_str, 0xFF00CC33));
    node_add_child(s_content_panel, label_create("net_dhcp", 0, 0, "DHCP: Enabled", 0xFF00CC33));
    node_add_child(s_content_panel, label_create("net_mac", 0, 0, "MAC: QEMU Default", 0xFF00CC33));
    
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
    node_t *sidebar = panel_create("settings_sidebar", 0, 0, 150, 400, 0xFF0A2E1A);
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
    s_content_panel = panel_create("settings_content", 0, 0, 450, 400, 0xFF0A1510);
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
