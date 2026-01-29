/*
 * src/drivers/mp3.c
 *
 * MP3 playback for LiwusOS.
 *
 * Wraps the public-domain minimp3 decoder (include/drivers/minimp3.h) and
 * provides:
 *   - mp3_decode_pcm16(): decode a whole MP3 buffer to raw 16-bit PCM
 *   - mp3_init():         boot-time sync of music files from the initrd to
 *                         the persistent SDFS + in-memory song list
 *   - media_task():       background kernel task that decodes and plays
 *                         requested songs through the AC'97 audio driver
 *
 * The kernel is compiled with -mno-sse/-mno-sse2, so the minimp3 SIMD path
 * is disabled (MINIMP3_NO_SIMD forces the portable C implementation, which
 * only needs the x87 FPU — enabled by init_fpu()).
 */

#include "mp3.h"
#include "audio.h"
#include "sdfs.h"
#include "initrd.h"
#include "fs/pen.h"
#include "serial.h"
#include "string.h"
#include "kheap.h"
#include "stdint.h"
#include "task.h"
#include "timer.h"

#define MINIMP3_NO_SIMD
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

/* MP3 -> PCM16 expansion heuristic. Worst practical case (low bitrate,
 * stereo) expands less than ~16 samples per compressed byte. */
#define MP3_EXPAND_MAX 16u

/* ------------------------------------------------------------------ */
/* Song list + playback state                                          */
/* ------------------------------------------------------------------ */

static char          s_songs[MP3_MAX_SONGS][MP3_MAX_PATH];
static char          s_song_names[MP3_MAX_SONGS][64];
static uint32_t      s_song_count = 0;
static volatile int  s_play_request = 0;
static char          s_request_path[MP3_MAX_PATH];
static char          s_playing_name[MP3_MAX_PATH];
static char          s_status[64] = "idle";
static char          s_pen_path[MP3_MAX_PATH];

/* minimp3's decoder state (mp3dec_t = ~6.7KB) must NOT live on the kernel
 * task stack (8KB). Static; the decoder is only used by the "media" task. */
static mp3dec_t s_dec;

/* FXSAVE area for the media task (see task_set_fpu in sched/task.c). */
static uint8_t s_fpu_ctx[512] __attribute__((aligned(64)));

/* Streaming playback state. Filled chunk-at-a-time by mp3_stream_fill() so
 * the song starts as soon as the first frame is decoded (no full-file PCM
 * buffer, no startup delay). Only the "media" task touches it. */
typedef struct {
    const uint8_t *data;      /* MP3 file in memory (owned by media task) */
    uint32_t size;
    uint32_t pos;             /* input cursor */
    int16_t pending[MINIMP3_MAX_SAMPLES_PER_FRAME]; /* last decoded frame */
    uint32_t pending_frames;  /* frames stashed in pending[] */
    uint32_t pcm_pos;         /* next frame to output from pending[] */
    uint32_t channels;
} mp3_stream_t;

static mp3_stream_t s_pcm_stream;
static volatile int s_play_stop = 0;

static uint32_t mp3_stream_fill(void *user, int16_t *dst, uint32_t max_frames) {
    mp3_stream_t *st = (mp3_stream_t *)user;
    uint32_t out = 0;

    while (out < max_frames) {
        /* Drain the previously decoded frame first. */
        while (out < max_frames && st->pcm_pos < st->pending_frames) {
            uint32_t sp = st->pcm_pos * st->channels;
            int16_t l = st->pending[sp];
            int16_t r = (st->channels == 2) ? st->pending[sp + 1] : l;
            dst[out * 2 + 0] = l;
            dst[out * 2 + 1] = r;
            out++;
            st->pcm_pos++;
        }
        if (out >= max_frames)
            break;
        if (st->pos >= st->size)
            break;

        mp3dec_frame_info_t info;
        int ns = mp3dec_decode_frame(&s_dec, st->data + st->pos,
                                     (int)(st->size - st->pos),
                                     st->pending, &info);
        uint32_t adv = info.frame_bytes > 0 ? (uint32_t)info.frame_bytes : 1;
        st->pos += adv;
        if (st->pos > st->size)
            st->pos = st->size;

        if (ns > 0 && info.channels > 0) {
            st->pending_frames = (uint32_t)ns;
            st->pcm_pos = 0;
        }
        /* ns <= 0: just skipped a bad region; loop drains/advances. */
    }
    return out;
}

static void set_status(const char *s) {
    memcpy(s_status, s, strlen(s) + 1);
}

/* basename of a path ("/music/x.mp3" -> "x.mp3") */
static void basename_of(const char *path, char *out, uint32_t cap) {
    const char *slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') slash = p;
    }
    const char *name = slash ? slash + 1 : path;
    uint32_t n = strlen(name);
    if (n >= cap) n = cap - 1;
    memcpy(out, name, n);
    out[n] = '\0';
}

/* ------------------------------------------------------------------ */
/* Decoder wrapper                                                     */
/* ------------------------------------------------------------------ */

int mp3_decode_pcm16(const uint8_t *mp3, uint32_t mp3_size,
                     int16_t **out_pcm, uint32_t *out_frames,
                     uint32_t *out_rate, uint32_t *out_channels) {
    if (!mp3 || mp3_size < 4 || !out_pcm || !out_frames || !out_rate ||
        !out_channels) {
        return -1;
    }

    uint32_t cap = mp3_size * MP3_EXPAND_MAX + MINIMP3_MAX_SAMPLES_PER_FRAME;
    int16_t *buf = (int16_t *)kmalloc(cap * sizeof(int16_t));
    if (!buf) {
        serial_print("mp3: out of memory for PCM buffer\n");
        return -2;
    }

    mp3dec_init(&s_dec);

    uint32_t written = 0;   /* total int16 samples written */
    uint32_t pos = 0;
    uint32_t rate = AUDIO_DEFAULT_RATE;
    uint32_t channels = 2;
    int decoded = 0;

    while (pos < mp3_size && written + MINIMP3_MAX_SAMPLES_PER_FRAME <= cap) {
        mp3dec_frame_info_t info;
        int ns = mp3dec_decode_frame(&s_dec, mp3 + pos, (int)(mp3_size - pos),
                                     buf + written, &info);
        if (ns > 0) {
            if (!decoded) {
                decoded = 1;
                rate = info.hz ? (uint32_t)info.hz : AUDIO_DEFAULT_RATE;
                channels = (uint32_t)info.channels;
            }
            written += (uint32_t)ns * (uint32_t)info.channels;
        }

        if (info.frame_bytes > 0) {
            pos += (uint32_t)info.frame_bytes;
        } else if (ns <= 0) {
            pos += 1;   /* skip a bad byte, avoid infinite loop */
        } else {
            break;
        }
    }

    if (written == 0) {
        serial_print("mp3: decoder produced no audio\n");
        kfree(buf);
        return -3;
    }

    *out_pcm = buf;
    *out_frames = written / channels;
    *out_rate = rate;
    *out_channels = channels;
    return (int)(*out_frames);
}

/* ------------------------------------------------------------------ */
/* Boot-time sync + song list                                          */
/* ------------------------------------------------------------------ */

void mp3_init(void) {
    s_song_count = 0;
    set_status("idle");

    if (!sdfs_is_mounted()) {
        serial_print("mp3: SDFS not mounted, skipping song sync\n");
        return;
    }

    /* Ensure the /music directory exists (sdfs_create_dir is a no-op if it
     * already exists from a previous boot). */
    sdfs_create_dir("/music");

    serial_print("mp3: syncing music from initrd...\n");
    for (int i = 0; i < 1024; i++) {
        const char *raw = initrd_list_files(i);
        if (!raw) break;

        /* Strip leading "./" or "/" as used by tar. */
        const char *name = raw;
        while (name[0] == '.' && name[1] == '/') name += 2;
        if (name[0] == '/') name++;

        if (strncmp(name, "music/", 6) != 0) continue;
        uint32_t nlen = (uint32_t)strlen(name);
        if (nlen < 5 || strcmp(name + nlen - 4, ".mp3") != 0) continue;

        uint32_t fsize = 0;
        uint8_t *data = (uint8_t *)initrd_get_file(name, &fsize);
        if (!data || fsize == 0) continue;

        char path[MP3_MAX_PATH];
        path[0] = '/';
        memcpy(path + 1, name, nlen);
        path[nlen + 1] = '\0';

        if (sdfs_write_file(path, data, fsize) == fsize) {
            serial_print("mp3: synced ");
            serial_print(path);
            serial_print("\n");
            if (s_song_count < MP3_MAX_SONGS) {
                memcpy(s_songs[s_song_count], path, nlen + 2);
                basename_of(path, s_song_names[s_song_count],
                            sizeof(s_song_names[s_song_count]));
                s_song_count++;
            }
        }
    }
    serial_print("mp3: sync done, ");
    serial_print_hex((uint64_t)s_song_count);
    serial_print(" songs\n");
}

uint32_t mp3_song_count(void) {
    return s_song_count + pen_song_count();
}

static int is_pen_path(const char *p) {
    return p && p[0] == '/' && p[1] == 'p' && p[2] == 'e' && p[3] == 'n' &&
           p[4] == '/';
}

const char *mp3_song_path(uint32_t index) {
    if (index >= s_song_count) {
        const char *name = pen_song_name(index - s_song_count);
        uint32_t len;
        if (!name) return NULL;
        len = strlen(name);
        if (len + 5 >= MP3_MAX_PATH) len = MP3_MAX_PATH - 6;
        memcpy(s_pen_path, "/pen/", 5);
        memcpy(s_pen_path + 5, name, len);
        s_pen_path[5 + len] = '\0';
        return s_pen_path;
    }
    return s_songs[index];
}

const char *mp3_song_name(uint32_t index) {
    if (index >= s_song_count) {
        return pen_song_name(index - s_song_count);
    }
    return s_song_names[index];
}

const char *mp3_status(void) {
    return s_status;
}

const char *audio_song_name(void) {
    if (s_playing_name[0]) return s_playing_name;
    if (s_request_path[0]) return s_request_path;
    return "";
}

/* ------------------------------------------------------------------ */
/* Playback API                                                        */
/* ------------------------------------------------------------------ */

void audio_song_request(const char *path) {
    if (!path || !path[0]) return;
    if (!sdfs_is_mounted()) return;

    uint32_t slen = strlen(path);
    if (slen >= MP3_MAX_PATH) slen = MP3_MAX_PATH - 1;
    memcpy(s_request_path, path, slen);
    s_request_path[slen] = '\0';
    set_status("requested");
    s_play_request = 1;
}

void audio_song_stop(void) {
    s_play_stop = 1;   /* producer stops after the current chunk */
    audio_stop();      /* and the bus is dropped immediately */
    set_status("stopped");
    s_play_request = 0;
}

void media_task(void) {
    task_set_fpu(s_fpu_ctx);

    for (;;) {
        if (!s_play_request) {
            asm volatile("hlt");
            continue;
        }
        s_play_request = 0;

        char path[MP3_MAX_PATH];
        memcpy(path, s_request_path, MP3_MAX_PATH);
        basename_of(path, s_playing_name, sizeof(s_playing_name));
        set_status("decoding");

        uint32_t size = 0;
        uint8_t *data;
        if (is_pen_path(path)) {
            data = (uint8_t *)pen_read_song(path + 5, &size);
            serial_print("mp3: pen read ");
            serial_print(path);
            serial_print(" size=0x");
            serial_print_hex(size);
            serial_print("\n");
        } else {
            data = (uint8_t *)sdfs_read_file(path, &size);
        }
        if (!data || size == 0) {
            if (data) kfree(data);
            set_status("not found");
            continue;
        }

        /* Decode the first frame up-front so the stream starts with the
         * real rate/channels and almost no audible delay. */
        mp3_stream_t *st = &s_pcm_stream;
        memset(st, 0, sizeof(*st));
        st->data = data;
        st->size = size;

        mp3dec_init(&s_dec);
        mp3dec_frame_info_t info;
        int ns = mp3dec_decode_frame(&s_dec, data, (int)size, st->pending,
                                     &info);
        if (ns <= 0 || info.hz == 0 || info.channels == 0) {
            serial_print("mp3: decoder produced no audio\n");
            kfree(data);
            set_status("decode error");
            continue;
        }
        st->pending_frames = (uint32_t)ns;
        st->pcm_pos = 0;
        st->channels = (uint32_t)info.channels;
        uint32_t adv = info.frame_bytes > 0 ? (uint32_t)info.frame_bytes : 1;
        st->pos = adv > (uint32_t)size ? size : adv;

        serial_print("mp3: streaming ");
        serial_print_hex((uint64_t)info.hz);
        serial_print(" Hz ");
        serial_print_hex((uint64_t)info.channels);
        serial_print(" ch\n");

        set_status("playing");
        s_play_stop = 0;
        uint32_t hz = (uint32_t)info.hz;
        int rc = -1;
        /* If another playback (e.g. the boot chime) is still running, wait
         * for it to finish and retry, instead of dropping the request. */
        for (int try = 0; try < 200; try++) {
            rc = audio_play_stream(mp3_stream_fill, st, hz,
                                   audio_get_volume(), &s_play_stop);
            if (rc == 0 || s_play_stop)
                break;
            switch_task();
        }
        kfree(data);

        serial_print("mp3: playback finished\n");
        set_status("idle");
    }
}