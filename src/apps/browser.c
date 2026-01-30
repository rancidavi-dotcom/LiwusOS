#include "browser.h"
#include "http.h"
#include "kheap.h"
#include "string.h"
#include <stdbool.h>

// Basic URL cleaner
static void clean_input(char *str) {
  int len = strlen(str);
  while (len > 0 && (str[len - 1] == ' ' || str[len - 1] == '\r' ||
                     str[len - 1] == '\n')) {
    str[--len] = '\0';
  }
}

// Convert HTML to Console Text
static void html_to_text(const char *html, char *output, uint32_t max_len) {
  uint32_t out_ptr = 0;
  const char *ptr = html;
  bool in_tag = false;

  while (*ptr && out_ptr < max_len - 1) {
    if (*ptr == '<') {
      // Check for specific tags to add formatting
      if (strstr(ptr, "<h1>") == ptr) {
        if (out_ptr + 10 < max_len) {
          strcat(output, "\n[TITULO] ");
          out_ptr += 10;
        }
        ptr += 4;
        continue;
      } else if (strstr(ptr, "</h1>") == ptr) {
        if (out_ptr + 1 < max_len) {
          output[out_ptr++] = '\n';
          output[out_ptr] = '\0';
        }
        ptr += 5;
        continue;
      } else if (strstr(ptr, "<li>") == ptr) {
        if (out_ptr + 3 < max_len) {
          strcat(output, "  * ");
          out_ptr += 4;
        }
        ptr += 4;
        continue;
      } else if (strstr(ptr, "<p>") == ptr) {
        if (out_ptr + 1 < max_len) {
          output[out_ptr++] = '\n';
          output[out_ptr] = '\0';
        }
        ptr += 3;
        continue;
      }
      in_tag = true;
    } else if (*ptr == '>') {
      in_tag = false;
      ptr++;
      continue;
    }

    if (!in_tag) {
      output[out_ptr++] = *ptr;
      output[out_ptr] = '\0';
    }
    ptr++;
  }
}

void browser_cli_execute(const char *input, char *output, uint32_t max_len) {
  char url_buffer[256];
  strncpy(url_buffer, input, 255);
  clean_input(url_buffer);

  if (strlen(url_buffer) == 0) {
    strcpy(output, "Erro: Digite uma URL ou termos de busca.\n");
    return;
  }

  // Handle internal pages manually
  if (strcmp(url_buffer, "liwus://home") == 0) {
    strcpy(output, "\n[TITULO] LiwusOS Home\n\nEste e o navegador CLI do "
                   "LiwusOS.\nUse 'browser <busca>' para navegar.\n");
    return;
  }

  uint32_t content_size = 32768;
  char *raw_content = (char *)kmalloc(content_size);
  memset(raw_content, 0, content_size);

  char final_url[512];
  bool is_search =
      (strchr(url_buffer, '.') == NULL && strstr(url_buffer, "://") == NULL);

  if (is_search) {
    strcpy(final_url, "https://api.duckduckgo.com/?q=");
    strcat(final_url, url_buffer);
    strcat(final_url, "&format=xml&no_html=1");
  } else {
    if (strstr(url_buffer, "://") == NULL) {
      strcpy(final_url, "http://");
      strcat(final_url, url_buffer);
    } else {
      strcpy(final_url, url_buffer);
    }
  }

  int res = http_get_url(final_url, raw_content, content_size);

  if (res <= 0) {
    strcpy(output, "Erro de Conexao: Nao foi possivel acessar a rede.\n");
  } else {
    output[0] = '\0';
    html_to_text(raw_content, output, max_len);
  }

  kfree(raw_content);
}