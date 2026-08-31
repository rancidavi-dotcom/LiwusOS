#include "audio.h"
#include "io.h"
#include "kheap.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "task.h"

/*
 * AC'97 (Intel 82801AA "ICH") driver for LiwusOS.
 *
 * The controller exposes two I/O windows:
 *   BAR0 -> NAMBAR  : native audio mixer (AC'97 codec registers).
 *   BAR1 -> NABMBAR : native audio bus master (DMA engine registers).
 *
 * The bus-master engine reads "buffer descriptors" from a 32-entry ring
 * pointed to by PO_BDBAR. Each descriptor references a physical buffer of
 * 16-bit stereo samples. We keep the whole ring full of valid chunks and
 * advance by re-arming with PO_LVI whenever the engine halts (SR_DCH).
 * Playback is driven by polling the status register, so no extra IRQ
 * routing is required. QEMU captures the audio to the host's chosen
 * audio backend (e.g. `-audiodev wav` or `-audiodev sdl`).
 */

/* ---------------------------------------------------------------- */
/* Bus-master register offsets (NABMBAR, I/O space)                 */
/* ---------------------------------------------------------------- */
#define AC97_PI_BDBAR 0x00
#define AC97_PI_CIV   0x04
#define AC97_PI_LVI   0x05
#define AC97_PI_SR    0x06
#define AC97_PI_CR    0x0B
#define AC97_PO_BDBAR 0x10
#define AC97_PO_CIV   0x14
#define AC97_PO_LVI   0x15
#define AC97_PO_SR    0x16
#define AC97_PO_PICB  0x18
#define AC97_PO_CR    0x1B

/* Mixer (codec) register numbers == NAM I/O byte offset.          */
#define AC97_REG_RESET           0x00
#define AC97_REG_MASTER_VOLUME   0x02
#define AC97_REG_PCM_OUT_VOLUME  0x18
#define AC97_REG_EXT_AUDIO_CTRL  0x2A
#define AC97_REG_FRONT_DAC_RATE  0x2C
#define AC97_REG_VENDOR_ID1      0x7C
#define AC97_REG_VENDOR_ID2      0x7E

#define AC97_EACS_VRA (1U << 0) /* Variable-rate audio (48k cap)   */

#define AC97_BD_IOC    (1U << 31) /* Interrupt-on-complete in BD  */
#define AC97_BD_BUP    (1U << 30) /* Buffer-underrun policy       */

#define AC97_PO_CR_RPBM 0x01 /* Run/pause bus master stream    */
#define AC97_PO_SR_DCH  0x01 /* DMA controller halted         */

#define AC97_RING_ENTRIES 32
#define AC97_CHUNK_FRAMES 1024 /* frames (stereo/16-bit) per chunk */
#define AC97_CHUNK_BYTES  (AC97_CHUNK_FRAMES * 4)

/* Hardware buffer descriptor (8 bytes, written by the driver).    */
typedef struct {
    uint32_t addr;    /* physical address of the PCM buffer       */
    uint32_t ctl_len; /* bits 31/30 = IOC/BUP; low 16 = #samples  */
} ac97_bd_t;

/* 512-entry sine table (2^9) so tone synthesis needs no FP math.  */
static const int16_t audio_sin_tab[512] = {
        0,   402,   804,  1206,  1608,  2009,  2410,  2811,  3212,  3612,  4011,  4410,
     4808,  5205,  5602,  5998,  6393,  6786,  7179,  7571,  7962,  8351,  8739,  9126,
     9512,  9896, 10278, 10659, 11039, 11417, 11793, 12167, 12539, 12910, 13279, 13645,
    14010, 14372, 14732, 15090, 15446, 15800, 16151, 16499, 16846, 17189, 17530, 17869,
    18204, 18537, 18868, 19195, 19519, 19841, 20159, 20475, 20787, 21096, 21403, 21705,
    22005, 22301, 22594, 22884, 23170, 23452, 23731, 24007, 24279, 24547, 24811, 25072,
    25329, 25582, 25832, 26077, 26319, 26556, 26790, 27019, 27245, 27466, 27683, 27896,
    28105, 28310, 28510, 28706, 28898, 29085, 29268, 29447, 29621, 29791, 29956, 30117,
    30273, 30424, 30571, 30714, 30852, 30985, 31113, 31237, 31356, 31470, 31580, 31685,
    31785, 31880, 31971, 32057, 32137, 32213, 32285, 32351, 32412, 32469, 32521, 32567,
    32609, 32646, 32678, 32705, 32728, 32745, 32757, 32765, 32767, 32765, 32757, 32745,
    32728, 32705, 32678, 32646, 32609, 32567, 32521, 32469, 32412, 32351, 32285, 32213,
    32137, 32057, 31971, 31880, 31785, 31685, 31580, 31470, 31356, 31237, 31113, 30985,
    30852, 30714, 30571, 30424, 30273, 30117, 29956, 29791, 29621, 29447, 29268, 29085,
    28898, 28706, 28510, 28310, 28105, 27896, 27683, 27466, 27245, 27019, 26790, 26556,
    26319, 26077, 25832, 25582, 25329, 25072, 24811, 24547, 24279, 24007, 23731, 23452,
    23170, 22884, 22594, 22301, 22005, 21705, 21403, 21096, 20787, 20475, 20159, 19841,
    19519, 19195, 18868, 18537, 18204, 17869, 17530, 17189, 16846, 16499, 16151, 15800,
    15446, 15090, 14732, 14372, 14010, 13645, 13279, 12910, 12539, 12167, 11793, 11417,
    11039, 10659, 10278,  9896,  9512,  9126,  8739,  8351,  7962,  7571,  7179,  6786,
     6393,  5998,  5602,  5205,  4808,  4410,  4011,  3612,  3212,  2811,  2410,  2009,
     1608,  1206,   804,   402,     0,  -402,  -804, -1206, -1608, -2009, -2410, -2811,
    -3212, -3612, -4011, -4410, -4808, -5205, -5602, -5998, -6393, -6786, -7179, -7571,
    -7962, -8351, -8739, -9126, -9512, -9896, -10278, -10659, -11039, -11417, -11793, -12167,
    -12539, -12910, -13279, -13645, -14010, -14372, -14732, -15090, -15446, -15800, -16151, -16499,
    -16846, -17189, -17530, -17869, -18204, -18537, -18868, -19195, -19519, -19841, -20159, -20475,
    -20787, -21096, -21403, -21705, -22005, -22301, -22594, -22884, -23170, -23452, -23731, -24007,
    -24279, -24547, -24811, -25072, -25329, -25582, -25832, -26077, -26319, -26556, -26790, -27019,
    -27245, -27466, -27683, -27896, -28105, -28310, -28510, -28706, -28898, -29085, -29268, -29447,
    -29621, -29791, -29956, -30117, -30273, -30424, -30571, -30714, -30852, -30985, -31113, -31237,
    -31356, -31470, -31580, -31685, -31785, -31880, -31971, -32057, -32137, -32213, -32285, -32351,
    -32412, -32469, -32521, -32567, -32609, -32646, -32678, -32705, -32728, -32745, -32757, -32765,
    -32767, -32765, -32757, -32745, -32728, -32705, -32678, -32646, -32609, -32567, -32521, -32469,
    -32412, -32351, -32285, -32213, -32137, -32057, -31971, -31880, -31785, -31685, -31580, -31470,
    -31356, -31237, -31113, -30985, -30852, -30714, -30571, -30424, -30273, -30117, -29956, -29791,
    -29621, -29447, -29268, -29085, -28898, -28706, -28510, -28310, -28105, -27896, -27683, -27466,
    -27245, -27019, -26790, -26556, -26319, -26077, -25832, -25582, -25329, -25072, -24811, -24547,
    -24279, -24007, -23731, -23452, -23170, -22884, -22594, -22301, -22005, -21705, -21403, -21096,
    -20787, -20475, -20159, -19841, -19519, -19195, -18868, -18537, -18204, -17869, -17530, -17189,
    -16846, -16499, -16151, -15800, -15446, -15090, -14732, -14372, -14010, -13645, -13279, -12910,
    -12539, -12167, -11793, -11417, -11039, -10659, -10278, -9896, -9512, -9126, -8739, -8351,
    -7962, -7571, -7179, -6786, -6393, -5998, -5602, -5205, -4808, -4410, -4011, -3612,
    -3212, -2811, -2410, -2009, -1608, -1206,  -804,  -402,
};

/* ---------------------------------------------------------------- */
/* Driver state                                                     */
/* ---------------------------------------------------------------- */
static int audio_present = 0;
static uint16_t audio_nam_base;
static uint16_t audio_nabm_base;
static uint32_t audio_rate = AUDIO_DEFAULT_RATE;
static int audio_volume = 100;
static int audio_muted = 0;
static volatile int audio_busy = 0;

static ac97_bd_t *audio_ring;
static int16_t *audio_chunks[AC97_RING_ENTRIES];
static uint32_t audio_fill_idx;

/* ---------------------------------------------------------------- */
/* Sample source                                                     */
/* ---------------------------------------------------------------- */

enum {
    AUDIO_SRC_PCM = 0,
    AUDIO_SRC_TONE,
    AUDIO_SRC_NOTES,
    AUDIO_SRC_STREAM
};

typedef struct {
    int type;
    uint32_t rate;
    uint32_t volume;      /* 0..100 */
    uint32_t total_frames;/* 0 = generated until it ends */
    uint32_t emitted;

    /* PCM / WAV */
    const int16_t *pcm;
    uint32_t pcm_frames;
    uint32_t pcm_pos;
    int pcm_mono;

    /* Tone */
    uint32_t tone_freq;
    uint32_t phase;

    /* Note sequences */
    const audio_note_t *notes;
    uint32_t note_count;
    uint32_t note_idx;
    uint32_t note_phase;
    uint32_t note_frames_left;
    uint32_t gap_frames_left;

    /* Stream source (filled chunk-at-a-time by an external producer) */
    audio_stream_fill_t stream_fill;
    void *stream_user;
    volatile int *stream_stop;
} audio_src_t;

static inline int32_t apply_volume(int32_t sample, uint32_t volume) {
    if (volume == 0)
        return 0;
    if (volume == 100)
        return sample;
    return (sample * (int32_t)volume) / 100;
}

/* Generates the next frame; returns 0 if the source is exhausted.   */
static int src_next_frame(audio_src_t *s, int16_t *out_l, int16_t *out_r) {
    int32_t sample = 0;

    if (s->type == AUDIO_SRC_TONE) {
        uint32_t idx = (s->phase >> 23) & 511;
        sample = audio_sin_tab[idx];
        s->phase += (uint32_t)(((uint64_t)s->tone_freq << 32) / s->rate);
    } else if (s->type == AUDIO_SRC_NOTES) {
        if (s->gap_frames_left > 0) {
            s->gap_frames_left--;
            sample = 0;
        } else {
            if (s->note_frames_left == 0) {
                /* Advance to the next note. */
                if (s->note_idx >= s->note_count)
                    return 0;
                const audio_note_t *n = &s->notes[s->note_idx];
                s->note_idx++;
                s->note_phase = 0;
                if (n->frequency == 0) {
                    s->gap_frames_left = (uint32_t)((uint64_t)n->duration_ms * s->rate / 1000);
                    return src_next_frame(s, out_l, out_r);
                }
                s->note_frames_left =
                    (uint32_t)((uint64_t)n->duration_ms * s->rate / 1000);
            }
            s->note_frames_left--;
            uint32_t idx = (s->note_phase >> 23) & 511;
            sample = audio_sin_tab[idx];
            s->note_phase +=
                (uint32_t)(((uint64_t)s->notes[s->note_idx - 1].frequency << 32) /
                           s->rate);
        }
    } else { /* PCM */
        if (s->pcm_pos >= s->pcm_frames)
            return 0;
        const int16_t *frame = &s->pcm[s->pcm_pos * (s->pcm_mono ? 1 : 2)];
        s->pcm_pos++;
        int16_t lv = frame[0];
        int16_t rv = s->pcm_mono ? lv : frame[1];
        *out_l = (int16_t)apply_volume(lv, s->volume);
        *out_r = (int16_t)apply_volume(rv, s->volume);
        return 1;
    }

    *out_l = (int16_t)apply_volume(sample, s->volume);
    *out_r = *out_l;
    return 1;
}

/* Fills ring chunk `idx` (interleaved stereo/16-bit); pads silence. */
/* Returns the number of real frames written (0 == chunk is empty). */
static uint32_t fill_chunk(audio_src_t *s, uint32_t idx) {
    int16_t *dst = audio_chunks[idx];
    uint32_t got = 0;

    if (s->type == AUDIO_SRC_STREAM) {
        if (s->stream_stop && *s->stream_stop)
            return 0;
        got = s->stream_fill(s->stream_user, dst, AC97_CHUNK_FRAMES);
        if (s->stream_stop && *s->stream_stop)
            got = 0;
        if (got > AC97_CHUNK_FRAMES)
            got = AC97_CHUNK_FRAMES;
        for (uint32_t f = got; f < AC97_CHUNK_FRAMES; f++) {
            dst[f * 2 + 0] = 0;
            dst[f * 2 + 1] = 0;
        }
        if (s->volume != 100) {
            for (uint32_t f = 0; f < got; f++) {
                dst[f * 2 + 0] = (int16_t)apply_volume(dst[f * 2 + 0], s->volume);
                dst[f * 2 + 1] = (int16_t)apply_volume(dst[f * 2 + 1], s->volume);
            }
        }
        if (s->emitted < 0xFFFFFFFF)
            s->emitted += got;
        return got;
    }

    for (uint32_t f = 0; f < AC97_CHUNK_FRAMES; f++) {
        int16_t lv = 0, rv = 0;
        if (!(s->total_frames && s->emitted >= s->total_frames)) {
            if (src_next_frame(s, &lv, &rv))
                got++;
        }
        if (s->emitted < 0xFFFFFFFF)
            s->emitted++;
        dst[f * 2 + 0] = lv;
        dst[f * 2 + 1] = rv;
    }
    return got;
}

/* ---------------------------------------------------------------- */
/* Ring / bus-master engine                                          */
/* ---------------------------------------------------------------- */

static void ring_arm_descriptor(uint32_t idx) {
    audio_ring[idx].addr = (uint32_t)(uintptr_t)audio_chunks[idx];
    /* QEMU treats ctl_len&0xffff as *frames* (16-bit) -> each chunk (which
     * holds AC97_CHUNK_FRAMES *frames* of 2x16-bit samples) is 2x that many
     * bytes, so the count must be the number of *samples* per chunk. */
    audio_ring[idx].ctl_len = AC97_BD_IOC | (AC97_CHUNK_FRAMES * 2);
}

static int playback_run(audio_src_t *s) {
    uint32_t eof_chunk = 0;
    int eof = 0;
    int free_slots = 0;
    uint32_t last_civ = 0;

    /* Every descriptor advertised through LVI must be valid.  The old code
     * prepared only four but set LVI to 31, so a long stream reached empty
     * descriptors and the AC'97 engine halted.  Fill the complete ring
     * before starting DMA; subsequent iterations recycle completed slots. */
    audio_fill_idx = 0;
    for (uint32_t i = 0; i < AC97_RING_ENTRIES; i++) {
        if (!eof) {
            uint32_t got = fill_chunk(s, i);
            if (got < AC97_CHUNK_FRAMES) {
                eof = 1;
                eof_chunk = i;
            }
        }
        ring_arm_descriptor(i);
        audio_fill_idx = (i + 1) & (AC97_RING_ENTRIES - 1);
        if (eof)
            break;
    }

    if (eof && eof_chunk == 0 && s->total_frames == 0) {
        /* Nothing to play (empty source). */
        return 0;
    }

    /* Start the PCM-out stream. */
    outl(audio_nabm_base + AC97_PO_BDBAR, (uint32_t)(uintptr_t)audio_ring);
    outb(audio_nabm_base + AC97_PO_LVI, eof ? (uint8_t)eof_chunk : (uint8_t)31);
    serial_print("AC97PO: start bdbar=");
    serial_print_hex((uint64_t)(uintptr_t)audio_ring);
    serial_print(" lvi=");
    serial_print_hex(eof ? eof_chunk : 31);
    serial_print(" ring0.a=");
    serial_print_hex(audio_ring[0].addr);
    serial_print(" ring0.c=");
    serial_print_hex(audio_ring[0].ctl_len);
    serial_print(" chunk0=");
    serial_print_hex((uint64_t)(uintptr_t)audio_chunks[0]);
    serial_print("\n");
    outb(audio_nabm_base + AC97_PO_CR, AC97_PO_CR_RPBM);

    while (1) {
        uint8_t sr = inb(audio_nabm_base + AC97_PO_SR);
        int halted = sr & AC97_PO_SR_DCH;

        /* External stop requested via audio_stop() (which already dropped
         * the bus). Leave the loop immediately. */
        if (!audio_busy)
            break;

        uint32_t civ = (uint32_t)(inl(audio_nabm_base + AC97_PO_CIV) & 0xFF);
        free_slots += (int)((civ + AC97_RING_ENTRIES - last_civ) &
                            (AC97_RING_ENTRIES - 1));
        if (free_slots > AC97_RING_ENTRIES)
            free_slots = AC97_RING_ENTRIES;
        last_civ = civ;

        /* Refill every slot the DMA has consumed so the ring stays deep.
         * `free_slots` counts empty slots exactly (avoids the ring-full vs
         * ring-empty mod-32 ambiguity): 0 == fully armed. */
        if (!eof) {
            uint32_t guard = 0;
            while (free_slots > 0 && guard++ < AC97_RING_ENTRIES * 4) {
                uint32_t got = fill_chunk(s, audio_fill_idx);
                ring_arm_descriptor(audio_fill_idx);
                audio_fill_idx = (audio_fill_idx + 1) &
                                 (AC97_RING_ENTRIES - 1);
                free_slots--;
                if (got < AC97_CHUNK_FRAMES) {
                    eof = 1;
                    eof_chunk = (audio_fill_idx + AC97_RING_ENTRIES - 1) &
                                (AC97_RING_ENTRIES - 1);
                    /* Trim the tail so the engine halts at the last chunk. */
                    outb(audio_nabm_base + AC97_PO_LVI, (uint8_t)eof_chunk);
                    break;
                }
            }
        }

        if (halted) {
            serial_print("AC97PO: halted (eof=");
            serial_print_hex(eof);
            serial_print(")\n");
            if (eof)
                break;
            if (!audio_busy)
                break;   /* external stop requested via audio_stop() */
            /* Natural end of the 32-chunk ring: re-arm and keep going. */
            outb(audio_nabm_base + AC97_PO_LVI, 31);
        } else if (!eof) {
            /* QEMU halts the engine whenever a BD completes with
             * civ == lvi, so keep LVI sliding ahead of CIV. The engine then
             * never matches and DMA flows continuously (no per-cycle
             * stop/start gaps). */
            outb(audio_nabm_base + AC97_PO_LVI,
                 (uint8_t)((civ + 12) & 0x1F));
        }

        if (eof) {
            /* Last chunk(s) still queued: just wait for the halt. */
            switch_task();
            continue;
        }

        if (free_slots == 0) {
            /* Ring fully armed: let other tasks (GUI) run a tick. */
            switch_task();
        } else {
            /* Catching up: busy-spin so the DMA never goes dry. */
            for (volatile int b = 0; b < 8000; b++)
                asm volatile("pause");
        }
    }

    serial_print("AC97PO: done\n");
    outb(audio_nabm_base + AC97_PO_CR, 0);
    return 0;
}

/* ---------------------------------------------------------------- */
/* Mixer / codec access                                              */
/* ---------------------------------------------------------------- */

static inline void mixer_write(uint16_t reg, uint16_t value) {
    outw(audio_nam_base + reg, value);
    io_wait();
}

static inline uint16_t mixer_read(uint16_t reg) {
    return inw(audio_nam_base + reg);
}

void audio_set_volume(int percent) {
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    audio_volume = percent;
    if (!audio_present)
        return;

    /* Master volume is a 6-bit/ -1.5 dB inverse scale per channel. */
    int v = 0x3F - (percent * 0x3F) / 100;
    if (v < 0)
        v = 0;
    /* PCM-out volume is a 5-bit inverse scale. */
    int p = 0x1F - (percent * 0x1F) / 100;
    if (p < 0)
        p = 0;

    /* Bit 15 of each (mute for both channels). */
    uint16_t mute_bit = audio_muted ? 0x8000 : 0;
    mixer_write(AC97_REG_MASTER_VOLUME, (uint16_t)((v | (v << 8)) | mute_bit));
    mixer_write(AC97_REG_PCM_OUT_VOLUME, (uint16_t)((p | (p << 8)) | mute_bit));
}

int audio_get_volume(void) {
    return audio_volume;
}

void audio_set_muted(int muted) {
    audio_muted = muted ? 1 : 0;
    if (audio_present)
        audio_set_volume(audio_volume);   /* re-write mixer with mute bit */
}

int audio_get_muted(void) {
    return audio_muted;
}

int audio_set_rate(uint32_t hz) {
    if (!audio_present || hz == 0)
        return -1;
    if (hz < 8000 || hz > 96000)
        hz = AUDIO_DEFAULT_RATE;
    /* Variable-rate audio + new front DAC rate. */
    mixer_write(AC97_REG_EXT_AUDIO_CTRL, AC97_EACS_VRA);
    mixer_write(AC97_REG_FRONT_DAC_RATE, (uint16_t)hz);
    audio_rate = hz;
    return 0;
}

uint32_t audio_get_rate(void) {
    return audio_rate;
}

/* ---------------------------------------------------------------- */
/* Public playback API                                               */
/* ---------------------------------------------------------------- */

int audio_play_pcm16(const int16_t *samples, uint32_t count, uint32_t hz,
                     int mono, int volume_percent) {
    if (!audio_present || audio_busy || !samples || count == 0)
        return -1;
    audio_src_t s;
    memset(&s, 0, sizeof(s));
    s.type = AUDIO_SRC_PCM;
    s.rate = hz ? hz : AUDIO_DEFAULT_RATE;
    s.volume = (uint32_t)volume_percent;
    s.pcm = samples;
    s.pcm_frames = mono ? count : count; /* count is already in frames */
    s.pcm_mono = mono;
    s.total_frames = count;
    audio_set_rate(s.rate);
    audio_busy = 1;
    int rc = playback_run(&s);
    audio_busy = 0;
    return rc;
}

int audio_play_tone(uint32_t freq_hz, uint32_t duration_ms, int volume_percent) {
    if (!audio_present || audio_busy || freq_hz == 0 || duration_ms == 0)
        return -1;
    audio_src_t s;
    memset(&s, 0, sizeof(s));
    s.type = AUDIO_SRC_TONE;
    s.rate = audio_rate;
    s.volume = (uint32_t)volume_percent;
    s.tone_freq = freq_hz;
    s.total_frames = (uint32_t)((uint64_t)duration_ms * audio_rate / 1000);
    audio_busy = 1;
    int rc = playback_run(&s);
    audio_busy = 0;
    return rc;
}

int audio_play_notes(const audio_note_t *notes, uint32_t count, uint32_t hz,
                     int volume_percent) {
    if (!audio_present || audio_busy || !notes || count == 0)
        return -1;
    audio_src_t s;
    memset(&s, 0, sizeof(s));
    s.type = AUDIO_SRC_NOTES;
    s.rate = hz ? hz : AUDIO_DEFAULT_RATE;
    s.volume = (uint32_t)volume_percent;
    s.notes = notes;
    s.note_count = count;
    s.total_frames = 0;
    audio_set_rate(s.rate);
    audio_busy = 1;
    int rc = playback_run(&s);
    audio_busy = 0;
    return rc;
}

int audio_play_stream(audio_stream_fill_t fill, void *user, uint32_t hz,
                      int volume_percent, volatile int *stop) {
    if (!audio_present || audio_busy || !fill || hz == 0)
        return -1;
    if (hz < 8000 || hz > 96000)
        hz = AUDIO_DEFAULT_RATE;
    audio_src_t s;
    memset(&s, 0, sizeof(s));
    s.type = AUDIO_SRC_STREAM;
    s.rate = hz;
    s.volume = (uint32_t)volume_percent;
    s.stream_fill = fill;
    s.stream_user = user;
    s.stream_stop = stop;
    s.total_frames = 0;
    audio_set_rate(s.rate);
    audio_busy = 1;
    int rc = playback_run(&s);
    audio_busy = 0;
    return rc;
}

int audio_parse_wav(const uint8_t *data, uint32_t size, wav_info_t *out) {
    if (!data || !out || size < 44)
        return -1;
    if (memcmp(data, "RIFF", 4) || memcmp(data + 8, "WAVE", 4))
        return -1;

    memset(out, 0, sizeof(*out));
    out->bits_per_sample = 16;

    uint32_t pos = 12;
    while (pos + 8 <= size) {
        uint32_t id = *(const uint32_t *)(data + pos);
        uint32_t sz = *(const uint32_t *)(data + pos + 4);
        pos += 8;
        if (pos + sz > size)
            break;
        if (id == 0x20746D66) { /* 'fmt ' */
            uint16_t atag = *(const uint16_t *)(data + pos);
            uint16_t ch   = *(const uint16_t *)(data + pos + 2);
            uint32_t hz   = *(const uint32_t *)(data + pos + 4);
            uint16_t bits = *(const uint16_t *)(data + pos + 14);
            if (atag != 1 || bits != 16) /* PCM only, 16-bit only */
                return -1;
            out->sample_rate = hz;
            out->channels = ch;
            out->bits_per_sample = bits;
        } else if (id == 0x61746164) { /* 'data' */
            out->data_start = pos;
            out->data_length = sz > size - pos ? size - pos : sz;
        }
        pos += sz + (sz & 1);
    }
    return (out->data_length > 0) ? 0 : -1;
}

int audio_play_wav(const uint8_t *data, uint32_t size, int volume_percent) {
    if (!audio_present || audio_busy || !data || size < 44)
        return -1;
    wav_info_t info;
    if (audio_parse_wav(data, size, &info) != 0)
        return -1;

    int mono = (info.channels == 1);
    uint32_t frames = info.data_length / (mono ? 2 : 4);
    if (frames == 0)
        return -1;
    uint32_t hz = info.sample_rate;
    if (hz < 8000 || hz > 96000)
        hz = AUDIO_DEFAULT_RATE;

    audio_src_t s;
    memset(&s, 0, sizeof(s));
    s.type = AUDIO_SRC_PCM;
    s.rate = hz;
    s.volume = (uint32_t)volume_percent;
    s.pcm = (const int16_t *)(data + info.data_start);
    s.pcm_frames = frames;
    s.pcm_mono = mono;
    s.total_frames = frames;
    audio_set_rate(hz);
    audio_busy = 1;
    int rc = playback_run(&s);
    audio_busy = 0;
    return rc;
}

void audio_stop(void) {
    if (!audio_present)
        return;
    outb(audio_nabm_base + AC97_PO_CR, 0);
    audio_busy = 0;
}

int audio_is_busy(void) {
    return audio_busy;
}

/* ---------------------------------------------------------------- */
/* Setup                                                             */
/* ---------------------------------------------------------------- */

int audio_init(void) {
    if (audio_present)
        return 0;

    pci_device_t *dev = pci_get_audio();
    if (!dev) {
        serial_print("AC97: no audio controller found (using PC speaker)\n");
        return -1;
    }

    uint32_t bar0 =
        pci_read_config(dev->bus, dev->device, dev->function, 0x10);
    uint32_t bar1 =
        pci_read_config(dev->bus, dev->device, dev->function, 0x14);
    if (!(bar0 & 1) || !(bar1 & 1)) { /* both windows must be I/O space */
        serial_print("AC97: unexpected BAR type\n");
        return -1;
    }

    uint32_t cmd =
        pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    cmd |= 0x01; /* I/O space enable */
    cmd |= 0x04; /* bus mastering enable (required for DMA) */
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, cmd);

    audio_nam_base = (uint16_t)(bar0 & 0xFFFC);
    audio_nabm_base = (uint16_t)(bar1 & 0xFFFC);

    audio_ring = (ac97_bd_t *)kmalloc(sizeof(ac97_bd_t) * AC97_RING_ENTRIES);
    memset((void *)audio_ring, 0, sizeof(ac97_bd_t) * AC97_RING_ENTRIES);
    for (int i = 0; i < AC97_RING_ENTRIES; i++) {
        audio_chunks[i] = (int16_t *)kmalloc(AC97_CHUNK_BYTES);
        memset(audio_chunks[i], 0, AC97_CHUNK_BYTES);
    }

    /* Cold reset the codec, then enable variable rate at 48 kHz. */
    mixer_write(AC97_REG_RESET, 0x0000);
    mixer_write(AC97_REG_EXT_AUDIO_CTRL, AC97_EACS_VRA);
    mixer_write(AC97_REG_FRONT_DAC_RATE, AUDIO_DEFAULT_RATE);

    uint16_t vid1 = mixer_read(AC97_REG_VENDOR_ID1);
    uint16_t vid2 = mixer_read(AC97_REG_VENDOR_ID2);

    serial_print("AC97: Intel 82801AA controller found (NAM=0x");
    serial_print_hex(audio_nam_base);
    serial_print(" NABM=0x");
    serial_print_hex(audio_nabm_base);
    serial_print(")\nAC97: codec vendor-id=");
    serial_print_hex(vid1);
    serial_print_hex(vid2);
    serial_print("\n");

    audio_set_volume(100);
    audio_set_rate(AUDIO_DEFAULT_RATE);
    audio_present = 1;

    serial_print("AC97: audio driver ready (");
    serial_print_hex(AUDIO_DEFAULT_RATE);
    serial_print(" Hz, 16-bit stereo, hardware DMA)\n");
    return 0;
}

int audio_available(void) {
    return audio_present;
}
