#include "kernel/media.h"

static uint16_t media_le16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t media_le32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint16_t media_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static uint32_t media_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t media_be64(const uint8_t *p) {
    return ((uint64_t)media_be32(p) << 32) | (uint64_t)media_be32(p + 4);
}

static int media_eq4(const uint8_t *p, char a, char b, char c, char d) {
    return p[0] == (uint8_t)a && p[1] == (uint8_t)b &&
           p[2] == (uint8_t)c && p[3] == (uint8_t)d;
}

static void media_zero(void *dst, size_t n) {
    uint8_t *p = (uint8_t *)dst;
    for (size_t i = 0; i < n; i++) p[i] = 0;
}

static void media_copy4_str(char out[5], const uint8_t *p) {
    for (int i = 0; i < 4; i++) {
        uint8_t c = p[i];
        out[i] = (c >= 32 && c <= 126) ? (char)c : '?';
    }
    out[4] = '\0';
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
            out->channels = media_le16(data + 22);
            out->sample_rate = media_le32(data + 24);
            out->bits_per_sample = media_le16(data + 34);
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
     * primeiros bytes. */
    if (size >= 12 && media_eq4(data + 4, 'f', 't', 'y', 'p')) {
        out->kind = LIWUS_MEDIA_MP4;
        return 0;
    }
    return -1;
}

/* =========================================================================
 * ISO-BMFF (MP4 / MOV) Demuxer com Limites Verificados
 * ========================================================================= */
typedef struct {
    uint8_t  handler_type; /* 1 = vide, 2 = soun */
    uint16_t width;
    uint16_t height;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    char     fourcc[5];
    uint8_t  esds_oti;
    liwus_mp4_table_t tab;
} mp4_trak_parse_t;

static int mp4_next_box(const uint8_t *data, size_t start, size_t end,
                        size_t *box_pos, size_t *payload_pos, size_t *box_end,
                        const uint8_t **type_ptr) {
    if (start + 8 > end) return 0;
    uint64_t sz = media_be32(data + start);
    size_t hdr = 8;
    if (sz == 1) {
        if (start + 16 > end) return 0;
        sz = media_be64(data + start + 8);
        hdr = 16;
    } else if (sz == 0) {
        sz = end - start;
    }
    if (sz < hdr || sz > (uint64_t)(end - start)) return 0;
    *box_pos = start;
    *payload_pos = start + hdr;
    *box_end = start + (size_t)sz;
    *type_ptr = data + start + 4;
    return 1;
}

static uint8_t mp4_parse_esds_oti(const uint8_t *data, size_t start, size_t end) {
    /* Procura o descritor DecoderConfigDescriptor (tag 0x04) dentro do box esds */
    for (size_t i = start; i + 6 < end; i++) {
        if (data[i] == 0x04) {
            size_t p = i + 1;
            uint32_t len = 0;
            for (int k = 0; k < 4 && p < end; k++) {
                uint8_t b = data[p++];
                len = (len << 7) | (b & 0x7FU);
                if (!(b & 0x80U)) break;
            }
            if (len >= 13 && p < end) {
                return data[p]; /* objectTypeIndication */
            }
        }
    }
    return 0;
}

static void mp4_parse_stsd(const uint8_t *data, size_t payload, size_t box_end,
                           mp4_trak_parse_t *tk) {
    if (payload + 8 > box_end) return;
    uint32_t entry_count = media_be32(data + payload + 4);
    if (entry_count == 0) return;
    size_t entry = payload + 8;
    if (entry + 16 > box_end) return;
    uint32_t esz = media_be32(data + entry);
    if (esz < 16 || entry + esz > box_end) return;
    size_t entry_end = entry + esz;

    media_copy4_str(tk->fourcc, data + entry + 4);

    /* VisualSampleEntry: 8(hdr) + 6(rsvd) + 2(dref) + 16(pre) + 2(w) + 2(h) = offset 36 */
    if (entry + 36 <= entry_end && tk->handler_type != 2) {
        uint16_t w = media_be16(data + entry + 32);
        uint16_t h = media_be16(data + entry + 34);
        if (w > 0 && w <= 4096 && h > 0 && h <= 4096) {
            tk->width = w;
            tk->height = h;
        }
        size_t sub = entry + 86;
        while (sub + 8 <= entry_end) {
            size_t bp, pp, be;
            const uint8_t *tp;
            if (!mp4_next_box(data, sub, entry_end, &bp, &pp, &be, &tp)) break;
            if (media_eq4(tp, 'e', 's', 'd', 's')) {
                tk->esds_oti = mp4_parse_esds_oti(data, pp, be);
            }
            sub = be;
        }
    }

    /* AudioSampleEntry: 8(hdr) + 6(rsvd) + 2(dref) + 2(ver) + 6(rsvd) + 2(ch) + 2(bits) + 4(rsvd) + 4(sr_16_16) = 36 */
    if (entry + 36 <= entry_end && tk->handler_type != 1) {
        uint16_t ver = media_be16(data + entry + 16);
        uint16_t ch = media_be16(data + entry + 24);
        uint16_t bits = media_be16(data + entry + 26);
        uint32_t sr = media_be32(data + entry + 32) >> 16;
        if (ch >= 1 && ch <= 8) tk->channels = ch;
        if (bits >= 8 && bits <= 32) tk->bits_per_sample = bits;
        if (sr >= 4000 && sr <= 192000) tk->sample_rate = sr;

        size_t sub = entry + 36;
        if (ver == 1 && sub + 16 <= entry_end) sub += 16;
        else if (ver == 2 && sub + 36 <= entry_end) sub += 36;
        while (sub + 8 <= entry_end) {
            size_t bp, pp, be;
            const uint8_t *tp;
            if (!mp4_next_box(data, sub, entry_end, &bp, &pp, &be, &tp)) break;
            if (media_eq4(tp, 'e', 's', 'd', 's')) {
                tk->esds_oti = mp4_parse_esds_oti(data, pp, be);
            }
            sub = be;
        }
    }
}

static void mp4_parse_stbl(const uint8_t *data, size_t start, size_t end,
                           mp4_trak_parse_t *tk) {
    size_t pos = start;
    while (pos + 8 <= end) {
        size_t bp, pp, be;
        const uint8_t *tp;
        if (!mp4_next_box(data, pos, end, &bp, &pp, &be, &tp)) break;

        if (media_eq4(tp, 's', 't', 's', 'd')) {
            mp4_parse_stsd(data, pp, be, tk);
        } else if (media_eq4(tp, 's', 't', 't', 's') && pp + 8 <= be) {
            uint32_t cnt = media_be32(data + pp + 4);
            if (pp + 8 + (uint64_t)cnt * 8ULL <= be) {
                tk->tab.stts_offset = (uint32_t)(pp + 8);
                tk->tab.stts_count = cnt;
            }
        } else if (media_eq4(tp, 's', 't', 's', 'c') && pp + 8 <= be) {
            uint32_t cnt = media_be32(data + pp + 4);
            if (pp + 8 + (uint64_t)cnt * 12ULL <= be) {
                tk->tab.stsc_offset = (uint32_t)(pp + 8);
                tk->tab.stsc_count = cnt;
            }
        } else if (media_eq4(tp, 's', 't', 's', 'z') && pp + 12 <= be) {
            uint32_t fixed_sz = media_be32(data + pp + 4);
            uint32_t cnt = media_be32(data + pp + 8);
            if (fixed_sz != 0 || (pp + 12 + (uint64_t)cnt * 4ULL <= be)) {
                tk->tab.stsz_fixed_size = fixed_sz;
                tk->tab.stsz_count = cnt;
                tk->tab.stsz_offset = (uint32_t)(pp + 12);
            }
        } else if (media_eq4(tp, 's', 't', 'c', 'o') && pp + 8 <= be) {
            uint32_t cnt = media_be32(data + pp + 4);
            if (pp + 8 + (uint64_t)cnt * 4ULL <= be) {
                tk->tab.stco_offset = (uint32_t)(pp + 8);
                tk->tab.stco_count = cnt;
                tk->tab.stco_is_64 = 0;
            }
        } else if (media_eq4(tp, 'c', 'o', '6', '4') && pp + 8 <= be) {
            uint32_t cnt = media_be32(data + pp + 4);
            if (pp + 8 + (uint64_t)cnt * 8ULL <= be) {
                tk->tab.stco_offset = (uint32_t)(pp + 8);
                tk->tab.stco_count = cnt;
                tk->tab.stco_is_64 = 1;
            }
        }
        pos = be;
    }
}

static void mp4_parse_mdia(const uint8_t *data, size_t start, size_t end,
                           mp4_trak_parse_t *tk) {
    size_t pos = start;
    size_t minf_pp = 0, minf_be = 0;
    while (pos + 8 <= end) {
        size_t bp, pp, be;
        const uint8_t *tp;
        if (!mp4_next_box(data, pos, end, &bp, &pp, &be, &tp)) break;

        if (media_eq4(tp, 'm', 'd', 'h', 'd') && pp + 20 <= be) {
            uint8_t ver = data[pp];
            if (ver == 0 && pp + 20 <= be) {
                tk->tab.timescale = media_be32(data + pp + 12);
                tk->tab.duration_units = media_be32(data + pp + 16);
            } else if (ver == 1 && pp + 32 <= be) {
                tk->tab.timescale = media_be32(data + pp + 20);
                tk->tab.duration_units = media_be64(data + pp + 24);
            }
        } else if (media_eq4(tp, 'h', 'd', 'l', 'r') && pp + 12 <= be) {
            if (media_eq4(data + pp + 8, 'v', 'i', 'd', 'e')) tk->handler_type = 1;
            else if (media_eq4(data + pp + 8, 's', 'o', 'u', 'n')) tk->handler_type = 2;
        } else if (media_eq4(tp, 'm', 'i', 'n', 'f')) {
            minf_pp = pp;
            minf_be = be;
        }
        pos = be;
    }

    if (minf_pp && minf_be > minf_pp) {
        pos = minf_pp;
        while (pos + 8 <= minf_be) {
            size_t bp, pp, be;
            const uint8_t *tp;
            if (!mp4_next_box(data, pos, minf_be, &bp, &pp, &be, &tp)) break;
            if (media_eq4(tp, 's', 't', 'b', 'l')) {
                mp4_parse_stbl(data, pp, be, tk);
            }
            pos = be;
        }
    }
}

static void mp4_parse_trak(const uint8_t *data, size_t start, size_t end,
                           mp4_trak_parse_t *tk) {
    media_zero(tk, sizeof(*tk));
    size_t pos = start;
    while (pos + 8 <= end) {
        size_t bp, pp, be;
        const uint8_t *tp;
        if (!mp4_next_box(data, pos, end, &bp, &pp, &be, &tp)) break;

        if (media_eq4(tp, 't', 'k', 'h', 'd')) {
            uint8_t ver = (pp < be) ? data[pp] : 0;
            size_t wh_off = (ver == 1) ? (pp + 88) : (pp + 76);
            if (wh_off + 8 <= be) {
                uint16_t w = (uint16_t)(media_be32(data + wh_off) >> 16);
                uint16_t h = (uint16_t)(media_be32(data + wh_off + 4) >> 16);
                if (tk->width == 0 && w > 0 && w <= 4096) tk->width = w;
                if (tk->height == 0 && h > 0 && h <= 4096) tk->height = h;
            }
        } else if (media_eq4(tp, 'm', 'd', 'i', 'a')) {
            mp4_parse_mdia(data, pp, be, tk);
        }
        pos = be;
    }
}

static int mp4_table_locate_sample(const liwus_mp4_table_t *tab,
                                   const uint8_t *data, size_t size,
                                   uint32_t sample_index,
                                   const uint8_t **sample_ptr,
                                   uint32_t *sample_size,
                                   uint32_t *pts_ms) {
    if (!tab || !data || sample_index >= tab->stsz_count ||
        tab->stco_count == 0 || tab->stsc_count == 0) {
        return -1;
    }

    /* 1. Tamanho da amostra sample_index */
    uint32_t target_sz = tab->stsz_fixed_size;
    if (target_sz == 0) {
        size_t sz_pos = (size_t)tab->stsz_offset + (size_t)sample_index * 4U;
        if (sz_pos + 4U > size) return -1;
        target_sz = media_be32(data + sz_pos);
    }

    /* 2. Localiza em qual chunk (1-based) e qual indice dentro do chunk esta sample_index */
    uint32_t cur_sample = 0;
    uint32_t target_chunk = 0; /* 0-based */
    uint32_t index_in_chunk = 0;
    int found_chunk = 0;

    for (uint32_t i = 0; i < tab->stsc_count; i++) {
        size_t epos = (size_t)tab->stsc_offset + (size_t)i * 12U;
        if (epos + 12U > size) return -1;
        uint32_t first_chunk = media_be32(data + epos);
        uint32_t spc = media_be32(data + epos + 4U);
        if (first_chunk == 0 || spc == 0) return -1;

        uint32_t next_first_chunk = tab->stco_count + 1U;
        if (i + 1U < tab->stsc_count) {
            size_t npos = epos + 12U;
            if (npos + 4U <= size) {
                uint32_t nfc = media_be32(data + npos);
                if (nfc >= first_chunk && nfc <= tab->stco_count + 1U) {
                    next_first_chunk = nfc;
                }
            }
        }
        uint32_t num_chunks = (next_first_chunk > first_chunk)
                            ? (next_first_chunk - first_chunk) : 0U;
        uint64_t group_samples = (uint64_t)num_chunks * (uint64_t)spc;

        if ((uint64_t)(sample_index - cur_sample) < group_samples) {
            uint32_t rel = sample_index - cur_sample;
            target_chunk = (first_chunk - 1U) + (rel / spc);
            index_in_chunk = rel % spc;
            cur_sample += (rel / spc) * spc;
            found_chunk = 1;
            break;
        }
        cur_sample += (uint32_t)group_samples;
    }

    if (!found_chunk || target_chunk >= tab->stco_count) return -1;

    /* 3. Offset inicial do chunk em stco / co64 */
    uint64_t chunk_off = 0;
    if (tab->stco_is_64) {
        size_t cpos = (size_t)tab->stco_offset + (size_t)target_chunk * 8U;
        if (cpos + 8U > size) return -1;
        chunk_off = media_be64(data + cpos);
    } else {
        size_t cpos = (size_t)tab->stco_offset + (size_t)target_chunk * 4U;
        if (cpos + 4U > size) return -1;
        chunk_off = media_be32(data + cpos);
    }

    /* 4. Soma os tamanhos das amostras anteriores dentro do mesmo chunk */
    for (uint32_t k = 0; k < index_in_chunk; k++) {
        uint32_t s_idx = cur_sample + k;
        uint32_t s_sz = tab->stsz_fixed_size;
        if (s_sz == 0) {
            size_t sp = (size_t)tab->stsz_offset + (size_t)s_idx * 4U;
            if (sp + 4U > size) return -1;
            s_sz = media_be32(data + sp);
        }
        chunk_off += s_sz;
    }

    if (chunk_off > size || (uint64_t)target_sz > (size - (size_t)chunk_off)) {
        return -1;
    }

    /* 5. Calcula PTS em ms via stts */
    uint64_t ticks = 0;
    uint32_t rem = sample_index;
    for (uint32_t i = 0; i < tab->stts_count && rem > 0; i++) {
        size_t tpos = (size_t)tab->stts_offset + (size_t)i * 8U;
        if (tpos + 8U > size) break;
        uint32_t sc = media_be32(data + tpos);
        uint32_t sd = media_be32(data + tpos + 4U);
        if (rem < sc) {
            ticks += (uint64_t)rem * (uint64_t)sd;
            rem = 0;
        } else {
            ticks += (uint64_t)sc * (uint64_t)sd;
            rem -= sc;
        }
    }

    uint32_t timescale = tab->timescale ? tab->timescale : 1000U;
    if (sample_ptr) *sample_ptr = data + (size_t)chunk_off;
    if (sample_size) *sample_size = target_sz;
    if (pts_ms) *pts_ms = (uint32_t)((ticks * 1000ULL) / timescale);
    return 0;
}

int liwus_mp4_parse(const uint8_t *data, size_t size, liwus_mp4_index_t *out) {
    if (!data || !out || size < 16) return -1;
    media_zero(out, sizeof(*out));

    if (!media_eq4(data + 4, 'f', 't', 'y', 'p')) return -1;

    uint32_t mvhd_timescale = 0;
    uint64_t mvhd_duration = 0;
    size_t pos = 0;
    int saw_moov = 0;

    while (pos + 8 <= size) {
        size_t bp, pp, be;
        const uint8_t *tp;
        if (!mp4_next_box(data, pos, size, &bp, &pp, &be, &tp)) break;

        if (media_eq4(tp, 'm', 'o', 'o', 'v')) {
            saw_moov = 1;
            size_t mpos = pp;
            while (mpos + 8 <= be) {
                size_t mbp, mpp, mbe;
                const uint8_t *mtp;
                if (!mp4_next_box(data, mpos, be, &mbp, &mpp, &mbe, &mtp)) break;

                if (media_eq4(mtp, 'm', 'v', 'h', 'd') && mpp + 20 <= mbe) {
                    uint8_t ver = data[mpp];
                    if (ver == 0 && mpp + 20 <= mbe) {
                        mvhd_timescale = media_be32(data + mpp + 12);
                        mvhd_duration = media_be32(data + mpp + 16);
                    } else if (ver == 1 && mpp + 32 <= mbe) {
                        mvhd_timescale = media_be32(data + mpp + 20);
                        mvhd_duration = media_be64(data + mpp + 24);
                    }
                } else if (media_eq4(mtp, 't', 'r', 'a', 'k')) {
                    mp4_trak_parse_t tk;
                    mp4_parse_trak(data, mpp, mbe, &tk);
                    if (tk.handler_type == 1 && !out->has_video && tk.tab.stsz_count > 0) {
                        out->has_video = 1;
                        out->width = tk.width ? tk.width : 320;
                        out->height = tk.height ? tk.height : 180;
                        out->video_sample_count = tk.tab.stsz_count;
                        out->video_tab = tk.tab;
                        for (int i = 0; i < 5; i++) out->video_fourcc[i] = tk.fourcc[i];

                        if (media_eq4((const uint8_t *)tk.fourcc, 'j', 'p', 'e', 'g') ||
                            media_eq4((const uint8_t *)tk.fourcc, 'm', 'j', 'p', 'a') ||
                            media_eq4((const uint8_t *)tk.fourcc, 'M', 'J', 'P', 'G') ||
                            tk.esds_oti == 0x6C) {
                            out->video_codec = LIWUS_VCODEC_MJPEG;
                        } else if (media_eq4((const uint8_t *)tk.fourcc, 'r', 'a', 'w', ' ') ||
                                   media_eq4((const uint8_t *)tk.fourcc, 'R', 'G', 'B', '2')) {
                            out->video_codec = LIWUS_VCODEC_RAW_RGB24;
                        } else {
                            out->video_codec = LIWUS_VCODEC_UNSUPPORTED;
                        }

                        /* Inspeciona a primeira amostra para confirmar JPEG em mp4v */
                        const uint8_t *s0 = NULL;
                        uint32_t s0_sz = 0;
                        if (mp4_table_locate_sample(&out->video_tab, data, size, 0, &s0, &s0_sz, NULL) == 0 &&
                            s0 && s0_sz >= 4) {
                            if (s0[0] == 0xFF && s0[1] == 0xD8) {
                                out->video_codec = LIWUS_VCODEC_MJPEG;
                            }
                        }

                        if (tk.tab.timescale > 0 && tk.tab.duration_units > 0) {
                            uint32_t d_ms = (uint32_t)((tk.tab.duration_units * 1000ULL) / tk.tab.timescale);
                            if (d_ms > out->duration_ms) out->duration_ms = d_ms;
                            if (d_ms > 0) {
                                out->fps_x100 = (uint32_t)(((uint64_t)tk.tab.stsz_count * 100000ULL) / d_ms);
                            }
                        }
                    } else if (tk.handler_type == 2 && !out->has_audio && tk.tab.stsz_count > 0) {
                        out->has_audio = 1;
                        out->audio_sample_count = tk.tab.stsz_count;
                        out->audio_sample_rate = tk.sample_rate ? tk.sample_rate : 48000;
                        out->audio_channels = tk.channels ? tk.channels : 2;
                        out->audio_bits = tk.bits_per_sample ? tk.bits_per_sample : 16;
                        out->audio_tab = tk.tab;
                        for (int i = 0; i < 5; i++) out->audio_fourcc[i] = tk.fourcc[i];

                        if (media_eq4((const uint8_t *)tk.fourcc, '.', 'm', 'p', '3') ||
                            media_eq4((const uint8_t *)tk.fourcc, 'm', 'p', '3', ' ') ||
                            tk.esds_oti == 0x69 || tk.esds_oti == 0x6B) {
                            out->audio_codec = LIWUS_ACODEC_MP3;
                        } else if (media_eq4((const uint8_t *)tk.fourcc, 's', 'o', 'w', 't') ||
                                   media_eq4((const uint8_t *)tk.fourcc, 'l', 'p', 'c', 'm')) {
                            out->audio_codec = LIWUS_ACODEC_PCM_S16LE;
                        } else if (media_eq4((const uint8_t *)tk.fourcc, 't', 'w', 'o', 's')) {
                            out->audio_codec = LIWUS_ACODEC_PCM_S16BE;
                        } else if (media_eq4((const uint8_t *)tk.fourcc, 'r', 'a', 'w', ' ')) {
                            out->audio_codec = LIWUS_ACODEC_PCM_U8;
                        } else {
                            out->audio_codec = LIWUS_ACODEC_UNSUPPORTED;
                        }

                        const uint8_t *a0 = NULL;
                        uint32_t a0_sz = 0;
                        if (mp4_table_locate_sample(&out->audio_tab, data, size, 0, &a0, &a0_sz, NULL) == 0 &&
                            a0 && a0_sz >= 4) {
                            if ((a0[0] == 0xFF && (a0[1] & 0xE0) == 0xE0) ||
                                (a0[0] == 'I' && a0[1] == 'D' && a0[2] == '3')) {
                                out->audio_codec = LIWUS_ACODEC_MP3;
                            }
                        }

                        if (tk.tab.timescale > 0 && tk.tab.duration_units > 0) {
                            uint32_t d_ms = (uint32_t)((tk.tab.duration_units * 1000ULL) / tk.tab.timescale);
                            if (d_ms > out->duration_ms) out->duration_ms = d_ms;
                        }
                    }
                }
                mpos = mbe;
            }
        }
        pos = be;
    }

    if (!saw_moov) return -1;
    if (out->duration_ms == 0 && mvhd_timescale > 0 && mvhd_duration > 0) {
        out->duration_ms = (uint32_t)((mvhd_duration * 1000ULL) / mvhd_timescale);
    }
    if (out->fps_x100 == 0 && out->video_sample_count > 0 && out->duration_ms > 0) {
        out->fps_x100 = (uint32_t)(((uint64_t)out->video_sample_count * 100000ULL) / out->duration_ms);
    }
    if (out->fps_x100 == 0 && out->has_video) out->fps_x100 = 1500;
    return (out->has_video || out->has_audio) ? 0 : -1;
}

int liwus_mp4_get_video_sample(const liwus_mp4_index_t *mp4,
                               const uint8_t *data, size_t size,
                               uint32_t sample_index,
                               const uint8_t **sample_ptr,
                               uint32_t *sample_size,
                               uint32_t *pts_ms) {
    if (!mp4 || !mp4->has_video) return -1;
    return mp4_table_locate_sample(&mp4->video_tab, data, size, sample_index,
                                   sample_ptr, sample_size, pts_ms);
}

int liwus_mp4_get_audio_sample(const liwus_mp4_index_t *mp4,
                               const uint8_t *data, size_t size,
                               uint32_t sample_index,
                               const uint8_t **sample_ptr,
                               uint32_t *sample_size,
                               uint32_t *pts_ms) {
    if (!mp4 || !mp4->has_audio) return -1;
    return mp4_table_locate_sample(&mp4->audio_tab, data, size, sample_index,
                                   sample_ptr, sample_size, pts_ms);
}

/* =========================================================================
 * Decodificador Baseline JPEG (MJPEG) Inteiro p/ Quadros de Video MP4
 * ========================================================================= */
typedef struct {
    uint16_t min_code[17];
    int32_t  max_code[17];
    uint16_t val_ptr[17];
    uint8_t  values[256];
    uint8_t  valid;
} jpeg_huff_t;

typedef struct {
    uint8_t id;
    uint8_t h_samp;
    uint8_t v_samp;
    uint8_t quant_id;
    uint8_t dc_id;
    uint8_t ac_id;
    int32_t prev_dc;
} jpeg_comp_t;

typedef struct {
    const uint8_t *data;
    uint32_t size;
    uint32_t scan_pos;
    uint32_t bit_buf;
    int      bit_count;
    int      marker_hit;
    uint16_t width;
    uint16_t height;
    uint8_t  num_comp;
    uint8_t  max_h;
    uint8_t  max_v;
    uint16_t restart_interval;
    uint16_t qtable[4][64];
    jpeg_huff_t dc_huff[4];
    jpeg_huff_t ac_huff[4];
    jpeg_comp_t comp[3];
} jpeg_dec_t;

static const uint8_t g_jpeg_zigzag[64] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

/* Tabelas Huffman padrao ISO/IEC 10918-1 Annex K (usadas caso o encoder MJPEG omita DHT) */
static const uint8_t g_std_dc_lum_bits[16] = {0,1,5,1,1,1,1,1,1,0,0,0,0,0,0,0};
static const uint8_t g_std_dc_lum_val[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t g_std_dc_chr_bits[16] = {0,3,1,1,1,1,1,1,1,1,1,0,0,0,0,0};
static const uint8_t g_std_dc_chr_val[12]  = {0,1,2,3,4,5,6,7,8,9,10,11};
static const uint8_t g_std_ac_lum_bits[16] = {0,2,1,3,3,2,4,3,5,5,4,4,0,0,1,125};
static const uint8_t g_std_ac_lum_val[162] = {
    0x01,0x02,0x03,0x00,0x04,0x11,0x05,0x12,0x21,0x31,0x41,0x06,0x13,0x51,0x61,0x07,
    0x22,0x71,0x14,0x32,0x81,0x91,0xa1,0x08,0x23,0x42,0xb1,0xc1,0x15,0x52,0xd1,0xf0,
    0x24,0x33,0x62,0x72,0x82,0x09,0x0a,0x16,0x17,0x18,0x19,0x1a,0x25,0x26,0x27,0x28,
    0x29,0x2a,0x34,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,0x49,
    0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,0x69,
    0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x83,0x84,0x85,0x86,0x87,0x88,0x89,
    0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,0xa6,0xa7,
    0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,0xc4,0xc5,
    0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,0xe1,0xe2,
    0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};
static const uint8_t g_std_ac_chr_bits[16] = {0,2,1,2,4,4,3,4,7,5,4,4,0,1,2,119};
static const uint8_t g_std_ac_chr_val[162] = {
    0x00,0x01,0x02,0x03,0x11,0x04,0x05,0x21,0x31,0x06,0x12,0x41,0x51,0x07,0x61,0x71,
    0x13,0x22,0x32,0x81,0x08,0x14,0x42,0x91,0xa1,0xb1,0xc1,0x09,0x23,0x33,0x52,0xf0,
    0x15,0x62,0x72,0xd1,0x0a,0x16,0x24,0x34,0xe1,0x25,0xf1,0x17,0x18,0x19,0x1a,0x26,
    0x27,0x28,0x29,0x2a,0x35,0x36,0x37,0x38,0x39,0x3a,0x43,0x44,0x45,0x46,0x47,0x48,
    0x49,0x4a,0x53,0x54,0x55,0x56,0x57,0x58,0x59,0x5a,0x63,0x64,0x65,0x66,0x67,0x68,
    0x69,0x6a,0x73,0x74,0x75,0x76,0x77,0x78,0x79,0x7a,0x82,0x83,0x84,0x85,0x86,0x87,
    0x88,0x89,0x8a,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0xa2,0xa3,0xa4,0xa5,
    0xa6,0xa7,0xa8,0xa9,0xaa,0xb2,0xb3,0xb4,0xb5,0xb6,0xb7,0xb8,0xb9,0xba,0xc2,0xc3,
    0xc4,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd8,0xd9,0xda,
    0xe2,0xe3,0xe4,0xe5,0xe6,0xe7,0xe8,0xe9,0xea,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8,
    0xf9,0xfa
};

static void jpeg_build_huff(jpeg_huff_t *ht, const uint8_t bits[16], const uint8_t *vals, uint32_t total_vals) {
    media_zero(ht, sizeof(*ht));
    for (uint32_t i = 0; i < total_vals && i < 256; i++) ht->values[i] = vals[i];
    uint16_t code = 0;
    uint16_t k = 0;
    for (int len = 1; len <= 16; len++) {
        uint8_t cnt = bits[len - 1];
        if (cnt == 0) {
            ht->max_code[len] = -1;
        } else {
            ht->val_ptr[len] = k;
            ht->min_code[len] = code;
            code = (uint16_t)(code + cnt);
            ht->max_code[len] = (int32_t)code - 1;
            k = (uint16_t)(k + cnt);
        }
        code = (uint16_t)(code << 1);
    }
    ht->valid = 1;
}

static void jpeg_fill_bits(jpeg_dec_t *j) {
    while (j->bit_count <= 24) {
        if (j->marker_hit || j->scan_pos >= j->size) {
            j->bit_buf <<= 8;
            j->bit_count += 8;
            continue;
        }
        uint8_t b = j->data[j->scan_pos++];
        if (b == 0xFF) {
            while (j->scan_pos < j->size && j->data[j->scan_pos] == 0xFF) {
                j->scan_pos++;
            }
            if (j->scan_pos >= j->size) {
                j->marker_hit = 1;
                j->bit_buf <<= 8;
                j->bit_count += 8;
                continue;
            }
            uint8_t m = j->data[j->scan_pos++];
            if (m == 0x00) {
                b = 0xFF;
            } else if (m >= 0xD0 && m <= 0xD7) {
                continue;
            } else {
                j->marker_hit = 1;
                j->bit_buf <<= 8;
                j->bit_count += 8;
                continue;
            }
        }
        j->bit_buf = (j->bit_buf << 8) | (uint32_t)b;
        j->bit_count += 8;
    }
}

static int jpeg_get_bits(jpeg_dec_t *j, int n) {
    if (n <= 0) return 0;
    if (j->bit_count < n) jpeg_fill_bits(j);
    uint32_t val = (j->bit_buf >> (j->bit_count - n)) & ((1U << n) - 1U);
    j->bit_count -= n;
    return (int)val;
}

static int jpeg_decode_huff(jpeg_dec_t *j, const jpeg_huff_t *ht) {
    int32_t code = 0;
    for (int len = 1; len <= 16; len++) {
        if (j->bit_count < 1) jpeg_fill_bits(j);
        int bit = (int)((j->bit_buf >> (j->bit_count - 1)) & 1U);
        j->bit_count--;
        code = (code << 1) | bit;
        if (code <= ht->max_code[len]) {
            uint32_t idx = (uint32_t)ht->val_ptr[len] + (uint32_t)(code - (int32_t)ht->min_code[len]);
            return ht->values[idx & 0xFFU];
        }
    }
    return 0;
}

static int jpeg_extend(int v, int t) {
    if (t <= 0) return 0;
    int vt = 1 << (t - 1);
    if (v < vt) v -= (1 << t) - 1;
    return v;
}

static uint8_t jpeg_clamp_u8(int32_t x) {
    if (x < 0) return 0;
    if (x > 255) return 255;
    return (uint8_t)x;
}

/* IDCT 8x8 inteira rapida (AAN / Loeffler escalado) */
static void jpeg_idct_8x8(const int32_t block[64], uint8_t out[64]) {
    int32_t tmp[64];

    for (int i = 0; i < 8; i++) {
        const int32_t *s = block + i * 8;
        if (s[1] == 0 && s[2] == 0 && s[3] == 0 && s[4] == 0 &&
            s[5] == 0 && s[6] == 0 && s[7] == 0) {
            int32_t dc = s[0] * 4;
            for (int c = 0; c < 8; c++) tmp[i * 8 + c] = dc;
            continue;
        }
        int32_t x0 = (s[0] * 2048) + 128;
        int32_t x1 = s[4] * 2048;
        int32_t x2 = s[6];
        int32_t x3 = s[2];
        int32_t x4 = s[1];
        int32_t x5 = s[7];
        int32_t x6 = s[5];
        int32_t x7 = s[3];

        int32_t x8 = 565 * (x4 + x5);
        x4 = x8 + 2276 * x4;
        x5 = x8 - 3406 * x5;
        x8 = 2408 * (x6 + x7);
        x6 = x8 - 799 * x6;
        x7 = x8 - 4017 * x7;

        x8 = x0 + x1;
        x0 -= x1;
        x1 = 1108 * (x3 + x2);
        x2 = x1 - 3784 * x2;
        x3 = x1 + 1568 * x3;
        x1 = x4 + x6;
        x4 -= x6;
        x6 = x5 + x7;
        x5 -= x7;

        int32_t t0 = x8 + x3;
        int32_t t3 = x8 - x3;
        int32_t t1 = x0 + x2;
        int32_t t2 = x0 - x2;
        int32_t t4 = (181 * (x4 + x5) + 128) >> 8;
        int32_t t5 = (181 * (x4 - x5) + 128) >> 8;

        tmp[i * 8 + 0] = (t0 + x1) >> 8;
        tmp[i * 8 + 7] = (t0 - x1) >> 8;
        tmp[i * 8 + 1] = (t1 + t4) >> 8;
        tmp[i * 8 + 6] = (t1 - t4) >> 8;
        tmp[i * 8 + 2] = (t2 + t5) >> 8;
        tmp[i * 8 + 5] = (t2 - t5) >> 8;
        tmp[i * 8 + 3] = (t3 + x6) >> 8;
        tmp[i * 8 + 4] = (t3 - x6) >> 8;
    }

    for (int c = 0; c < 8; c++) {
        int32_t x0 = (tmp[0 * 8 + c] * 256) + 8192;
        int32_t x1 = tmp[4 * 8 + c] * 256;
        int32_t x2 = tmp[6 * 8 + c];
        int32_t x3 = tmp[2 * 8 + c];
        int32_t x4 = tmp[1 * 8 + c];
        int32_t x5 = tmp[7 * 8 + c];
        int32_t x6 = tmp[5 * 8 + c];
        int32_t x7 = tmp[3 * 8 + c];

        int32_t x8 = 565 * (x4 + x5) + 4;
        x4 = (x8 + 2276 * x4) >> 3;
        x5 = (x8 - 3406 * x5) >> 3;
        x8 = 2408 * (x6 + x7) + 4;
        x6 = (x8 - 799 * x6) >> 3;
        x7 = (x8 - 4017 * x7) >> 3;

        x8 = x0 + x1;
        x0 -= x1;
        x1 = 1108 * (x3 + x2) + 4;
        x2 = (x1 - 3784 * x2) >> 3;
        x3 = (x1 + 1568 * x3) >> 3;
        x1 = x4 + x6;
        x4 -= x6;
        x6 = x5 + x7;
        x5 -= x7;

        int32_t t0 = x8 + x3;
        int32_t t3 = x8 - x3;
        int32_t t1 = x0 + x2;
        int32_t t2 = x0 - x2;
        int32_t t4 = (181 * (x4 + x5) + 128) >> 8;
        int32_t t5 = (181 * (x4 - x5) + 128) >> 8;

        out[0 * 8 + c] = jpeg_clamp_u8(((t0 + x1) >> 14) + 128);
        out[7 * 8 + c] = jpeg_clamp_u8(((t0 - x1) >> 14) + 128);
        out[1 * 8 + c] = jpeg_clamp_u8(((t1 + t4) >> 14) + 128);
        out[6 * 8 + c] = jpeg_clamp_u8(((t1 - t4) >> 14) + 128);
        out[2 * 8 + c] = jpeg_clamp_u8(((t2 + t5) >> 14) + 128);
        out[5 * 8 + c] = jpeg_clamp_u8(((t2 - t5) >> 14) + 128);
        out[3 * 8 + c] = jpeg_clamp_u8(((t3 + x6) >> 14) + 128);
        out[4 * 8 + c] = jpeg_clamp_u8(((t3 - x6) >> 14) + 128);
    }
}

static int jpeg_decode_block(jpeg_dec_t *j, int comp_idx, uint8_t out_pixels[64]) {
    jpeg_comp_t *c = &j->comp[comp_idx];
    const jpeg_huff_t *dc_ht = &j->dc_huff[c->dc_id & 3];
    const jpeg_huff_t *ac_ht = &j->ac_huff[c->ac_id & 3];
    const uint16_t *qt = j->qtable[c->quant_id & 3];

    int32_t coef[64];
    media_zero(coef, sizeof(coef));

    int t = jpeg_decode_huff(j, dc_ht);
    int diff = jpeg_extend(jpeg_get_bits(j, t), t);
    c->prev_dc += diff;
    coef[0] = c->prev_dc * (int32_t)qt[0];

    int k = 1;
    while (k < 64) {
        int rs = jpeg_decode_huff(j, ac_ht);
        int r = (rs >> 4) & 0x0F;
        int s = rs & 0x0F;
        if (s == 0) {
            if (r == 15) {
                k += 16;
                continue;
            }
            break;
        }
        k += r;
        if (k >= 64) break;
        int v = jpeg_extend(jpeg_get_bits(j, s), s);
        uint8_t zz = g_jpeg_zigzag[k];
        coef[zz] = (int32_t)v * (int32_t)qt[k];
        k++;
    }

    jpeg_idct_8x8(coef, out_pixels);
    return 0;
}

static int jpeg_decode_to_argb(const uint8_t *data, uint32_t size,
                               uint32_t *argb_out, uint16_t out_w, uint16_t out_h) {
    if (!data || size < 16 || !argb_out || out_w == 0 || out_h == 0) return -1;
    if (data[0] != 0xFF || data[1] != 0xD8) return -1;

    static jpeg_dec_t j;
    media_zero(&j, sizeof(j));
    j.data = data;
    j.size = size;

    /* Inicializa tabelas Huffman padrao caso o frame MJPEG omita DHT */
    jpeg_build_huff(&j.dc_huff[0], g_std_dc_lum_bits, g_std_dc_lum_val, sizeof(g_std_dc_lum_val));
    jpeg_build_huff(&j.dc_huff[1], g_std_dc_chr_bits, g_std_dc_chr_val, sizeof(g_std_dc_chr_val));
    jpeg_build_huff(&j.ac_huff[0], g_std_ac_lum_bits, g_std_ac_lum_val, sizeof(g_std_ac_lum_val));
    jpeg_build_huff(&j.ac_huff[1], g_std_ac_chr_bits, g_std_ac_chr_val, sizeof(g_std_ac_chr_val));

    uint32_t pos = 2;
    int ready_for_scan = 0;

    while (pos + 2 <= size && !ready_for_scan) {
        if (data[pos] != 0xFF) {
            pos++;
            continue;
        }
        while (pos < size && data[pos] == 0xFF) pos++;
        if (pos >= size) break;
        uint8_t marker = data[pos++];
        if (marker == 0xD9) break;
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)) continue;
        if (pos + 2 > size) return -1;
        uint16_t seg_len = media_be16(data + pos);
        if (seg_len < 2 || pos + seg_len > size) return -1;
        uint32_t seg_start = pos + 2;
        uint32_t seg_end = pos + seg_len;

        if (marker == 0xDB) { /* DQT */
            uint32_t p = seg_start;
            while (p < seg_end) {
                uint8_t pq_tq = data[p++];
                uint8_t prec = (pq_tq >> 4) & 0x0F;
                uint8_t tq = pq_tq & 0x03;
                if (prec == 0) {
                    if (p + 64 > seg_end) return -1;
                    for (int i = 0; i < 64; i++) j.qtable[tq][i] = data[p++];
                } else {
                    if (p + 128 > seg_end) return -1;
                    for (int i = 0; i < 64; i++) {
                        j.qtable[tq][i] = media_be16(data + p);
                        p += 2;
                    }
                }
            }
        } else if (marker == 0xC0) { /* SOF0 */
            if (seg_start + 6 > seg_end) return -1;
            j.height = media_be16(data + seg_start + 1);
            j.width = media_be16(data + seg_start + 3);
            j.num_comp = data[seg_start + 5];
            if (j.width == 0 || j.height == 0 || (j.num_comp != 1 && j.num_comp != 3)) return -1;
            if (seg_start + 6 + (uint32_t)j.num_comp * 3U > seg_end) return -1;
            j.max_h = 1;
            j.max_v = 1;
            for (uint8_t c = 0; c < j.num_comp; c++) {
                uint32_t cp = seg_start + 6 + (uint32_t)c * 3U;
                j.comp[c].id = data[cp];
                uint8_t hv = data[cp + 1];
                j.comp[c].h_samp = (hv >> 4) & 0x0F;
                j.comp[c].v_samp = hv & 0x0F;
                j.comp[c].quant_id = data[cp + 2] & 0x03;
                if (j.comp[c].h_samp == 0 || j.comp[c].h_samp > 2 ||
                    j.comp[c].v_samp == 0 || j.comp[c].v_samp > 2) return -1;
                if (j.comp[c].h_samp > j.max_h) j.max_h = j.comp[c].h_samp;
                if (j.comp[c].v_samp > j.max_v) j.max_v = j.comp[c].v_samp;
            }
        } else if (marker == 0xC4) { /* DHT */
            uint32_t p = seg_start;
            while (p + 17 <= seg_end) {
                uint8_t tc_th = data[p++];
                uint8_t tc = (tc_th >> 4) & 0x0F;
                uint8_t th = tc_th & 0x03;
                uint32_t total = 0;
                for (int i = 0; i < 16; i++) total += data[p + i];
                if (p + 16 + total > seg_end) return -1;
                if (tc == 0) jpeg_build_huff(&j.dc_huff[th], data + p, data + p + 16, total);
                else jpeg_build_huff(&j.ac_huff[th], data + p, data + p + 16, total);
                p += 16 + total;
            }
        } else if (marker == 0xDD) { /* DRI */
            if (seg_start + 2 <= seg_end) j.restart_interval = media_be16(data + seg_start);
        } else if (marker == 0xDA) { /* SOS */
            if (seg_start + 1 > seg_end) return -1;
            uint8_t ns = data[seg_start];
            if (seg_start + 1 + (uint32_t)ns * 2U + 3U > seg_end) return -1;
            for (uint8_t s = 0; s < ns; s++) {
                uint8_t cid = data[seg_start + 1 + s * 2U];
                uint8_t td_ta = data[seg_start + 2 + s * 2U];
                for (uint8_t c = 0; c < j.num_comp; c++) {
                    if (j.comp[c].id == cid) {
                        j.comp[c].dc_id = (td_ta >> 4) & 0x03;
                        j.comp[c].ac_id = td_ta & 0x03;
                    }
                }
            }
            j.scan_pos = seg_end;
            ready_for_scan = 1;
        }
        pos = seg_end;
    }

    if (!ready_for_scan || j.width == 0 || j.height == 0) return -1;

    uint16_t mcu_w = (uint16_t)(j.max_h * 8U);
    uint16_t mcu_h = (uint16_t)(j.max_v * 8U);
    uint16_t mcus_x = (uint16_t)((j.width + mcu_w - 1U) / mcu_w);
    uint16_t mcus_y = (uint16_t)((j.height + mcu_h - 1U) / mcu_h);

    uint8_t y_blk[4][64];
    uint8_t cb_blk[64];
    uint8_t cr_blk[64];
    uint32_t mcu_count = 0;

    for (uint16_t my = 0; my < mcus_y; my++) {
        for (uint16_t mx = 0; mx < mcus_x; mx++) {
            if (j.restart_interval > 0 && mcu_count > 0 && (mcu_count % j.restart_interval) == 0) {
                j.bit_count = 0;
                j.bit_buf = 0;
                j.marker_hit = 0;
                j.comp[0].prev_dc = 0;
                j.comp[1].prev_dc = 0;
                j.comp[2].prev_dc = 0;
            }
            mcu_count++;

            int y_idx = 0;
            for (uint8_t vy = 0; vy < j.comp[0].v_samp; vy++) {
                for (uint8_t hx = 0; hx < j.comp[0].h_samp; hx++) {
                    jpeg_decode_block(&j, 0, y_blk[y_idx++]);
                }
            }
            if (j.num_comp == 3) {
                jpeg_decode_block(&j, 1, cb_blk);
                jpeg_decode_block(&j, 2, cr_blk);
            } else {
                for (int i = 0; i < 64; i++) {
                    cb_blk[i] = 128;
                    cr_blk[i] = 128;
                }
            }

            /* Converte o MCU (ate 16x16) de YCbCr para ARGB32 no buffer de saida */
            for (uint16_t py = 0; py < mcu_h; py++) {
                uint16_t src_y = (uint16_t)(my * mcu_h + py);
                if (src_y >= j.height) continue;
                uint16_t dst_y = (j.height == out_h) ? src_y : (uint16_t)(((uint32_t)src_y * out_h) / j.height);
                if (dst_y >= out_h) continue;

                for (uint16_t px = 0; px < mcu_w; px++) {
                    uint16_t src_x = (uint16_t)(mx * mcu_w + px);
                    if (src_x >= j.width) continue;
                    uint16_t dst_x = (j.width == out_w) ? src_x : (uint16_t)(((uint32_t)src_x * out_w) / j.width);
                    if (dst_x >= out_w) continue;

                    uint8_t by = (uint8_t)(py / 8U);
                    uint8_t bx = (uint8_t)(px / 8U);
                    uint8_t b_idx = (uint8_t)(by * j.comp[0].h_samp + bx);
                    int32_t Y = y_blk[b_idx][(py & 7U) * 8U + (px & 7U)];

                    uint8_t cx = (uint8_t)(px / j.max_h);
                    uint8_t cy = (uint8_t)(py / j.max_v);
                    int32_t Cb = (int32_t)cb_blk[(cy & 7U) * 8U + (cx & 7U)] - 128;
                    int32_t Cr = (int32_t)cr_blk[(cy & 7U) * 8U + (cx & 7U)] - 128;

                    int32_t r = Y + ((359 * Cr + 128) >> 8);
                    int32_t g = Y - ((88 * Cb + 183 * Cr + 128) >> 8);
                    int32_t b = Y + ((454 * Cb + 128) >> 8);

                    uint32_t pixel = 0xFF000000U |
                                     ((uint32_t)jpeg_clamp_u8(r) << 16) |
                                     ((uint32_t)jpeg_clamp_u8(g) << 8) |
                                     (uint32_t)jpeg_clamp_u8(b);
                    argb_out[(size_t)dst_y * out_w + dst_x] = pixel;
                }
            }
        }
    }
    return 0;
}

int liwus_mp4_decode_video_frame(liwus_vcodec_t codec,
                                 const uint8_t *sample_data,
                                 uint32_t sample_size,
                                 uint16_t expected_w,
                                 uint16_t expected_h,
                                 uint32_t *argb_out,
                                 uint16_t out_w,
                                 uint16_t out_h) {
    if (!sample_data || sample_size == 0 || !argb_out || out_w == 0 || out_h == 0) return -1;

    if (codec == LIWUS_VCODEC_MJPEG ||
        (sample_size >= 4 && sample_data[0] == 0xFF && sample_data[1] == 0xD8)) {
        return jpeg_decode_to_argb(sample_data, sample_size, argb_out, out_w, out_h);
    }

    if (codec == LIWUS_VCODEC_RAW_RGB24 && expected_w > 0 && expected_h > 0) {
        uint64_t needed = (uint64_t)expected_w * (uint64_t)expected_h * 3ULL;
        if (sample_size < needed) return -1;
        for (uint16_t y = 0; y < out_h; y++) {
            uint16_t sy = (uint16_t)(((uint32_t)y * expected_h) / out_h);
            for (uint16_t x = 0; x < out_w; x++) {
                uint16_t sx = (uint16_t)(((uint32_t)x * expected_w) / out_w);
                const uint8_t *p = sample_data + ((size_t)sy * expected_w + sx) * 3U;
                argb_out[(size_t)y * out_w + x] = 0xFF000000U |
                                                  ((uint32_t)p[0] << 16) |
                                                  ((uint32_t)p[1] << 8) |
                                                  (uint32_t)p[2];
            }
        }
        return 0;
    }

    return -1;
}

uint32_t liwus_pcm_resample_to_48k_stereo(const int16_t *in_pcm,
                                          uint32_t in_frames,
                                          uint16_t in_channels,
                                          uint32_t in_hz,
                                          int16_t *out_stereo,
                                          uint32_t max_out_frames) {
    if (!in_pcm || in_frames == 0 || in_channels == 0 || in_hz == 0 ||
        !out_stereo || max_out_frames == 0) {
        return 0;
    }

    uint32_t out_frames = (uint32_t)(((uint64_t)in_frames * 48000ULL) / (uint64_t)in_hz);
    if (out_frames == 0) return 0;
    if (out_frames > max_out_frames) out_frames = max_out_frames;

    uint32_t step_fp16 = (uint32_t)(((uint64_t)in_hz << 16) / 48000ULL);
    uint32_t pos_fp16 = 0;

    for (uint32_t i = 0; i < out_frames; i++) {
        uint32_t idx0 = pos_fp16 >> 16;
        uint32_t frac = pos_fp16 & 0xFFFFU;
        if (idx0 >= in_frames) idx0 = in_frames - 1U;
        uint32_t idx1 = (idx0 + 1U < in_frames) ? (idx0 + 1U) : idx0;

        if (in_channels == 1U) {
            int32_t s0 = in_pcm[idx0];
            int32_t s1 = in_pcm[idx1];
            int16_t s = (int16_t)(s0 + (((s1 - s0) * (int32_t)frac) >> 16));
            out_stereo[i * 2U + 0U] = s;
            out_stereo[i * 2U + 1U] = s;
        } else {
            int32_t l0 = in_pcm[idx0 * in_channels + 0U];
            int32_t l1 = in_pcm[idx1 * in_channels + 0U];
            int32_t r0 = in_pcm[idx0 * in_channels + 1U];
            int32_t r1 = in_pcm[idx1 * in_channels + 1U];
            out_stereo[i * 2U + 0U] = (int16_t)(l0 + (((l1 - l0) * (int32_t)frac) >> 16));
            out_stereo[i * 2U + 1U] = (int16_t)(r0 + (((r1 - r0) * (int32_t)frac) >> 16));
        }
        pos_fp16 += step_fp16;
    }

    return out_frames;
}
