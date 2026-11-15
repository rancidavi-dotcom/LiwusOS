#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "kernel/media.h"

int main(void) {
    liwus_media_info_t info;
    static const uint8_t id3[] = {'I', 'D', '3', 4, 0, 0};
    static const uint8_t mp3[] = {0xFF, 0xFB, 0x90, 0x64};
    static const uint8_t mp4[] = {0, 0, 0, 0, 'f', 't', 'y', 'p', 'i', 's', 'o', 'm'};
    static const uint8_t wav[] = {
        'R','I','F','F', 36,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
        0x80,0xBB,0,0, 0,0,0,0, 4,0, 16,0
    };
    assert(liwus_media_probe(id3, sizeof(id3), &info) == 0 && info.kind == LIWUS_MEDIA_MP3);
    assert(liwus_media_probe(mp3, sizeof(mp3), &info) == 0 && info.kind == LIWUS_MEDIA_MP3);
    assert(liwus_media_probe(mp4, sizeof(mp4), &info) == 0 && info.kind == LIWUS_MEDIA_MP4);
    assert(liwus_media_probe(wav, sizeof(wav), &info) == 0 && info.kind == LIWUS_MEDIA_WAV);

    /* Testa o demuxer ISO-BMFF e o decodificador MJPEG no video real assets/video.mp4 */
    FILE *f = fopen("assets/video.mp4", "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        assert(sz > 1024);
        uint8_t *buf = (uint8_t *)malloc((size_t)sz);
        assert(buf != NULL);
        assert(fread(buf, 1, (size_t)sz, f) == (size_t)sz);
        fclose(f);

        assert(liwus_media_probe(buf, (size_t)sz, &info) == 0 && info.kind == LIWUS_MEDIA_MP4);

        liwus_mp4_index_t idx;
        assert(liwus_mp4_parse(buf, (size_t)sz, &idx) == 0);
        assert(idx.has_video == 1);
        assert(idx.has_audio == 1);
        assert(idx.video_codec == LIWUS_VCODEC_MJPEG);
        assert(idx.audio_codec == LIWUS_ACODEC_MP3);
        assert(idx.width == 240 && idx.height == 136);
        assert(idx.video_sample_count >= 60);
        assert(idx.audio_sample_count > 0);
        assert(idx.duration_ms >= 5000);

        const uint8_t *v_ptr = NULL;
        uint32_t v_sz = 0, v_pts = 0;
        assert(liwus_mp4_get_video_sample(&idx, buf, (size_t)sz, 0, &v_ptr, &v_sz, &v_pts) == 0);
        assert(v_ptr != NULL && v_sz > 64 && v_pts == 0);

        uint32_t *argb = (uint32_t *)calloc(240 * 136, sizeof(uint32_t));
        assert(argb != NULL);
        assert(liwus_mp4_decode_video_frame(idx.video_codec, v_ptr, v_sz,
                                            idx.width, idx.height, argb, 240, 136) == 0);
        unsigned non_black = 0;
        for (int i = 0; i < 240 * 136; i++) {
            if ((argb[i] & 0x00FFFFFFU) != 0) non_black++;
        }
        assert(non_black > 1000);

        const uint8_t *a_ptr = NULL;
        uint32_t a_sz = 0, a_pts = 0;
        assert(liwus_mp4_get_audio_sample(&idx, buf, (size_t)sz, 0, &a_ptr, &a_sz, &a_pts) == 0);
        assert(a_ptr != NULL && a_sz > 32);

        free(argb);
        free(buf);
    }

    puts("media probe & MP4 demux/MJPEG decode: PASS");
    return 0;
}
