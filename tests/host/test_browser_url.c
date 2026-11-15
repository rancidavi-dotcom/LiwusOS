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
    /* 1. Verificação com o mecanismo padrão inicial (Bing) */
    assert(browser_url_get_search_engine() == BROWSER_SEARCH_BING);
    expect("github.com", "https://github.com");
    expect("www.github.com/org/repo", "https://www.github.com/org/repo");
    expect("192.168.0.1/path", "https://192.168.0.1/path");
    expect("https://github.com", "https://github.com");
    expect("http://localhost:8080", "http://localhost:8080");
    expect("  example.org/path  ", "https://example.org/path");

    /* Pesquisa simples no motor padrão (Bing) */
    expect("github status", "https://www.bing.com/search?q=github+status");

    /* 2. Troca dinâmica de mecanismos prontos */
    browser_url_set_search_engine(BROWSER_SEARCH_GOOGLE);
    assert(browser_url_get_search_engine() == BROWSER_SEARCH_GOOGLE);
    assert(strcmp(browser_url_get_search_engine_name(BROWSER_SEARCH_GOOGLE), "Google") == 0);
    expect("github status", "https://www.google.com/search?q=github+status");

    browser_url_set_search_engine(BROWSER_SEARCH_DUCKDUCKGO);
    assert(browser_url_get_search_engine() == BROWSER_SEARCH_DUCKDUCKGO);
    assert(strcmp(browser_url_get_search_engine_name(BROWSER_SEARCH_DUCKDUCKGO), "DuckDuckGo") == 0);
    expect("github status", "https://duckduckgo.com/?q=github+status");

    browser_url_set_search_engine(BROWSER_SEARCH_WIKIPEDIA);
    assert(browser_url_get_search_engine() == BROWSER_SEARCH_WIKIPEDIA);
    assert(strcmp(browser_url_get_search_engine_name(BROWSER_SEARCH_WIKIPEDIA), "Wikipedia") == 0);
    expect("github status", "https://pt.wikipedia.org/w/index.php?search=github+status");

    /* 3. Atalhos de prefixo funcionam independentemente do motor ativo */
    browser_url_set_search_engine(BROWSER_SEARCH_GOOGLE);
    expect("g: liwus os", "https://www.google.com/search?q=liwus+os");
    expect("google: liwus os", "https://www.google.com/search?q=liwus+os");
    expect("ddg: liwus os", "https://duckduckgo.com/?q=liwus+os");
    expect("b: liwus os", "https://www.bing.com/search?q=liwus+os");
    expect("wiki: liwus os", "https://pt.wikipedia.org/w/index.php?search=liwus+os");
    expect("w: liwus os", "https://pt.wikipedia.org/w/index.php?search=liwus+os");

    /* 4. Navegação para portais de busca por palavra-chave direta */
    expect("google", "https://www.google.com");
    expect("ddg", "https://duckduckgo.com");
    expect("duckduckgo", "https://duckduckgo.com");
    expect("bing", "https://www.bing.com");
    expect("wiki", "https://pt.wikipedia.org/");
    expect("wikipedia", "https://pt.wikipedia.org/");

    expect("/local/path", "/local/path");

    char tiny[8] = "x";
    browser_url_normalize_input("github.com", tiny, sizeof(tiny));
    assert(tiny[0] == '\0');
    puts("browser URL normalization: engines (Google, DuckDuckGo, Bing, Wikipedia) pass");
    return 0;
}
