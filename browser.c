#include "browser.h"
#include "video.h"
#include "string.h"
#include "kheap.h"
#include "net.h"

widget_t* browser_win;
static widget_t* url_label;
static char url_buffer[256] = "youtube.com";
static int url_ptr = 11; 
static bool show_results = false;
static char* raw_html = "";

void liwus_render_html(int x, int y, const char* html) {
    char temp_line[256];
    int ty = y;
    const char* ptr = html;
    while(*ptr) {
        if (strstr(ptr, "<h1>") == ptr) {
            ptr += 4;
            int i = 0; while(*ptr && strstr(ptr, "</h1>") != ptr) temp_line[i++] = *ptr++;
            temp_line[i] = '\0';
            draw_string(x, ty, temp_line, 0xFF0000);
            ty += 35; ptr += 5;
        } else if (strstr(ptr, "<button>") == ptr) {
            ptr += 8;
            int i = 0; while(*ptr && strstr(ptr, "</button>") != ptr) temp_line[i++] = *ptr++;
            temp_line[i] = '\0';
            draw_button_visual(x, ty, 200, 30, temp_line, 0x555555);
            ty += 40; ptr += 9;
        } else if (strstr(ptr, "<p>") == ptr) {
            ptr += 3;
            int i = 0; while(*ptr && strstr(ptr, "</p>") != ptr) temp_line[i++] = *ptr++;
            temp_line[i] = '\0';
            draw_string(x, ty, temp_line, 0x333333);
            ty += 20; ptr += 4;
        } else ptr++;
    }
}

void on_go_click(widget_t* self) {
    (void)self;
    show_results = true;
    if (strstr(url_buffer, "youtube.com")) {
        raw_html = "<h1>YouTube</h1><p>LiwusOS: Rede Real Detectada!</p><button>PLAY VIDEO</button>";
    } else raw_html = "<h1>Liwus Search</h1><p>Resultado da busca real...</p>";
}

widget_t* init_browser() {
    browser_win = create_window("Liwus Web Browser", 50, 50, 1024, 700);
    browser_win->visible = false;

    // Elementos da Interface (posicionados no topo da janela)
    add_widget(browser_win, create_label("Web:", 15, 15, 0x000000));
    
    // Campo de URL (Botao clicavel)
    widget_t* url_bg = create_button("", 60, 10, 820, 30, (void*)0);
    url_bg->color = 0xFFFFFF;
    add_widget(browser_win, url_bg);

    url_label = create_label(url_buffer, 70, 18, 0x000000);
    add_widget(browser_win, url_label);
    
    add_widget(browser_win, create_button("BUSCAR", 890, 10, 120, 30, on_go_click));

    return browser_win;
}

extern char get_last_key();
void update_browser() {
    if (!browser_win->visible || !browser_win->focused) return;
    char k = get_last_key();
    if (k > 0) {
        if (k == '\n') on_go_click(NULL);
        else if (k == '\b') { if (url_ptr > 0) url_buffer[--url_ptr] = '\0'; }
        else if (url_ptr < 250) { url_buffer[url_ptr++] = k; url_buffer[url_ptr] = '\0'; }
        url_label->text = url_buffer;
    }
}

void draw_browser_content() {
    if (!browser_win->visible) return;
    int cx = browser_win->x + 10;
    int cy = browser_win->y + 90; // Área de conteúdo começa abaixo da barra, com margem de segurança

    if (!show_results) {
        draw_rect(cx, cy, 1004, 600, 0x000000); // Fundo Preto Home
        draw_string(cx + 420, cy + 250, "LiwusOS", 0x00FF00);
        return;
    }
    draw_rect(cx, cy, 1004, 600, 0xFFFFFF); // Viewport Branca
    liwus_render_html(cx + 40, cy + 40, raw_html);
}

void open_browser() {
    browser_win->visible = true;
    browser_win->focused = true;
    show_results = false;
}