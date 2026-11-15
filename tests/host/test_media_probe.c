#include <assert.h>
#include <stdint.h>
#include <stdio.h>
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
    puts("media probe: PASS");
    return 0;
}
