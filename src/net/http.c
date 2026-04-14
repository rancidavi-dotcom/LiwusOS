#include "http.h"
#include "kheap.h"
#include "netstack.h"
#include "serial.h"
#include "string.h"
#include "tcp.h"
#include "video.h"

static void http_log(const char *msg) { serial_print(msg); }

static void http_log_u32(uint32_t value) {
  char num[16];
  itoa((int)value, num, 10);
  serial_print(num);
}

static void http_log_ip(uint32_t ip) {
  char num[16];

  itoa((int)(ip & 0xFF), num, 10);
  serial_print(num);
  serial_print(".");
  itoa((int)((ip >> 8) & 0xFF), num, 10);
  serial_print(num);
  serial_print(".");
  itoa((int)((ip >> 16) & 0xFF), num, 10);
  serial_print(num);
  serial_print(".");
  itoa((int)((ip >> 24) & 0xFF), num, 10);
  serial_print(num);
}

// Parse URL: http://host[:port]/path
int http_parse_url(const char *url, char *host, char *path, uint16_t *port) {
  http_log("[http] parse_url: ");
  http_log(url);
  http_log("\n");
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

  http_log("[http] host=");
  http_log(host);
  http_log(" port=");
  http_log_u32(*port);
  http_log(" path=");
  http_log(path);
  http_log("\n");

  return 0;
}

// Build and send HTTP GET request
int http_get(const char *host, uint16_t port, const char *path, char *response,
             uint32_t max_len) {
  // Resolve host to IP
  extern uint32_t net_resolve_host(const char *host);
  uint32_t ip = net_resolve_host(host);

  http_log("[http] resolve ");
  http_log(host);
  http_log(" -> ");
  http_log_ip(ip);
  http_log("\n");

  if (ip == 0) {
    http_log("[http] resolve failed\n");
    return -1;
  }

  // Connect via TCP
  http_log("[http] tcp_connect begin\n");
  tcp_socket_t *sock = tcp_connect(ip, port);
  if (!sock) {
    http_log("[http] tcp_connect failed\n");
    return -1;
  }
  http_log("[http] tcp_connect ok\n");

  // Build HTTP request
  char request[512];
  strcpy(request, "GET ");
  strcat(request, path);
  strcat(request, " HTTP/1.0\r\nHost: ");
  strcat(request, host);
  strcat(request, "\r\nConnection: close\r\n\r\n");

  http_log("[http] request bytes=");
  http_log_u32((uint32_t)strlen(request));
  http_log("\n");

  // Send request
  tcp_send(sock, (const uint8_t *)request, strlen(request));
  http_log("[http] request sent\n");

  // Receive response
  uint32_t total = 0;
  int received;
  uint8_t *chunk = (uint8_t *)kmalloc(1024);

  while ((received = tcp_receive(sock, chunk, 1023)) > 0) {
    http_log("[http] recv chunk=");
    http_log_u32((uint32_t)received);
    http_log("\n");
    if (total + received >= max_len - 1) {
      received = max_len - 1 - total;
    }
    memcpy(response + total, chunk, received);
    total += received;

    if (total >= max_len - 1)
      break;
  }
  response[total] = '\0';
  kfree(chunk);

  // Close connection
  http_log("[http] total bytes=");
  http_log_u32(total);
  http_log("\n");
  tcp_close(sock);
  http_log("[http] tcp_close sent\n");

  // Skip HTTP headers (find \r\n\r\n)
  char *body = strstr(response, "\r\n\r\n");
  if (body) {
    body += 4;
    // Move body to start of buffer
    int body_len = strlen(body);
    memmove(response, body, body_len + 1);
    http_log("[http] body bytes=");
    http_log_u32((uint32_t)body_len);
    http_log("\n");
    return body_len;
  }

  http_log("[http] header separator not found\n");

  return total;
}

// Simplified URL-based GET
int http_get_url(const char *url, char *response, uint32_t max_len) {
  char host[128];
  char path[256];
  uint16_t port;

  if (http_parse_url(url, host, path, &port) < 0) {
    http_log("[http] parse_url failed\n");
    return -1;
  }

  return http_get(host, port, path, response, max_len);
}
