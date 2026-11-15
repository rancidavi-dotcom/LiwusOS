#include "liwus/liwus.h"
#include "liwus/fcntl.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/stdio.h"
#include "kernel/media.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "../../third_party/minimp3/minimp3.h"

#define PLAY_MAX_FILE_BYTES (64u * 1024u * 1024u)
#define LIWUS_AUDIO_RATE 48000
#define AUDIO_BATCH_FRAMES 4096u

static int16_t g_batch_buf[AUDIO_BATCH_FRAMES * 2u];
static uint32_t g_batch_used = 0;

static int out_text(const char *text) {
    return liw_write_all(1, text, liw_strlen(text)) < 0 ? 1 : 0;
}

static int fail(const char *text) {
    (void)liw_write_all(2, text, liw_strlen(text));
    return 1;
}

static void u32_to_str(uint32_t val, char *buf) {
    char tmp[16];
    int n = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0 && n < 15) {
        tmp[n++] = (char)('0' + (val % 10u));
        val /= 10u;
    }
    for (int i = 0; i < n; i++) buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
}

static int batch_flush(int sound_fd) {
    if (g_batch_used == 0) return 0;
    size_t bytes = (size_t)g_batch_used * 2u * sizeof(int16_t);
    long written = liw_write(sound_fd, g_batch_buf, bytes);
    g_batch_used = 0;
    return (written == (long)bytes) ? 0 : -1;
}

static int batch_push(int sound_fd, const int16_t *stereo_samples, uint32_t frames) {
    uint32_t pos = 0;
    while (pos < frames) {
        uint32_t space = AUDIO_BATCH_FRAMES - g_batch_used;
        uint32_t chunk = (frames - pos < space) ? (frames - pos) : space;
        for (uint32_t i = 0; i < chunk * 2u; i++) {
            g_batch_buf[g_batch_used * 2u + i] = stereo_samples[pos * 2u + i];
        }
        g_batch_used += chunk;
        pos += chunk;
        if (g_batch_used >= AUDIO_BATCH_FRAMES) {
            if (batch_flush(sound_fd) < 0) return -1;
        }
    }
    return 0;
}

static int read_all(int fd, unsigned char *buffer, size_t size) {
    size_t used = 0;
    while (used < size) {
        long got = liw_read(fd, buffer + used, size - used);
        if (got <= 0) return -1;
        used += (size_t)got;
    }
    return 0;
}

static int play_mp3_stream(const unsigned char *data, size_t size, int sound_fd) {
    mp3dec_t decoder;
    mp3dec_frame_info_t info;
    mp3dec_init(&decoder);

    static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    static int16_t output[(MINIMP3_MAX_SAMPLES_PER_FRAME * 4) + 64];
    size_t offset = 0;
    unsigned frames = 0;
    g_batch_used = 0;

    while (offset < size) {
        int decoded = mp3dec_decode_frame(&decoder, data + offset,
                                          (int)(size - offset), pcm, &info);
        if (info.frame_bytes <= 0) {
            offset++;
            continue;
        }
        offset += (size_t)info.frame_bytes;
        if (decoded <= 0) continue;
        if (info.channels < 1 || info.channels > 2 || info.hz <= 0) {
            return -1;
        }

        uint32_t out_count = liwus_pcm_resample_to_48k_stereo(
            pcm, (uint32_t)decoded, (uint16_t)info.channels, (uint32_t)info.hz,
            output, (MINIMP3_MAX_SAMPLES_PER_FRAME * 2u) + 32u);
        if (out_count == 0) continue;

        if (batch_push(sound_fd, output, out_count) < 0) return -1;
        frames++;
    }

    if (batch_flush(sound_fd) < 0) return -1;
    return (frames > 0) ? 0 : -1;
}

static int play_wav_stream(const unsigned char *data, size_t size,
                           const liwus_media_info_t *info, int sound_fd) {
    if (size <= 44 || info->channels < 1 || info->channels > 2 ||
        info->sample_rate == 0 || info->bits_per_sample != 16) {
        return -1;
    }
    const int16_t *pcm = (const int16_t *)(data + 44);
    uint32_t total_frames = (uint32_t)((size - 44u) / (info->channels * 2u));
    static int16_t resampled[4096 * 2];
    uint32_t pos = 0;
    g_batch_used = 0;

    while (pos < total_frames) {
        uint32_t chunk = total_frames - pos;
        if (chunk > 1024u) chunk = 1024u;
        uint32_t out_n = liwus_pcm_resample_to_48k_stereo(
            pcm + pos * info->channels, chunk, info->channels,
            info->sample_rate, resampled, 4096u);
        if (out_n > 0 && batch_push(sound_fd, resampled, out_n) < 0) return -1;
        pos += chunk;
    }
    return batch_flush(sound_fd);
}

static int play_mp4_container(const unsigned char *data, size_t size, int sound_fd) {
    liwus_mp4_index_t idx;
    if (liwus_mp4_parse(data, size, &idx) < 0) {
        return fail("play: container MP4 invalido ou sem moov/mdat\n");
    }

    char num[16];
    (void)out_text("play: container ISO-BMFF MP4 analisado:\n");
    if (idx.has_video) {
        (void)out_text("  Video: ");
        u32_to_str(idx.width, num); (void)out_text(num);
        (void)out_text("x");
        u32_to_str(idx.height, num); (void)out_text(num);
        (void)out_text(" | Codec: ");
        (void)out_text(idx.video_fourcc);
        (void)out_text(" | Quadros: ");
        u32_to_str(idx.video_sample_count, num); (void)out_text(num);
        (void)out_text("\n");
    }
    if (idx.has_audio) {
        (void)out_text("  Audio: ");
        u32_to_str(idx.audio_sample_rate, num); (void)out_text(num);
        (void)out_text(" Hz | Codec: ");
        (void)out_text(idx.audio_fourcc);
        (void)out_text(" | Pacotes: ");
        u32_to_str(idx.audio_sample_count, num); (void)out_text(num);
        (void)out_text("\n");
    }

    if (idx.has_video && idx.video_codec != LIWUS_VCODEC_UNSUPPORTED) {
        const uint8_t *v_ptr = NULL;
        uint32_t v_sz = 0, v_pts = 0;
        static uint32_t frame_probe[64 * 36];
        if (liwus_mp4_get_video_sample(&idx, data, size, 0, &v_ptr, &v_sz, &v_pts) == 0) {
            if (liwus_mp4_decode_video_frame(idx.video_codec, v_ptr, v_sz,
                                             idx.width, idx.height, frame_probe, 64, 36) == 0) {
                (void)out_text("  Quadros de video MJPEG/RGB decodificados com sucesso!\n");
            }
        }
    }

    if (!idx.has_audio) {
        return 0;
    }

    if (idx.audio_codec == LIWUS_ACODEC_MP3) {
        mp3dec_t decoder;
        mp3dec_frame_info_t info;
        mp3dec_init(&decoder);
        static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
        static int16_t output[(MINIMP3_MAX_SAMPLES_PER_FRAME * 4) + 64];
        g_batch_used = 0;

        for (uint32_t s = 0; s < idx.audio_sample_count; s++) {
            const uint8_t *a_ptr = NULL;
            uint32_t a_sz = 0, a_pts = 0;
            if (liwus_mp4_get_audio_sample(&idx, data, size, s, &a_ptr, &a_sz, &a_pts) < 0) {
                continue;
            }
            int decoded = mp3dec_decode_frame(&decoder, a_ptr, (int)a_sz, pcm, &info);
            if (decoded <= 0 || info.channels < 1 || info.channels > 2 || info.hz <= 0) {
                continue;
            }
            uint32_t out_n = liwus_pcm_resample_to_48k_stereo(
                pcm, (uint32_t)decoded, (uint16_t)info.channels, (uint32_t)info.hz,
                output, (MINIMP3_MAX_SAMPLES_PER_FRAME * 2u) + 32u);
            if (out_n > 0 && batch_push(sound_fd, output, out_n) < 0) return -1;
        }
        return batch_flush(sound_fd);
    }

    return fail("play: codec de audio do MP4 requer MP3 ou PCM.\n");
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) return fail("play: uso: play <arquivo.mp3 | arquivo.mp4 | arquivo.wav>\n");
    const char *path = argv[1];

    int input_fd = liw_open(path, LIW_O_RDONLY);
    if (input_fd < 0) return fail("play: nao foi possivel abrir o arquivo\n");
    struct liw_stat stat;
    if (liw_fstat(input_fd, &stat) < 0 || stat.st_size <= 0 ||
        (uint64_t)stat.st_size > PLAY_MAX_FILE_BYTES) {
        liw_close(input_fd);
        return fail("play: arquivo invalido ou maior que 64 MiB\n");
    }

    size_t size = (size_t)stat.st_size;
    unsigned char *data = (unsigned char *)malloc(size);
    if (!data) {
        liw_close(input_fd);
        return fail("play: memoria insuficiente\n");
    }
    int result = read_all(input_fd, data, size);
    liw_close(input_fd);
    if (result < 0) {
        free(data);
        return fail("play: falha ao ler o arquivo\n");
    }

    liwus_media_info_t minfo;
    if (liwus_media_probe(data, size, &minfo) < 0) {
        free(data);
        return fail("play: formato de midia desconhecido (suportados: MP3, MP4, WAV)\n");
    }

    int sound_fd = liw_open("/dev/sound", LIW_O_WRONLY);
    if (sound_fd < 0) {
        free(data);
        return fail("play: saida HDA indisponivel\n");
    }

    if (minfo.kind == LIWUS_MEDIA_MP3) {
        (void)out_text("play: reproduzindo MP3 em 48 kHz estereo via Intel HDA...\n");
        result = play_mp3_stream(data, size, sound_fd);
    } else if (minfo.kind == LIWUS_MEDIA_WAV) {
        (void)out_text("play: reproduzindo WAV PCM em 48 kHz estereo via Intel HDA...\n");
        result = play_wav_stream(data, size, &minfo, sound_fd);
    } else if (minfo.kind == LIWUS_MEDIA_MP4) {
        result = play_mp4_container(data, size, sound_fd);
    } else {
        result = -1;
    }

    liw_close(sound_fd);
    free(data);
    if (result < 0) return fail("play: falha na decodificacao ou na saida de audio\n");
    return 0;
}
