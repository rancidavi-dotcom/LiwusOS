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

typedef enum {
    LIWUS_VCODEC_NONE = 0,
    LIWUS_VCODEC_MJPEG,
    LIWUS_VCODEC_RAW_RGB24,
    LIWUS_VCODEC_UNSUPPORTED
} liwus_vcodec_t;

typedef enum {
    LIWUS_ACODEC_NONE = 0,
    LIWUS_ACODEC_MP3,
    LIWUS_ACODEC_PCM_S16LE,
    LIWUS_ACODEC_PCM_S16BE,
    LIWUS_ACODEC_PCM_U8,
    LIWUS_ACODEC_UNSUPPORTED
} liwus_acodec_t;

typedef struct {
    uint32_t stts_offset;
    uint32_t stts_count;
    uint32_t stsc_offset;
    uint32_t stsc_count;
    uint32_t stsz_offset;
    uint32_t stsz_fixed_size;
    uint32_t stsz_count;
    uint32_t stco_offset;
    uint32_t stco_count;
    uint8_t  stco_is_64;
    uint32_t timescale;
    uint64_t duration_units;
} liwus_mp4_table_t;

typedef struct {
    uint8_t  has_video;
    uint8_t  has_audio;
    liwus_vcodec_t video_codec;
    liwus_acodec_t audio_codec;
    char     video_fourcc[5];
    char     audio_fourcc[5];
    uint16_t width;
    uint16_t height;
    uint32_t fps_x100;
    uint32_t duration_ms;
    uint32_t video_sample_count;
    uint32_t audio_sample_count;
    uint32_t audio_sample_rate;
    uint16_t audio_channels;
    uint16_t audio_bits;
    liwus_mp4_table_t video_tab;
    liwus_mp4_table_t audio_tab;
} liwus_mp4_index_t;

int liwus_media_probe(const uint8_t *data, size_t size, liwus_media_info_t *out);

int liwus_mp4_parse(const uint8_t *data, size_t size, liwus_mp4_index_t *out);

int liwus_mp4_get_video_sample(const liwus_mp4_index_t *mp4,
                               const uint8_t *data, size_t size,
                               uint32_t sample_index,
                               const uint8_t **sample_ptr,
                               uint32_t *sample_size,
                               uint32_t *pts_ms);

int liwus_mp4_get_audio_sample(const liwus_mp4_index_t *mp4,
                               const uint8_t *data, size_t size,
                               uint32_t sample_index,
                               const uint8_t **sample_ptr,
                               uint32_t *sample_size,
                               uint32_t *pts_ms);

int liwus_mp4_decode_video_frame(liwus_vcodec_t codec,
                                 const uint8_t *sample_data,
                                 uint32_t sample_size,
                                 uint16_t expected_w,
                                 uint16_t expected_h,
                                 uint32_t *argb_out,
                                 uint16_t out_w,
                                 uint16_t out_h);

uint32_t liwus_pcm_resample_to_48k_stereo(const int16_t *in_pcm,
                                          uint32_t in_frames,
                                          uint16_t in_channels,
                                          uint32_t in_hz,
                                          int16_t *out_stereo,
                                          uint32_t max_out_frames);

#endif
