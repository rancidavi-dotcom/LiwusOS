#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "gui/browser_url.h"

static void expect(const char *input, const char *wanted) {
    char out[256];
    browser_url_normalize_input(input, out, sizeof(out));
    if (strcmp(out, wanted) != 0) {
        fprintf(stderr, "browser URL normalize: '%s' => '%s', expected '%s'\n",
                input, out, wanted);
        assert(0);
    }
}

int main(void) {
    expect("github.com", "https://github.com");
    expect("www.github.com/org/repo", "https://www.github.com/org/repo");
    expect("192.168.0.1/path", "https://192.168.0.1/path");
    expect("https://github.com", "https://github.com");
    expect("http://localhost:8080", "http://localhost:8080");
    expect("  example.org/path  ", "https://example.org/path");
    expect("github status", "https://www.bing.com/search?q=github+status");
    expect("wiki", "https://pt.wikipedia.org/");
    expect("wiki: liwus os", "https://pt.wikipedia.org/w/index.php?search=liwus+os");
    expect("/local/path", "/local/path");

    char tiny[8] = "x";
    browser_url_normalize_input("github.com", tiny, sizeof(tiny));
    assert(tiny[0] == '\0');
    puts("browser URL normalization: hosts, HTTPS, searches, and wiki shortcuts pass");
    return 0;
}
