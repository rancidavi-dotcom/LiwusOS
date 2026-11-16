#include "net/liwustube.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_extract_video_id(void) {
    char id[LIWUSTUBE_MAX_ID];

    /* URL clássica do YouTube */
    assert(liwustube_extract_video_id("https://www.youtube.com/watch?v=dQw4w9WgXcQ", id, sizeof(id)) == 0);
    assert(strcmp(id, "dQw4w9WgXcQ") == 0);

    /* URL com parâmetros adicionais */
    assert(liwustube_extract_video_id("https://www.youtube.com/watch?v=dQw4w9WgXcQ&t=42s", id, sizeof(id)) == 0);
    assert(strcmp(id, "dQw4w9WgXcQ") == 0);

    /* youtu.be curto */
    assert(liwustube_extract_video_id("https://youtu.be/dQw4w9WgXcQ", id, sizeof(id)) == 0);
    assert(strcmp(id, "dQw4w9WgXcQ") == 0);

    /* Embed URL */
    assert(liwustube_extract_video_id("https://www.youtube.com/embed/dQw4w9WgXcQ", id, sizeof(id)) == 0);
    assert(strcmp(id, "dQw4w9WgXcQ") == 0);

    /* Piped /watch */
    assert(liwustube_extract_video_id("/watch?v=6X_OEUFV0v4", id, sizeof(id)) == 0);
    assert(strcmp(id, "6X_OEUFV0v4") == 0);

    /* ID direto puro */
    assert(liwustube_extract_video_id("dQw4w9WgXcQ", id, sizeof(id)) == 0);
    assert(strcmp(id, "dQw4w9WgXcQ") == 0);

    printf("liwustube extract_video_id: PASS\n");
}

static void test_url_builders(void) {
    char url[LIWUSTUBE_MAX_URL];

    assert(liwustube_build_search_url("https://api.piped.private.coffee", "piano jazz", url, sizeof(url)) == 0);
    assert(strstr(url, "/search?q=piano+jazz&filter=videos") != NULL);

    assert(liwustube_build_stream_url("https://api.piped.private.coffee", "dQw4w9WgXcQ", url, sizeof(url)) == 0);
    assert(strstr(url, "/streams/dQw4w9WgXcQ") != NULL);

    char dur[16];
    liwustube_format_duration(213, dur, sizeof(dur));
    assert(strcmp(dur, "03:33") == 0);

    liwustube_format_duration(3665, dur, sizeof(dur));
    assert(strcmp(dur, "01:01:05") == 0);

    /* Teste de API configurável e Bridge */
    liwustube_set_api("http://192.168.1.100:8085/");
    assert(strcmp(liwustube_get_api(), "http://192.168.1.100:8085") == 0);
    assert(liwustube_build_search_url(NULL, "jazz", url, sizeof(url)) == 0);
    assert(strstr(url, "http://192.168.1.100:8085/search?q=jazz") != NULL);
    liwustube_set_api(NULL);
    assert(strcmp(liwustube_get_api(), LIWUSTUBE_DEFAULT_PIPED_API) == 0);

    printf("liwustube url_builders and duration: PASS\n");
}

static void test_parse_search_json(void) {
    static const char sample_search[] =
        "{\"items\":["
        "{\"url\":\"/watch?v=6X_OEUFV0v4\",\"title\":\"Piano Session Relax\",\"uploaderName\":\"Giuseppe Centonze\",\"duration\":7200,\"views\":123456},"
        "{\"url\":\"/watch?v=ssN7_u08HFY\",\"title\":\"Most Beautiful Pieces\",\"uploaderName\":\"Clavier\",\"duration\":300,\"views\":5000000}"
        "]}";

    liwustube_search_result_t res;
    assert(liwustube_parse_search_json(sample_search, strlen(sample_search), &res) == 0);
    assert(res.count == 2);
    assert(strcmp(res.items[0].id, "6X_OEUFV0v4") == 0);
    assert(strcmp(res.items[0].title, "Piano Session Relax") == 0);
    assert(strcmp(res.items[0].author, "Giuseppe Centonze") == 0);
    assert(res.items[0].duration_sec == 7200);
    assert(res.items[0].views == 123456);

    assert(strcmp(res.items[1].id, "ssN7_u08HFY") == 0);
    assert(strcmp(res.items[1].title, "Most Beautiful Pieces") == 0);

    printf("liwustube parse_search_json: PASS\n");
}

static void test_parse_stream_json(void) {
    static const char sample_stream[] =
        "{\"title\":\"Rick Astley - Never Gonna Give You Up\",\"uploader\":\"Rick Astley\",\"duration\":213,"
        "\"videoStreams\":["
        "{\"quality\":\"1080p\",\"format\":\"MPEG_4\",\"codec\":null,\"videoOnly\":true,\"url\":\"https://example.com/1080p.mp4\"},"
        "{\"quality\":\"360p\",\"format\":\"MPEG_4\",\"codec\":null,\"audioTrackId\":null,\"videoOnly\":false,\"url\":\"https://example.com/360p_prog.mp4\","
        "\"width\":640,\"height\":360,\"contentLength\":11829048}"
        "]}";

    liwustube_stream_info_t info;
    assert(liwustube_parse_stream_json(sample_stream, strlen(sample_stream), &info) == 0);
    assert(strcmp(info.title, "Rick Astley - Never Gonna Give You Up") == 0);
    assert(strcmp(info.author, "Rick Astley") == 0);
    assert(info.duration_sec == 213);
    assert(strcmp(info.quality, "360p") == 0);
    assert(strcmp(info.stream_url, "https://example.com/360p_prog.mp4") == 0);
    assert(info.width == 640);
    assert(info.height == 360);
    assert(info.content_length == 11829048);

    /* Teste com null values e URLs longas (> 1000 chars) como no Piped */
    char long_stream[4096];
    char long_url[1200];
    memset(long_url, 'a', sizeof(long_url) - 1);
    long_url[sizeof(long_url) - 1] = '\0';
    snprintf(long_stream, sizeof(long_stream),
             "{\"title\":\"Long URL Test\",\"uploader\":\"Test\",\"duration\":100,"
             "\"videoStreams\":[{\"url\":\"https://example.com/%s.mp4\",\"quality\":\"360p\",\"format\":\"video/mp4\",\"codec\":null,\"videoOnly\":false}]}",
             long_url);

    assert(liwustube_parse_stream_json(long_stream, strlen(long_stream), &info) == 0);
    assert(strstr(info.stream_url, "https://example.com/") != NULL);
    assert(strlen(info.stream_url) > 1000);

    printf("liwustube parse_stream_json: PASS\n");
}

int main(void) {
    test_extract_video_id();
    test_url_builders();
    test_parse_search_json();
    test_parse_stream_json();
    printf("ALL LIWUSTUBE TESTS PASSED!\n");
    return 0;
}
