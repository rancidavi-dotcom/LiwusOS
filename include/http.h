#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

// Parse URL into components (http:// or https://)
// Returns 0 on success, -1 on failure
int http_parse_url(const char *url, char *host, char *path, uint16_t *port);

// Perform HTTP GET request (legacy, HTTP only)
// Returns bytes received, or -1 on error
int http_get(const char *host, uint16_t port, const char *path, char *response,
             uint32_t max_len);

// Perform HTTP/HTTPS GET request
// use_tls: 0 = HTTP, 1 = HTTPS (TLS)
// Returns bytes received, or -1 on error
int http_get_tls(const char *host, uint16_t port, const char *path, char *response,
                 uint32_t max_len, int use_tls);

// Simplified: GET by full URL (auto-detects http:// or https://)
// Returns bytes received, or -1 on error
int http_get_url(const char *url, char *response, uint32_t max_len);

#endif