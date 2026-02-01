#include "http.h"
#include "kheap.h"
#include "netstack.h"
#include "string.h"
#include "tcp.h"
#include "video.h"

// Parse URL: http://host[:port]/path
int http_parse_url(const char *url, char *host, char *path, uint16_t *port) {
  *port = 80; // Default HTTP port

  // Skip "http://" if present
  if (strstr(url, "http://") == url) {
    url += 7;
  } else if (strstr(url, "https://") == url) {
    // HTTPS not supported
    return -1;
  }

  // Find end of host (either ':', '/', or end of string)
  const char *ptr = url;
  int host_len = 0;

  while (*ptr && *ptr != ':' && *ptr != '/') {
    host[host_len++] = *ptr++;
  }
  host[host_len] = '\0';

  // Check for port
  if (*ptr == ':') {
    ptr++;
    *port = 0;
    while (*ptr >= '0' && *ptr <= '9') {
      *port = (*port * 10) + (*ptr - '0');
      ptr++;
    }
  }

  // Copy path (or default to "/")
  if (*ptr == '/') {
    strcpy(path, ptr);
  } else {
    strcpy(path, "/");
  }

  return 0;
}

// Build and send HTTP GET request
int http_get(const char *host, uint16_t port, const char *path, char *response,
             uint32_t max_len) {
  // Resolve host to IP
  extern uint32_t net_resolve_host(const char *host);
  uint32_t ip = net_resolve_host(host);

  if (ip == 0) {
    draw_string(10, 80, "DNS Resolve Failed", 0xFF0000);
    return -1;
  }

  // Connect via TCP
  tcp_socket_t *sock = tcp_connect(ip, port);
  if (!sock) {
    draw_string(10, 80, "TCP Connect Failed", 0xFF0000);
    return -1;
  }

  // Build HTTP request
  char request[512];
  strcpy(request, "GET ");
  strcat(request, path);
  strcat(request, " HTTP/1.0\r\nHost: ");
  strcat(request, host);
  strcat(request, "\r\nConnection: close\r\n\r\n");

  // Send request
  tcp_send(sock, (const uint8_t *)request, strlen(request));

  // Receive response
  uint32_t total = 0;
  int received;
  char chunk[1024];

  while ((received = tcp_receive(sock, (uint8_t *)chunk, sizeof(chunk) - 1)) >
         0) {
    if (total + received >= max_len - 1) {
      received = max_len - 1 - total;
    }
    memcpy(response + total, chunk, received);
    total += received;

    if (total >= max_len - 1)
      break;
  }
  response[total] = '\0';

  // Close connection
  tcp_close(sock);

  // Skip HTTP headers (find \r\n\r\n)
  char *body = strstr(response, "\r\n\r\n");
  if (body) {
    body += 4;
    // Move body to start of buffer
    int body_len = strlen(body);
    memmove(response, body, body_len + 1);
    return body_len;
  }

  return total;
}

// Simplified URL-based GET
int http_get_url(const char *url, char *response, uint32_t max_len) {
  char host[128];
  char path[256];
  uint16_t port;

  if (http_parse_url(url, host, path, &port) < 0) {
    return -1;
  }

  return http_get(host, port, path, response, max_len);
}
