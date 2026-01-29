#ifndef MP3_H
#define MP3_H

#include <stdint.h>

#define MP3_MAX_PATH  160
#define MP3_MAX_SONGS 32

/* Decode an in-memory MP3 to raw 16-bit PCM.
 * Caller must kfree() the returned buffer. Returns number of PCM frames
 * (>0) on success, or a negative error code.
 *   *out_pcm       -> allocated int16 buffer (stereo interleaved or mono)
 *   *out_frames    -> number of frames (each frame has channels samples)
 *   *out_rate      -> sample rate in Hz
 *   *out_channels  -> 1 or 2
 */
int mp3_decode_pcm16(const uint8_t *mp3, uint32_t mp3_size,
                     int16_t **out_pcm, uint32_t *out_frames,
                     uint32_t *out_rate, uint32_t *out_channels);

/* Boot-time sync: ensures /music exists on SDFS and copies every music MP3
 * file from the initrd. Fills the in-memory song list. Call once after SDFS
 * mount. */
void mp3_init(void);

/* Song list (in-memory, from the initrd). */
uint32_t mp3_song_count(void);
const char *mp3_song_path(uint32_t index);
const char *mp3_song_name(uint32_t index);

/* Current media status text ("idle", "playing", "decoding", etc.). */
const char *mp3_status(void);

/* Request playback of an absolute SDFS path (e.g. "/music/song.mp3"). */
void audio_song_request(const char *path);

/* Name of the last requested song (basename), or "" if none. */
const char *audio_song_name(void);

/* Stop any playback in progress. */
void audio_song_stop(void);

/* Media playback task (create via create_task_named). */
void media_task(void);

#endif /* MP3_H */