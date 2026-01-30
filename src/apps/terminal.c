#include "terminal.h"
#include "compositor.h"
#include "io.h"
#include "kheap.h"
#include "pmm.h"
#include "string.h"
#include "timer.h"
#include "video.h"

/* Terminal agora é um Wayland Client (simulado no Kernel) */

wl_surface_t *term_win = NULL; // Tornar global
static wl_surface_t *term_surface = NULL;
#define term_surface                                                           \
  term_win /* Alias para usar o nome existente no arquivo                      \
            */
static wl_buffer_t term_buffer;
static char input_buffer[64];
static int input_ptr = 0;
static char prompt_line[128] = "> ";
static char output_text[4096] =
    "LiwusOS Shell (Wayland/LGX Port)\nDigite 'help'.\n";

void terminal_redraw() {
  if (!term_surface)
    return;

  // 1. Redirecionar desenho para o buffer do terminal
  video_set_target(term_buffer.pixels, term_buffer.width, term_buffer.height);

  // 2. Desenhar fundo e UI
  draw_rect(0, 0, term_buffer.width, term_buffer.height, 0x111111); // Dark BG
  draw_rect(0, 0, term_buffer.width, 25, 0x333333);                 // Titlebar
  draw_string(10, 5, "Liwus Terminal", 0xFFFFFF);

  // 3. Desenhar Conteúdo
  // Simples renderizador de texto com quebra de linha manual
  int x = 10, y = 35;
  char *str = output_text;
  while (*str) {
    if (*str == '\n') {
      x = 10;
      y += 16;
    } else {
      draw_char(x, y, *str, 0xAAAAAA);
      x += 8;
    }
    str++;
  }

  // Prompt
  draw_string(10, 360, prompt_line, 0x00FF00);

  // Cursor
  if ((timer_ticks / 50) % 2 == 0) {
    draw_rect(10 + strlen(prompt_line) * 8, 360, 8, 16, 0x00FF00);
  }

  // 4. Restaurar target e Commitar superfície
  video_reset_target();
  wl_commit(term_surface);
}

// Helper to tokenize command
void exec_command_term(const char *cmd_raw) {
  char cmd_buf[128];
  strcpy(cmd_buf, cmd_raw);

  char *args[10];
  int argc = 0;

  // Tokenize
  char *token = cmd_buf;
  char *next_space = NULL;

  while (*token && argc < 10) {
    // Find space
    next_space = token;
    while (*next_space && *next_space != ' ')
      next_space++;

    if (*next_space) {
      *next_space = 0; // Null terminate token
      args[argc++] = token;
      token = next_space + 1;
      // Skip extra spaces
      while (*token == ' ')
        token++;
    } else {
      args[argc++] = token;
      break;
    }
  }
  args[argc] = NULL; // Sentinel

  if (argc == 0)
    return;

  char *cmd = args[0];

  if (strcmp(cmd, "help") == 0) {
    strcat(output_text, "Comandos disponiveis:\\n");
    strcat(output_text, "  help     - Mostra esta ajuda\\n");
    strcat(output_text, "  clear    - Limpa a tela\\n");
    strcat(output_text, "  liwfetch - Info do sistema\\n");
    strcat(output_text, "  browser  - Abre o navegador web\\n");
    strcat(output_text, "  reboot   - Reinicia o sistema\\n");
    strcat(output_text, "  liw      - Gerenciador de pacotes\\n");
    strcat(output_text, "  exec     - Executa programa\\n");
  } else if (strcmp(cmd, "clear") == 0) {
    output_text[0] = '\0';
  } else if (strcmp(cmd, "browser") == 0) {
    if (argc < 2) {
      strcat(output_text, "Uso: browser <url ou busca>\n");
    } else {
      // Reconstitute query from args
      char query[256] = "";
      for (int i = 1; i < argc; i++) {
        strcat(query, args[i]);
        if (i < argc - 1)
          strcat(query, " ");
      }

      char result[4096] = "";
      extern void browser_cli_execute(const char *input, char *output,
                                      uint32_t max_len);
      browser_cli_execute(query, result, 4096);
      strcat(output_text, result);
      strcat(output_text, "\n");
    }
  } else if (strcmp(cmd, "liwfetch") == 0) {
    strcat(output_text,
           "LiwusOS Wayland Edition\\nArchitecture: LGX Compositor\\n");
  } else if (strcmp(cmd, "reboot") == 0) {
    sys_reboot();
  } else if (strcmp(cmd, "liw") == 0 || strcmp(cmd, "exec") == 0) {
    // Handle "liw ..." directly or "exec liw ..."
    // If just "liw", we run passing the rest as args.
    // If "exec prog args...", we run prog with args.

    char *prog;
    char **prog_args;

    if (strcmp(cmd, "exec") == 0) {
      if (argc < 2) {
        strcat(output_text, "Usage: exec <program> [args]\\n");
        terminal_redraw();
        return;
      }
      prog = args[1];
      prog_args = &args[1]; // argv[0] for the new prog is the prog name
    } else {
      // It is "liw"
      prog = "liw";
      prog_args = args; // argv[0] is "liw"
    }

    // Call syscall
    strcat(output_text, "Executing ");
    strcat(output_text, prog);
    strcat(output_text, "...\\n");
    terminal_redraw();

    int ret = sys_execve(prog, prog_args, NULL);
    if (ret < 0) {
      strcat(output_text, "Failed to execute.\\n");
    }
  } else {
    strcat(output_text, "Comando desconhecido. Digite 'help'.\\n");
  }
  terminal_redraw();
}

void init_terminal_app() {
  // Cria Buffer (Backing Store shared memory)
  term_buffer.width = 600;
  term_buffer.height = 400;
  term_buffer.pixels = (uint32_t *)kmalloc(600 * 400 * 4);
  term_buffer.shm = true;

  // Cria Superfície
  term_surface = wl_create_surface(600, 400, WL_SURFACE_TOPLEVEL);
  term_surface->x = 100;
  term_surface->y = 100;

  // Set Title
  strcpy(term_surface->title, "Liwus Terminal");

  // Attach e render inicial
  wl_attach_buffer(term_surface, &term_buffer);
  terminal_redraw();
}

void open_terminal() {
  if (!term_win)
    init_terminal_app();
  term_win->visible = true;
  wl_set_focused_surface(term_win);
}

void update_terminal_key(char k) {
  if (!term_surface || !term_surface->is_focused)
    return;

  if (k == '\n') {
    strcat(output_text, prompt_line);
    strcat(output_text, "\n");
    exec_command_term(input_buffer);
    input_ptr = 0;
    input_buffer[0] = '\0';
    strcpy(prompt_line, "> ");
  } else if (k == '\b') {
    if (input_ptr > 0) {
      input_buffer[--input_ptr] = '\0';
      prompt_line[2 + input_ptr] = '\0';
    }
  } else if (k >= 32 && input_ptr < 60) {
    input_buffer[input_ptr++] = k;
    input_buffer[input_ptr] = '\0';
    strcat(prompt_line, (char[]){k, 0});
  }
  terminal_redraw();
}