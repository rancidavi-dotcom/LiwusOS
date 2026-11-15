#include "liwus/liwus.h"
#include "liwus/fcntl.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/stdio.h"

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "../../third_party/minimp3/minimp3.h"

#define PLAY_MAX_FILE_BYTES (64u * 1024u * 1024u)
#define LIWUS_AUDIO_RATE 48000

static int out_text(const char *text) {
    return liw_write_all(1, text, liw_strlen(text)) < 0 ? 1 : 0;
}

static int fail(const char *text) {
    (void)liw_write_all(2, text, liw_strlen(text));
    return 1;
}

static int is_suffix(const char *path, const char *suffix) {
    size_t path_len = liw_strlen(path);
    size_t suffix_len = liw_strlen(suffix);
    if (path_len < suffix_len) return 0;
    return liw_strcmp(path + path_len - suffix_len, suffix) == 0;
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

static int play_mp3(const unsigned char *data, size_t size, int sound_fd) {
    mp3dec_t decoder;
    mp3dec_frame_info_t info;
    mp3dec_init(&decoder);

    static int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];
    static int16_t output[(MINIMP3_MAX_SAMPLES_PER_FRAME * 2) + 64];
    size_t offset = 0;
    unsigned frames = 0;
    unsigned long long output_samples = 0;

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

        unsigned in_channels = (unsigned)info.channels;
        unsigned out_count = (unsigned)(((unsigned long long)decoded * LIWUS_AUDIO_RATE) /
                                        (unsigned)info.hz);
        if (out_count == 0) continue;
        if (out_count > (MINIMP3_MAX_SAMPLES_PER_FRAME + 32u)) return -1;

        for (unsigned i = 0; i < out_count; i++) {
            unsigned src = (unsigned)(((unsigned long long)i * (unsigned)info.hz) /
                                      LIWUS_AUDIO_RATE);
            if (src >= (unsigned)decoded) src = (unsigned)decoded - 1u;
            if (in_channels == 1u) {
                output[i * 2u] = pcm[src];
                output[i * 2u + 1u] = pcm[src];
            } else {
                output[i * 2u] = pcm[src * 2u];
                output[i * 2u + 1u] = pcm[src * 2u + 1u];
            }
        }

        long written = liw_write(sound_fd, output, out_count * 2u * sizeof(int16_t));
        if (written != (long)(out_count * 2u * sizeof(int16_t))) return -1;
        output_samples += out_count;
        frames++;
    }

    if (frames == 0) return -1;
    (void)output_samples;
    return 0;
}

int main(int argc, char **argv, char **envp) {
    (void)envp;
    if (argc != 2) return fail("play: uso: play arquivo.mp3\n");
    const char *path = argv[1];

    if (is_suffix(path, ".mp4") || is_suffix(path, ".MP4")) {
        return fail("play: MP4 reconhecido, mas video MP4 ainda requer demuxer e codec H.264/AAC.\n");
    }
    if (!is_suffix(path, ".mp3") && !is_suffix(path, ".MP3")) {
        return fail("play: nesta versao apenas MP3 e suportado.\n");
    }

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

    int sound_fd = liw_open("/dev/sound", LIW_O_WRONLY);
    if (sound_fd < 0) {
        free(data);
        return fail("play: saida HDA indisponivel\n");
    }
    (void)out_text("play: decodificando MP3 em 48 kHz estereo...\n");
    result = play_mp3(data, size, sound_fd);
    liw_close(sound_fd);
    free(data);
    if (result < 0) return fail("play: MP3 invalido ou falha na saida de audio\n");
    return 0;
}
