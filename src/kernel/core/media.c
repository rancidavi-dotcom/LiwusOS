#include "kernel/media.h"

static uint32_t media_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int media_eq4(const uint8_t *p, char a, char b, char c, char d) {
    return p[0] == (uint8_t)a && p[1] == (uint8_t)b &&
           p[2] == (uint8_t)c && p[3] == (uint8_t)d;
}

int liwus_media_probe(const uint8_t *data, size_t size, liwus_media_info_t *out) {
    if (!data || !out) return -1;
    out->kind = LIWUS_MEDIA_UNKNOWN;
    out->sample_rate = 0;
    out->channels = 0;
    out->bits_per_sample = 0;

    if (size >= 12 && media_eq4(data, 'R', 'I', 'F', 'F') &&
        media_eq4(data + 8, 'W', 'A', 'V', 'E')) {
        out->kind = LIWUS_MEDIA_WAV;
        if (size >= 36 && media_eq4(data + 12, 'f', 'm', 't', ' ') &&
            media_le32(data + 16) >= 16) {
            out->channels = (uint16_t)data[22] | ((uint16_t)data[23] << 8);
            out->sample_rate = media_le32(data + 24);
            out->bits_per_sample = (uint16_t)data[34] |
                                   ((uint16_t)data[35] << 8);
        }
        return 0;
    }

    /* ID3v2 ou um cabecalho MPEG Audio valido. Nao tentamos adivinhar MP3 a
     * partir de extensao: o probe e usado por VFS e pelo player. */
    if ((size >= 3 && data[0] == 'I' && data[1] == 'D' && data[2] == '3') ||
        (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)) {
        out->kind = LIWUS_MEDIA_MP3;
        return 0;
    }

    /* ISO Base Media File Format: MP4/MOV sempre possui uma caixa ftyp nos
     * primeiros bytes. O codec interno ainda e uma etapa separada. */
    if (size >= 12 && media_eq4(data + 4, 'f', 't', 'y', 'p')) {
        out->kind = LIWUS_MEDIA_MP4;
        return 0;
    }
    return -1;
}
