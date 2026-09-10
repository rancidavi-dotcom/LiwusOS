#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

// Parse URL into components
// Returns 0 on success, -1 on failure
int http_parse_url(const char *url, char *host, char *path, uint16_t *port);

// Perform HTTP GET request
// Returns bytes received, or -1 on error
int http_get(const char *host, uint16_t port, const char *path, char *response,
             uint32_t max_len);

// Simplified: GET by full URL
int http_get_url(const char *url, char *response, uint32_t max_len);

#endif
