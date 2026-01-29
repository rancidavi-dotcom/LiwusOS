#ifndef HDA_H
#define HDA_H

#include <stdint.h>
#include "audio.h"

/*
 * Intel High Definition Audio (HDA) driver.
 *
 * Targets the generic Intel HDA controller (PCI class 0x04, subclass 0x03)
 * with an output-only codec path.  Unlike the legacy AC'97 (ICH) driver this
 * uses a memory-mapped 16 KiB register window (BAR0), the codec command
 * interface (immediate commands, IC/IR) and the bus-master stream engine.
 *
 * The controller is reset, the first output-capable pin is discovered and
 * the DAC feeding it is located by walking the codec connection lists.  The
 * DAC is bound to one output stream descriptor which reads a 32-entry buffer
 * descriptor list filled on demand by a producer callback.
 *
 * The implementation is intentionally conservative: it programs a single
 * playback stream, 16-bit stereo, and polls the stream position instead of
 * relying on interrupts (same approach used by the AC'97 driver).
 */

/* Returns 0 on success, -1 when no HDA controller/codec is usable. */
int hda_init(void);
int hda_available(void);

/* Mixer / volume control (percent 0..100). */
void hda_set_volume(int percent);
int hda_get_volume(void);
void hda_set_muted(int muted);
int hda_get_muted(void);

/* Playback sample rate (8000..96000 Hz). */
int hda_set_rate(uint32_t hz);
uint32_t hda_get_rate(void);

/*
 * Playback (blocking). `fill` produces interleaved 16-bit stereo frames into
 * `dst`; returning fewer frames than requested signals end-of-stream. `stop`
 * is an optional flag the caller raises to abort playback.
 */
int hda_play(audio_stream_fill_t fill, void *user, uint32_t hz,
             int volume_percent, volatile int *stop);

void hda_stop(void);
int hda_is_busy(void);

#endif /* HDA_H */
