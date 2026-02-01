#include "browser.h"
#include "gui.h"
#include "kheap.h"
#include "string.h"
#include "video.h"

widget_t *browser_win = NULL;
static char url_buffer[256] = "liwus://home";
static int url_ptr = 12;
static bool show_results = false;
static char page_content[8192];

// Simple HTML Renderer
static void render_html(int x, int y, const char *html) {
  char line[256];
  int ty = y;
  const char *ptr = html;

  while (*ptr && ty < y + 500) {
    while (*ptr == ' ' || *ptr == '\n' || *ptr == '\r')
      ptr++;
    if (!*ptr)
      break;

    if (strstr(ptr, "<h1>") == ptr) {
      ptr += 4;
      int i = 0;
      while (*ptr && strstr(ptr, "</h1>") != ptr && i < 200)
        line[i++] = *ptr++;
      line[i] = '\0';
      draw_string(x, ty, line, 0x000000);
      ty += 28;
      if (strstr(ptr, "</h1>") == ptr)
        ptr += 5;
    } else if (strstr(ptr, "<p>") == ptr) {
      ptr += 3;
      int i = 0;
      while (*ptr && strstr(ptr, "</p>") != ptr && i < 200)
        line[i++] = *ptr++;
      line[i] = '\0';
      draw_string(x, ty, line, 0x333333);
      ty += 18;
      if (strstr(ptr, "</p>") == ptr)
        ptr += 4;
    } else if (*ptr == '<') {
      while (*ptr && *ptr != '>')
        ptr++;
      if (*ptr)
        ptr++;
    } else {
      int i = 0;
      while (*ptr && *ptr != '<' && *ptr != '\n' && i < 200)
        line[i++] = *ptr++;
      line[i] = '\0';
      if (i > 0) {
        draw_string(x, ty, line, 0x333333);
        ty += 16;
      }
    }
  }
}

// Internal pages
static void load_internal_page(const char *url) {
  if (strstr(url, "liwus://home")) {
    strcpy(page_content, "<h1>LiwusOS Browser</h1>"
                         "<p>Bem-vindo ao navegador do LiwusOS!</p>"
                         "<p>Paginas internas disponiveis:</p>"
                         "<p>* liwus://home - Esta pagina</p>"
                         "<p>* liwus://about - Sobre o sistema</p>"
                         "<p>* liwus://test - Pagina de teste</p>");
  } else if (strstr(url, "liwus://about")) {
    strcpy(page_content, "<h1>Sobre o LiwusOS</h1>"
                         "<p>LiwusOS - Sistema Operacional Brasileiro</p>"
                         "<p>Compositor: LGX Wayland</p>"
                         "<p>Versao: 1.0 Alpha</p>");
  } else if (strstr(url, "liwus://test")) {
    strcpy(page_content, "<h1>Pagina de Teste</h1>"
                         "<p>O navegador esta funcionando!</p>"
                         "<p>Parser HTML basico operacional.</p>");
  } else {
    strcpy(page_content, "<h1>Erro 404</h1>"
                         "<p>Pagina nao encontrada.</p>"
                         "<p>Use liwus://home para voltar.</p>");
  }
  show_results = true;
}

static void on_go_click(void *widget, void *surface) {
  (void)widget;
  (void)surface;

  // For now, only support internal pages to avoid crashes
  if (strstr(url_buffer, "liwus://") == url_buffer) {
    load_internal_page(url_buffer);
  } else {
    strcpy(page_content,
           "<h1>Rede Desabilitada</h1>"
           "<p>Por enquanto, apenas paginas internas funcionam.</p>"
           "<p>Use: liwus://home, liwus://about, liwus://test</p>");
    show_results = true;
  }
}

widget_t *init_browser() {
  browser_win = create_window("LiwusOS Browser", 50, 40, 800, 550);
  browser_win->visible = false;

  // Go Button
  add_widget(browser_win, create_button("IR", 700, 8, 80, 26, on_go_click));

  return browser_win;
}

extern char get_last_key();

void update_browser() {
  if (!browser_win || !browser_win->visible)
    return;

  char k = get_last_key();
  if (k > 0) {
    if (k == '\n') {
      on_go_click(NULL, NULL);
    } else if (k == '\b') {
      if (url_ptr > 0)
        url_buffer[--url_ptr] = '\0';
    } else if (k >= 32 && k < 127 && url_ptr < 250) {
      url_buffer[url_ptr++] = k;
      url_buffer[url_ptr] = '\0';
    }
  }
}

void draw_browser_content() {
  if (!browser_win || !browser_win->visible)
    return;

  int bx = browser_win->x;
  int by = browser_win->y;

  // URL Bar
  draw_rect(bx + 10, by + 45, 680, 26, 0xFFFFFF);
  draw_string(bx + 15, by + 49, url_buffer, 0x000000);

  // Content area
  int cx = bx + 10;
  int cy = by + 80;
  draw_rect(cx, cy, 780, 450, 0xFAFAFA);

  if (!show_results) {
    draw_string(cx + 300, cy + 200, "LiwusOS Browser", 0x00AA00);
    draw_string(cx + 240, cy + 230, "Digite uma URL e pressione ENTER",
                0x666666);
  } else {
    render_html(cx + 20, cy + 20, page_content);
  }
}

void open_browser() {
  if (!browser_win)
    init_browser();
  browser_win->visible = true;
  browser_win->is_focused = true;
  show_results = false;
  load_internal_page("liwus://home");
}