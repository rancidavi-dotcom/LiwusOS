#ifndef PEN_H
#define PEN_H

#include <stdint.h>

#define PEN_MAX_SONGS 32
#define PEN_NAME_MAX 64

/* Startup: discovers and attaches the (possibly absent) virtual pendrive. */
void pen_init(void);

/* Thread: watches for hot-plug/removal and rescans the MP3 list. */
void pen_task(void);

/* Number of MP3 files currently seen on the pendrive. */
uint32_t pen_song_count(void);

/* Name of song `index` (0 .. pen_song_count()-1) or NULL. */
const char *pen_song_name(uint32_t index);

/* Reads a whole song file into a kmalloc'ed buffer (NUL terminated).
 * Returns NULL on failure. *size_out receives the byte count. */
void *pen_read_song(const char *name, uint32_t *size_out);

/* Counter bumped whenever the media list may have changed (plug/rescan). */
uint32_t pen_song_gen(void);

/* 1 if the pendrive is mounted right now. */
int pen_is_connected(void);

#endif /* PEN_H */