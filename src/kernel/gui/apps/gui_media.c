/*
 * src/kernel/gui/apps/gui_media.c
 *
 * Multimedia player. For now it only plays MP3 files that were synced at
 * boot from the initrd to the SDFS /music directory.
 *
 * The window lists every song as a button; clicking one requests playback
 * through audio_song_request(), which is served by the background
 * "media" kernel task (src/drivers/mp3.c).
 */

#include "gui_media.h"
#include "../core/app_registry.h"
#include "../scene/node.h"
#include "../widgets/window_node.h"
#include "../widgets/button.h"
#include "../widgets/label.h"
#include "../widgets/panel.h"
#include "../layout/layout_engine.h"
#include "../window/window_manager.h"
#include "kheap.h"
#include "string.h"
#include "mp3.h"
#include "fs/pen.h"
#include "task.h"

#define SONG_BTN_MAX 60

static node_t *s_media_win = NULL;
static node_t *s_status_lbl = NULL;
static node_t *s_title_lbl = NULL;
static node_t *s_song_btns[SONG_BTN_MAX];
static uint32_t s_song_btn_count = 0;
static uint32_t s_last_song_gen = 0;

static void app_media_start(void);

/* The same track can be visible both in the bundled /music directory and
 * on the pendrive.  The player is source-agnostic, so present it only once
 * instead of making the library look as though it contains duplicates. */
static int song_name_equal(const char *a, const char *b) {
    if (!a || !b) return 0;
    while (*a && *b) {
        char ca = *a++;
        char cb = *b++;
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + ('a' - 'A'));
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + ('a' - 'A'));
        if (ca != cb) return 0;
    }
    return *a == '\0' && *b == '\0';
}

static void play_song_click(node_t *btn, void *userdata) {
    (void)btn;
    uint32_t idx = (uint32_t)(uint64_t)userdata;
    const char *path = mp3_song_path(idx);
    const char *name = mp3_song_name(idx);
    if (!path) return;
    audio_song_request(path);
    if (s_status_lbl) {
        char txt[96] = "Carregando: ";
        if (name) strcat(txt, name);
        label_set_text(s_status_lbl, txt);
    }
    if (s_media_win) layout_engine_compute(s_media_win);
}

static void stop_music_click(node_t *btn, void *userdata) {
    (void)btn; (void)userdata;
    audio_song_stop();
    if (s_status_lbl) label_set_text(s_status_lbl, "Stopped");
    if (s_media_win) layout_engine_compute(s_media_win);
}

static void rebuild_song_list(void) {
    node_t *rest[NODE_MAX_CHILDREN];
    uint32_t rest_count;
    uint32_t count;
    uint32_t i;

    if (!s_media_win) return;

    /* Destroy the song buttons we previously created. */
    for (i = 0; i < s_song_btn_count; i++) {
        node_t *b = s_song_btns[i];
        if (b) {
            node_remove_child(s_media_win, b);
            node_destroy(b);
        }
    }
    s_song_btn_count = 0;

    /* Detach the remaining children (title, status, stop) so we can
     * re-insert them in a stable order around the fresh song list. */
    rest_count = s_media_win->child_count;
    for (i = 0; i < rest_count; i++) {
        rest[i] = s_media_win->children[i];
    }
    while (s_media_win->child_count > 0) {
        node_remove_child(s_media_win, s_media_win->children[0]);
    }

    /* Order: title, then song buttons, then everything else. */
    for (i = 0; i < rest_count; i++) {
        if (rest[i] == s_title_lbl) node_add_child(s_media_win, rest[i]);
    }

    count = mp3_song_count();
    if (count > SONG_BTN_MAX) count = SONG_BTN_MAX;

    if (!count) {
        if (s_status_lbl) label_set_text(s_status_lbl, "No MP3 songs available");
    } else {
        for (i = 0; i < count; i++) {
            const char *name = mp3_song_name(i);
            int duplicate = 0;
            for (uint32_t j = 0; j < i; j++) {
                if (song_name_equal(name, mp3_song_name(j))) {
                    duplicate = 1;
                    break;
                }
            }
            if (duplicate) continue;
            node_t *b = button_create("song", 0, 0, 300, 32,
                                      name ? name : "Unknown");
            b->margin[2] = 6;
            b->layout_align = ALIGN_CENTER;
            button_set_on_click(b, play_song_click, (void *)(uint64_t)i);
            node_add_child(s_media_win, b);
            s_song_btns[s_song_btn_count++] = b;
        }
    }

    for (i = 0; i < rest_count; i++) {
        if (rest[i] != s_title_lbl) node_add_child(s_media_win, rest[i]);
    }
}

static const char *status_label_text(void) {
    const char *st = mp3_status();
    if (!st) return "Idle";
    if (strcmp(st, "playing") == 0) return "Tocando...";
    if (strcmp(st, "decoding") == 0) return "Carregando...";
    if (strcmp(st, "requested") == 0) return "Carregando...";
    if (strcmp(st, "stopped") == 0) return "Parado";
    if (strcmp(st, "idle") == 0) return "Pronto";
    if (strcmp(st, "not found") == 0) return "Musica nao encontrada";
    if (strcmp(st, "decode error") == 0) return "Erro ao decodificar";
    return st;
}

static void media_refresh_task(void) {
    char last_status[96];
    last_status[0] = '\0';
    for (;;) {
        uint32_t gen = pen_song_gen();
        if (s_media_win && gen != s_last_song_gen) {
            s_last_song_gen = gen;
            rebuild_song_list();
            if (s_media_win) layout_engine_compute(s_media_win);
        }
        if (s_media_win && s_status_lbl) {
            const char *st = status_label_text();
            if (strcmp(st, last_status) != 0) {
                char txt[96];
                strncpy(txt, st, sizeof(txt) - 1);
                txt[sizeof(txt) - 1] = '\0';
                if (audio_song_name()[0] && (strcmp(st, "Tocando...") == 0 ||
                    strcmp(st, "Carregando...") == 0)) {
                    strncat(txt, ": ", sizeof(txt) - strlen(txt) - 1);
                    strncat(txt, audio_song_name(),
                            sizeof(txt) - strlen(txt) - 1);
                }
                label_set_text(s_status_lbl, txt);
                strncpy(last_status, st, sizeof(last_status) - 1);
                last_status[sizeof(last_status) - 1] = '\0';
                layout_engine_compute(s_media_win);
            }
        }
        for (int k = 0; k < 12; k++) {
            switch_task();
        }
    }
}

static void app_media_start(void) {
    if (s_media_win) return; /* Already open */

    extern scene_graph_t *g_scene;
    if (!g_scene || !g_scene->root) return;

    s_media_win = window_node_create("win_media", 180, 110, 480, 360, "Multimedia");
    if (!s_media_win) return;

    s_media_win->layout_type = LAYOUT_VBOX;
    s_media_win->padding[0] = 30; /* Title bar height */
    s_media_win->padding[1] = 16;
    s_media_win->padding[2] = 16;
    s_media_win->padding[3] = 16;

    node_t *title = label_create("media_title", 0, 0, "Music (MP3)", 0xFFFFFFFF);
    title->margin[2] = 12;
    node_add_child(s_media_win, title);
    s_title_lbl = title;

    rebuild_song_list();

    s_status_lbl = label_create("media_status", 0, 0, "Idle", 0xFFAAAAAA);
    s_status_lbl->margin[2] = 8;
    node_add_child(s_media_win, s_status_lbl);

    node_t *btn_stop = button_create("media_stop", 0, 0, 140, 30, "Stop");
    btn_stop->margin[2] = 6;
    btn_stop->layout_align = ALIGN_CENTER;
    button_set_on_click(btn_stop, stop_music_click, NULL);
    node_add_child(s_media_win, btn_stop);

    node_add_child(g_scene->root, s_media_win);
    window_manager_bring_to_front(s_media_win);

    layout_engine_compute(s_media_win);
}

void app_media_init(void) {
    s_last_song_gen = pen_song_gen();
    create_task_named(media_refresh_task, "media_refresh");
    app_registry_add("Multimedia", "audio_icon", app_media_start);
}
