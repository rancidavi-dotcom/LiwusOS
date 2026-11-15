#ifndef LIWUS_KERNEL_MEDIA_H
#define LIWUS_KERNEL_MEDIA_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
    LIWUS_MEDIA_UNKNOWN = 0,
    LIWUS_MEDIA_WAV,
    LIWUS_MEDIA_MP3,
    LIWUS_MEDIA_MP4
} liwus_media_kind_t;

typedef struct {
    liwus_media_kind_t kind;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
} liwus_media_info_t;

int liwus_media_probe(const uint8_t *data, size_t size, liwus_media_info_t *out);

#endif
