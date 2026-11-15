#ifndef LIWUS_BROWSER_URL_H
#define LIWUS_BROWSER_URL_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    BROWSER_SEARCH_GOOGLE = 0,
    BROWSER_SEARCH_DUCKDUCKGO = 1,
    BROWSER_SEARCH_BING = 2,
    BROWSER_SEARCH_WIKIPEDIA = 3,
    BROWSER_SEARCH_COUNT
} browser_search_engine_t;

/* Get / set active search engine */
void browser_url_set_search_engine(browser_search_engine_t engine);
browser_search_engine_t browser_url_get_search_engine(void);
const char *browser_url_get_search_engine_name(browser_search_engine_t engine);
const char *browser_url_get_search_engine_home(browser_search_engine_t engine);
const char *browser_url_get_search_engine_prefix(browser_search_engine_t engine);

/* Build a search URL for a given engine and query */
bool browser_url_build_search(browser_search_engine_t engine, const char *query,
                              size_t query_len, char *out, size_t out_sz);

/* Normalize the browser address/search field into an absolute URL. */
void browser_url_normalize_input(const char *raw, char *out, size_t out_sz);

#endif
