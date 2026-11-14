#ifndef LIWUS_BROWSER_URL_H
#define LIWUS_BROWSER_URL_H

#include <stddef.h>

/* Normalize the browser address/search field into an absolute URL. */
void browser_url_normalize_input(const char *raw, char *out, size_t out_sz);

#endif
