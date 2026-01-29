#ifndef AUDIO_H
#define AUDIO_H

#include <stdint.h>

/*
 * LiwusOS audio driver.
 *
 * Implements a real sound-card driver for the Intel 82801AA (ICH) AC'97
 * controller emulated by QEMU (`-device AC97` / `-soundhw ac97`). Audio is
 * delivered through the controller's PCI bus-mastering ring of DMA buffer
 * descriptors at up to 48 kHz / 16-bit / stereo. The PC-speaker driver
 * (pcspkr) remains available as a fallback for systems without a sound card.
 */

#define AUDIO_DEFAULT_RATE 48000

/* One musical note: frequency in Hz (0 = rest) + duration in ms. */
typedef struct {
    uint32_t frequency;
    uint32_t duration_ms;
} audio_note_t;

/* Parsed WAVE (RIFF/PCM) layout as returned by audio_parse_wav(). */
typedef struct {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_start;   /* byte offset of PC M data inside the buffer */
    uint32_t data_length;  /* length of the PCM data in bytes */
} wav_info_t;

/* Initialize the driver. Returns 0 on success, -1 if no AC'97 card. */
int audio_init(void);
int audio_available(void);

/* Mixer / volume control. Percent ranges 0..100. */
void audio_set_volume(int percent);
int audio_get_volume(void);
void audio_set_muted(int muted);
int audio_get_muted(void);

/* Sample-rate control (typically 8000..96000 Hz). */
int audio_set_rate(uint32_t hz);
uint32_t audio_get_rate(void);

/*
 * Playback API (blocking).
 *
 * audio_play_pcm16: raw 16-bit PCM. `count` is the number of frames; when
 *   `mono` is set each frame is a single sample (duplicated to both
 *   channels), otherwise samples are interleaved L/R.
 * audio_play_tone: synthesize a sine wave of the given frequency.
 * audio_play_notes: synthesize a note/melody sequence.
 * audio_play_wav: play an in-memory RIFF/WAV buffer (16-bit PCM, 1/2 ch).
 */
int audio_play_pcm16(const int16_t *samples, uint32_t count, uint32_t hz,
                     int mono, int volume_percent);
int audio_play_tone(uint32_t freq_hz, uint32_t duration_ms, int volume_percent);
int audio_play_notes(const audio_note_t *notes, uint32_t count, uint32_t hz,
                     int volume_percent);
int audio_play_wav(const uint8_t *data, uint32_t size, int volume_percent);

/* Parse (not play) a WAV buffer. Returns 0 on success. */
int audio_parse_wav(const uint8_t *data, uint32_t size, wav_info_t *out);

/* Non-zero while a playback is in progress (audio_busy). */
int audio_is_busy(void);

/* Stop any playback in progress. */
void audio_stop(void);

/*
 * Streaming playback (chunk-at-a-time, non-blocking producer).
 *
 * Lets a producer (e.g. the MP3 decoder) feed PCM ring buffers on demand
 * instead of decoding the whole track into RAM before playback starts. The
 * producer callback is invoked to fill each DMA chunk and may return fewer
 * frames than requested to signal end-of-stream. `stop` is an optional
 * pointer to a volatile flag the caller can raise to end playback after the
 * current chunk.
 */
typedef uint32_t (*audio_stream_fill_t)(void *user, int16_t *dst,
                                        uint32_t max_frames);
int audio_play_stream(audio_stream_fill_t fill, void *user, uint32_t hz,
                      int volume_percent, volatile int *stop);

#endif /* AUDIO_H */