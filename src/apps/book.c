#include "compositor.h"
#include "kheap.h"
#include "mouse.h" // Para interacao direta no loop do compositor (idealmente via eventos)
#include "string.h"
#include "video.h"

static wl_surface_t *book_surface = NULL;
static wl_buffer_t book_buffer;
static int current_page = 0;

static const char *pages[] = {
    "Bem-vindo ao LiwusOS Wayland!\n\nAgora rodando sobre o Compositor "
    "LGX.\nJanelas sao superficies independentes.\n\nClique [Prox] para ver "
    "mais.",
    "Arquitetura Nova:\n\n- Compositor: Gerencia janelas\n- LGX: Renderiza com "
    "Hardware (fake)\n- Clients: Desenham em buffers",
    "Comandos do Terminal:\n\n- liwfetch: Info do sistema\n- help: Ajuda\n- "
    "reboot: Reiniciar",
    "Fim do Guia.\n\nAproveite sua experiencia no novo LiwusOS!\n\n(c) 2026 "
    "Davi VilasBoas"};

void book_redraw() {
  if (!book_surface)
    return;

  video_set_target(book_buffer.pixels, book_buffer.width, book_buffer.height);

  // Fundo e Borda estilo Janela
  draw_rect(0, 0, book_buffer.width, book_buffer.height, 0xF0F0F0);
  draw_rect(0, 0, book_buffer.width, 25, 0x4A90E2); // Titlebar Blue
  draw_string(10, 8, "Guia de Inicio", 0xFFFFFF);

  // Conteúdo
  int x = 20, y = 50;
  const char *str = pages[current_page];
  while (*str) {
    if (*str == '\n') {
      x = 20;
      y += 16;
    } else {
      draw_char(x, y, *str, 0x333333);
      x += 8;
    }
    str++;
  }

  // Botões (Desenhados manualmente, sem widgets)
  draw_rect(20, 250, 80, 30, 0xDDDDDD); // Prev
  draw_string(30, 258, "< Ant", 0x000000);

  draw_rect(300, 250, 80, 30, 0xDDDDDD); // Next
  draw_string(310, 258, "Prox >", 0x000000);

  video_reset_target();
  wl_commit(book_surface);
}

// Handler de clique simples chamado pelo kernel se o foco for essa janela
void book_click_handler(int rx, int ry) {
  // rx, ry são relativos à janela

  // Botão Prev (20, 250, 80, 30)
  if (rx >= 20 && rx <= 100 && ry >= 250 && ry <= 280) {
    if (current_page > 0) {
      current_page--;
      book_redraw();
    }
  }

  // Botão Next (300, 250, 80, 30)
  if (rx >= 300 && rx <= 380 && ry >= 250 && ry <= 280) {
    if (current_page < 3) {
      current_page++;
      book_redraw();
    }
  }
}

void init_book_app() {
  book_buffer.width = 400;
  book_buffer.height = 300;
  book_buffer.pixels = (uint32_t *)kmalloc(400 * 300 * 4);
  book_buffer.shm = true;

  book_surface = wl_create_surface(400, 300, WL_SURFACE_TOPLEVEL);
  book_surface->x = 200; // Centro aproximado
  book_surface->y = 100;

  // Hack: Armazenar ponteiro de função para callback na estrutura da surface?
  // Por enquanto, o compositor não suporta callbacks genéricos de app.
  // Vamos ter que exportar a função e o kernel chuta pro app certo.

  wl_attach_buffer(book_surface, &book_buffer);
  book_redraw();
}

// Wrapper global para acessar a surface
wl_surface_t *get_book_surface() { return book_surface; }

void open_book() {
  if (!book_surface)
    init_book_app();
  book_surface->visible = true;
  book_surface->is_focused = true;
}
