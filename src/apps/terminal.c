#include "terminal.h"
#include "compositor.h"
#include "sdfs.h"
#include "io.h"
#include "kheap.h"
#include "lvgl_shell.h"
#include "http.h"
#include "net.h"
#include "netstack.h"
#include "pmm.h"
#include "serial.h"
#include "string.h"
#include "syscall.h"
#include "task.h"
#include "timer.h"
#include "video.h"
#include "vga.h"

/* Terminal agora é um Wayland Client (simulado no Kernel) */

wl_surface_t *term_win = NULL; // Tornar global
#define term_surface                                                           \
  term_win /* Alias para usar o nome existente no arquivo                      \
            */
static wl_buffer_t term_buffer;
static char input_buffer[256];
static int input_ptr = 0;
static char prompt_line[256] = "> ";
static char output_text[8192] =
    "LiwusOS Shell (Wayland/LGX Port)\nDigite 'help'.\n";
static char terminal_cwd[128] = "/";

char* terminal_get_cwd() {
  return terminal_cwd;
}

typedef enum {
  TERM_MODE_SHELL = 0,
  TERM_MODE_EDITOR = 1,
  TERM_MODE_TOP = 2
} terminal_mode_t;

static terminal_mode_t terminal_mode = TERM_MODE_SHELL;
static bool editor_insert_mode = false;
static int editor_cursor_pos = 0; // Antigo, manter por compatibilidade se necessário
static int terminal_editor_cursor_x = 0;
static int terminal_editor_cursor_y = 0;
static char editor_file_path[160];
static char editor_file_name[64];
static char editor_buffer[8192];
static char editor_clipboard[1024];
static char editor_pending_cmd = 0;
static bool terminal_output_dirty = false;
static uint32_t terminal_live_last_tick = 0;
static char terminal_text_view[12288];
static bool terminal_console_mode = false;

static void terminal_redraw(void);

void terminal_enable_console_mode(void) { terminal_console_mode = true; }

static void terminal_str_append(char *dst, size_t dst_size, const char *src) {
  size_t len = strlen(dst);

  if (len >= dst_size - 1) {
    return;
  }

  strncpy(dst + len, src, dst_size - len - 1);
  dst[dst_size - 1] = '\0';
}

static const char *terminal_find_last_slash(const char *s) {
  const char *last = NULL;

  while (s && *s) {
    if (*s == '/') {
      last = s;
    }
    s++;
  }

  return last;
}

static char terminal_ascii_tolower(char c) {
  if (c >= 'A' && c <= 'Z') {
    return (char)(c - 'A' + 'a');
  }
  return c;
}

static int terminal_has_lua_suffix(const char *s) {
  size_t len;

  if (!s) {
    return 0;
  }

  len = strlen(s);
  if (len < 4) {
    return 0;
  }

  return terminal_ascii_tolower(s[len - 4]) == '.' &&
         terminal_ascii_tolower(s[len - 3]) == 'l' &&
         terminal_ascii_tolower(s[len - 2]) == 'u' &&
         terminal_ascii_tolower(s[len - 1]) == 'a';
}

static void terminal_trim_output(size_t incoming_len) {
  size_t current_len = strlen(output_text);

  if (incoming_len >= sizeof(output_text) - 1) {
    output_text[0] = '\0';
    return;
  }

  if (current_len + incoming_len < sizeof(output_text) - 1) {
    return;
  }

  // Se precisar de espaço, removemos o suficiente para caber incoming_len + 1/4 do buffer de folga
  size_t needed = incoming_len + (sizeof(output_text) / 4);
  if (needed > current_len) needed = current_len;
  
  size_t drop = needed;
  while (drop < current_len && output_text[drop] != '\n') {
    drop++;
  }
  if (drop < current_len && output_text[drop] == '\n') {
    drop++;
  }

  if (drop > current_len) drop = current_len;

  memmove(output_text, output_text + drop, current_len - drop + 1);
}

// Hook externo para o liwshd
extern void remote_shell_print_n(const char* msg, int n);
static bool remote_mirror_active = false;

void terminal_append_output_n(const char *text, int len) {
  if (!text || len <= 0) {
    return;
  }

  // Trava de recursão para evitar loop infinito de rede
  if (!remote_mirror_active) {
      remote_mirror_active = true;
      remote_shell_print_n(text, len);
      remote_mirror_active = false;
  }

  for (int i = 0; i < len; i++) {
    write_serial(text[i]);
    if (terminal_console_mode) {
      vga_putc(text[i]);
    }
  }

  terminal_trim_output((size_t)len);

  size_t current_len = strlen(output_text);
  size_t available = sizeof(output_text) - current_len - 1;
  size_t to_copy = (size_t)len;
  if (to_copy > available) to_copy = available;

  if (to_copy > 0) {
    memcpy(output_text + current_len, text, to_copy);
    output_text[current_len + to_copy] = '\0';
    terminal_output_dirty = true;
  }
}

void terminal_append_output(const char *text) {
  if (!text) {
    return;
  }

  // Redireciona para o "SSH" se houver cliente
  extern void remote_shell_print(const char* msg);
  remote_shell_print(text);

  terminal_append_output_n(text, (int)strlen(text));
}


int terminal_has_dirty_output(void) {
  return terminal_output_dirty ? 1 : 0;
}

void terminal_clear_dirty_output(void) {
  terminal_output_dirty = false;
}

int terminal_needs_update(uint32_t now_ticks) {
  if (terminal_output_dirty) {
    return 1;
  }

  if (terminal_mode == TERM_MODE_TOP && (term_surface || terminal_console_mode) &&
      now_ticks - terminal_live_last_tick >= 10) {
    return 1;
  }

  return 0;
}

void terminal_flush_updates(uint32_t now_ticks) {
  if (!term_surface && !terminal_console_mode) {
    return;
  }

  if (!terminal_output_dirty &&
      !(terminal_mode == TERM_MODE_TOP &&
        now_ticks - terminal_live_last_tick >= 10)) {
    return;
  }

  terminal_output_dirty = false;
  if (terminal_mode == TERM_MODE_TOP) {
    terminal_live_last_tick = now_ticks;
  }
  terminal_redraw();
}

static void terminal_format_prompt(void) {
  size_t out = 0;

  prompt_line[out++] = '[';
  for (size_t i = 0; terminal_cwd[i] && out < sizeof(prompt_line) - 5; i++) {
    prompt_line[out++] = terminal_cwd[i];
  }
  prompt_line[out++] = ']';
  prompt_line[out++] = '#';
  prompt_line[out++] = ' ';
  prompt_line[out] = '\0';
}

static void terminal_reset_input(void) {
  input_ptr = 0;
  input_buffer[0] = '\0';
  terminal_format_prompt();
}

static void terminal_build_path_label(const char *path, char *out,
                                      size_t out_size) {
  size_t len = 0;

  if (!out || out_size == 0) {
    return;
  }

  out[0] = '\0';
  out[len++] = 'C';
  out[len++] = ':';
  out[len++] = '\\';
  out[len] = '\0';

  if (!path || strcmp(path, "/") == 0) {
    return;
  }

  for (size_t i = 1; path[i] && len < out_size - 1; i++) {
    out[len++] = path[i] == '/' ? '\\' : path[i];
  }
  out[len] = '\0';
}

static void terminal_normalize_path(const char *input, char *out,
                                    size_t out_size) {
  char working[192];
  char temp[192];
  int seg_start[16];
  int seg_len[16];
  int seg_count = 0;
  int write = 0;

  if (!input || !input[0]) {
    strncpy(out, terminal_cwd, out_size - 1);
    out[out_size - 1] = '\0';
    return;
  }

  if (input[0] == '/') {
    strncpy(working, input, sizeof(working) - 1);
    working[sizeof(working) - 1] = '\0';
  } else if (strcmp(terminal_cwd, "/") == 0) {
    strcpy(working, "/");
    terminal_str_append(working, sizeof(working), input);
  } else {
    strncpy(working, terminal_cwd, sizeof(working) - 1);
    working[sizeof(working) - 1] = '\0';
    terminal_str_append(working, sizeof(working), "/");
    terminal_str_append(working, sizeof(working), input);
  }

  temp[write++] = '/';
  for (int i = 1; working[i] && write < (int)sizeof(temp) - 1; i++) {
    char ch = working[i];
    if (ch == '\\') {
      ch = '/';
    }
    if (ch == '/' && temp[write - 1] == '/') {
      continue;
    }
    temp[write++] = ch;
  }
  if (write > 1 && temp[write - 1] == '/') {
    write--;
  }
  temp[write] = '\0';

  for (int i = 1; temp[i];) {
    int start = i;
    int len = 0;

    while (temp[i] && temp[i] != '/') {
      i++;
      len++;
    }
    if (len == 1 && temp[start] == '.') {
    } else if (len == 2 && temp[start] == '.' && temp[start + 1] == '.') {
      if (seg_count > 0) {
        seg_count--;
      }
    } else if (len > 0 && seg_count < 16) {
      seg_start[seg_count] = start;
      seg_len[seg_count] = len;
      seg_count++;
    }
    if (temp[i] == '/') {
      i++;
    }
  }

  write = 0;
  out[write++] = '/';
  for (int s = 0; s < seg_count && write < (int)out_size - 1; s++) {
    if (s > 0 && write < (int)out_size - 1) {
      out[write++] = '/';
    }
    for (int j = 0; j < seg_len[s] && write < (int)out_size - 1; j++) {
      out[write++] = temp[seg_start[s] + j];
    }
  }
  out[write] = '\0';
}

static void terminal_join_filename(const char *name, char *out, size_t out_size) {
  terminal_normalize_path(name, out, out_size);
}

static void terminal_resolve_path(const char *name, char *out, size_t out_size) {
  char tmp[192];
  terminal_normalize_path(name, tmp, sizeof(tmp));
  if (strncmp(tmp, "/house/localhost", 16) == 0) {
    strncpy(out, tmp, out_size - 1);
    out[out_size - 1] = 0;
  } else {
    // Se não começar com /house/localhost, forçamos o prefixo para operações SDFS
    if (tmp[0] == '/') {
        snprintf(out, out_size, "/house/localhost%s", tmp);
    } else {
        snprintf(out, out_size, "/house/localhost/%s", tmp);
    }
  }
}

// Retorna o caminho relativo ao root do SDFS ou NULL se não for um caminho SDFS
static const char* terminal_get_sdfs_path(const char* full_path) {
    if (strncmp(full_path, "/house/localhost", 16) == 0) {
        const char* p = full_path + 16;
        if (*p == '\0') return "/";
        return p;
    }
    return NULL;
}

static void terminal_print_path(const char *path) {
  char label[160];
  terminal_build_path_label(path, label, sizeof(label));
  terminal_append_output(label);
  terminal_append_output("\n");
}

static int terminal_copy_file(const char *src_path, const char *dst_path,
                              bool remove_source) {
  uint32_t size = 0;
  int is_dir = 0;
  void *data;

  const char *s_src = terminal_get_sdfs_path(src_path);
  const char *s_dst = terminal_get_sdfs_path(dst_path);

  if (!s_src || !s_dst) return -1;

  if (!sdfs_path_info(s_src, &is_dir, &size) || is_dir) {
    return -1;
  }

  data = sdfs_read_file(s_src, &size);
  if (!data) {
    return -1;
  }

  sdfs_create_file(s_dst);
  if (sdfs_write_file(s_dst, (uint8_t *)data, size) != size) {
    kfree(data);
    return -1;
  }

  kfree(data);

  if (remove_source) {
    if (sdfs_delete(s_src) != 0) {
      return -1;
    }
  }

  return 0;
}

static void terminal_editor_open(const char *path) {
  uint32_t size = 0;
  int is_dir = 0;
  void *data = NULL;

  if (!path || !path[0]) {
    terminal_append_output("Usage: edit <arquivo>\n");
    return;
  }

  strncpy(editor_file_path, path, sizeof(editor_file_path) - 1);
  editor_file_path[sizeof(editor_file_path) - 1] = '\0';

  {
    const char *leaf = terminal_find_last_slash(path);
    leaf = leaf ? leaf + 1 : path;
    strncpy(editor_file_name, leaf, sizeof(editor_file_name) - 1);
    editor_file_name[sizeof(editor_file_name) - 1] = '\0';
  }

  const char* s_path = terminal_get_sdfs_path(path);

  editor_buffer[0] = '\0';
  if (s_path && sdfs_path_info(s_path, &is_dir, &size)) {
    if (is_dir) {
      terminal_append_output("Nao e possivel editar um diretorio.\n");
      return;
    }
    data = sdfs_read_file(s_path, &size);
    if (data) {
      if (size >= sizeof(editor_buffer)) {
        size = sizeof(editor_buffer) - 1;
      }
      memcpy(editor_buffer, data, size);
      editor_buffer[size] = '\0';
      kfree(data);
    }
  }

  terminal_mode = TERM_MODE_EDITOR;
  editor_insert_mode = false;
  terminal_editor_cursor_x = 0;
  terminal_editor_cursor_y = 0;
  terminal_append_output("Abrindo editor no terminal...\n");
}

static void terminal_editor_save(void) {
  if (!editor_file_path[0]) {
    terminal_append_output("Editor sem arquivo alvo.\n");
    return;
  }

  const char* s_path = terminal_get_sdfs_path(editor_file_path);
  if (!s_path) {
      terminal_append_output("Erro: Editor so pode salvar em /house/localhost.\n");
      return;
  }

  sdfs_create_file(s_path);
  sdfs_write_file(s_path, (uint8_t *)editor_buffer,
                        strlen(editor_buffer));
  terminal_append_output("Arquivo salvo.\n");
}

static void terminal_editor_close(bool announce) {
  terminal_mode = TERM_MODE_SHELL;
  editor_insert_mode = false;
  if (terminal_console_mode) {
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
  }
  if (announce) {
    terminal_append_output("Editor fechado.\n");
  }
}

static void terminal_open_top(void) {
  terminal_mode = TERM_MODE_TOP;
  terminal_live_last_tick = 0;
  terminal_output_dirty = true;
}

static uint32_t vga_palette[16] = {
    0x00000000, 0x000000AA, 0x0000AA00, 0x0000AAAA,
    0x00AA0000, 0x00AA00AA, 0x00AA5500, 0x00AAAAAA,
    0x00555555, 0x005555FF, 0x0055FF55, 0x0055FFFF,
    0x00FF5555, 0x00FF55FF, 0x00FFFF55, 0x00FFFFFF,
};

static void vga_text_to_fb() {
    if (!backbuffer || screen_width == 0 || screen_height == 0) return;
    video_reset_target();
    clear_screen(0x00111111);

    char *text = output_text;
    int text_len = strlen(text);

    int line_starts[64];
    int line_count = 0;
    int i = 0;

    while (i < text_len && line_count < 64) {
        line_starts[line_count++] = i;
        while (i < text_len && text[i] != '\n') i++;
        if (i < text_len && text[i] == '\n') i++;
    }

    int start_line = 0;
    if (line_count > 24) start_line = line_count - 24;

    int row = 0;
    for (int l = start_line; l < line_count && row < 25; l++, row++) {
        int pos = line_starts[l];
        int col = 0;
        while (pos < text_len && text[pos] != '\n' && col < 80) {
            if (text[pos] >= 0x20)
                draw_char(col * 8, row * 16, text[pos], 0x00AAAAAA);
            col++;
            pos++;
        }
    }

    row = 24;
    int col = 0;
    for (int p = 0; prompt_line[p] && col < 80; p++) {
        draw_char(col * 8, row * 16, prompt_line[p], 0x0000AA00);
        col++;
    }
    for (int p = 0; input_buffer[p] && col < 80; p++) {
        draw_char(col * 8, row * 16, input_buffer[p], 0x00AAAAAA);
        col++;
    }

    refresh_screen();
}

static void terminal_redraw_vga() {
  if (terminal_mode == TERM_MODE_EDITOR) {
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    // Barra de Status Superior
    vga_set_cursor(0, 0);
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
    for(int i=0; i<80; i++) vga_putc(' ');
    vga_set_cursor(1, 0);
    vga_puts("Liwim 1.0 - [");
    vga_puts(editor_file_name[0] ? editor_file_name : "Novo Arquivo");
    vga_puts("]");
    
    vga_set_cursor(50, 0);
    vga_puts(editor_insert_mode ? "MODO: INSERIR" : "MODO: COMANDO");

    // Conteúdo do Arquivo
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    
    int row = 0;
    int col = 0;
    int scroll_offset_y = (terminal_editor_cursor_y >= 22) ? terminal_editor_cursor_y - 21 : 0;

    for (int i = 0; editor_buffer[i] != '\0'; i++) {
      if (row >= scroll_offset_y && row < scroll_offset_y + 23) {
        int screen_row = row - scroll_offset_y + 1;
        vga_set_cursor(col, screen_row);
        
        // Highlight cursor position in Command mode
        if (!editor_insert_mode && row == terminal_editor_cursor_y && col == terminal_editor_cursor_x) {
            vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
        } else {
            vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        }

        if (editor_buffer[i] == '\n') {
          // Se o cursor estiver num newline, mostramos um espaço destacado
          if (!editor_insert_mode && row == terminal_editor_cursor_y && col == terminal_editor_cursor_x) {
              vga_putc(' ');
          }
          row++;
          col = 0;
        } else {
          if (col < 80) {
            vga_putc(editor_buffer[i]);
            col++;
          }
        }
      } else {
        if (editor_buffer[i] == '\n') {
          row++;
          col = 0;
        } else {
          col++;
        }
      }
    }

    // Se o cursor estiver no fim do arquivo
    if (!editor_insert_mode && row == terminal_editor_cursor_y && col == terminal_editor_cursor_x) {
        vga_set_cursor(col, row - scroll_offset_y + 1);
        vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
        vga_putc(' ');
    }

    // Barra de Instruções Inferior
    vga_set_cursor(0, 24);
    vga_set_color(VGA_COLOR_BLACK, VGA_COLOR_CYAN);
    for(int i=0; i<80; i++) vga_putc(' ');
    vga_set_cursor(1, 24);
    if (editor_insert_mode) {
      vga_puts("ESC: Voltar | Digite...");
    } else {
      vga_puts("i: Insert | s: Save | q: Quit | h/j/k/l: Move");
    }
    
    // Posicionar cursor de hardware no modo inserção
    if (editor_insert_mode) {
        vga_set_cursor(terminal_editor_cursor_x, terminal_editor_cursor_y - scroll_offset_y + 1);
    } else {
        vga_set_cursor(79, 24); // Esconde o cursor de hardware no canto
    }
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
  } else if (terminal_mode == TERM_MODE_TOP) {
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    task_info_t tasks[32];
    int task_count = task_snapshot(tasks, 32);
    vga_set_color(VGA_COLOR_WHITE, VGA_COLOR_CYAN);
    vga_puts(" PID  PPID TYPE  STATE  NAME\n");
    vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    for (int i = 0; i < task_count && i < 20; i++) {
        char pid[8], ppid[8];
        itoa(tasks[i].id, pid, 10);
        itoa(tasks[i].parent_id, ppid, 10);
        vga_puts(pid); vga_puts("  ");
        vga_puts(ppid); vga_puts("  ");
        vga_puts(tasks[i].user_mode ? "USER" : "KERN"); vga_puts("  ");
        vga_puts(task_state_name(tasks[i].state)); vga_puts("  ");
        vga_puts(tasks[i].name); vga_puts("\n");
    }
  } else {
    // In SHELL mode, we rely on vga_putc being called during output.
    // If we want to redraw the whole screen (e.g. after 'clear'), we do it here.
    static bool initial_redraw = true;
    if (initial_redraw) {
        vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        vga_puts("LiwusOS Shell (VGA Mode)\nDigite 'help'.\n\n");
        vga_set_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        vga_puts(prompt_line);
        vga_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        initial_redraw = false;
    }
  }
  vga_text_to_fb();
}

static void terminal_redraw() {
  if (!term_surface && !terminal_console_mode)
    return;

  if (terminal_console_mode) {
    terminal_redraw_vga();
    return;
  }

  int render_w = terminal_console_mode ? (int)screen_width : (int)term_buffer.width;
  int render_h = terminal_console_mode ? (int)screen_height : (int)term_buffer.height;
  int prompt_y = render_h - 40;
  int status_y = render_h - 28;
  int content_bottom = render_h - 56;

  // 2. Desenhar fundo e UI
  draw_rect(0, 0, render_w, render_h, 0x111111); // Dark BG
  draw_rect(0, 0, render_w, 25, 0x333333);       // Titlebar
  draw_string(10, 5, "Liwus Terminal", 0xFFFFFF);

  if (terminal_mode == TERM_MODE_EDITOR) {
    char path_label[180];
    terminal_build_path_label(editor_file_path, path_label, sizeof(path_label));
    draw_string(10, 35, "Liwim Terminal", 0xFFFFFF);
    draw_string(10, 52, path_label, 0xAAAAAA);
    draw_string(10, 69, editor_insert_mode ? "-- INSERT --" : "-- NORMAL --",
                editor_insert_mode ? 0x55FF55 : 0xFFCC66);
    draw_string(10, 86, "NORMAL: i insere, s salva, q sai | ESC volta",
                0xAAAAAA);
    draw_rect(8, 108, render_w - 16, content_bottom - 108, 0x181818);

    int x_start = 16, y_start = 116;
    char *str = editor_buffer;
    
    int row = 0, col = 0;
    int scroll_offset_y = (terminal_editor_cursor_y >= 15) ? terminal_editor_cursor_y - 14 : 0;

    while (*str) {
      if (row >= scroll_offset_y && row < scroll_offset_y + 16) {
        int screen_y = y_start + (row - scroll_offset_y) * 16;
        int screen_x = x_start + col * 8;

        // Draw cursor highlight in Normal mode
        if (!editor_insert_mode && row == terminal_editor_cursor_y && col == terminal_editor_cursor_x) {
            draw_rect(screen_x, screen_y, 8, 16, 0xFFCC66);
            if (*str != '\n') draw_char(screen_x, screen_y, *str, 0x000000);
        } else {
            if (*str != '\n' && screen_x < render_w - 24) {
                draw_char(screen_x, screen_y, *str, 0xDDDDDD);
            }
        }

        if (*str == '\n') {
          row++;
          col = 0;
        } else {
          col++;
          if (col * 8 > render_w - 40) { // Wrap
              row++;
              col = 0;
          }
        }
      } else {
        if (*str == '\n') { row++; col = 0; }
        else {
            col++;
            if (col * 8 > render_w - 40) { row++; col = 0; }
        }
      }
      str++;
    }

    // Cursor at EOF
    if (row == terminal_editor_cursor_y && col == terminal_editor_cursor_x) {
        int screen_y = y_start + (row - scroll_offset_y) * 16;
        int screen_x = x_start + col * 8;
        if (screen_y <= content_bottom - 12) {
            draw_rect(screen_x, screen_y, 8, 16, editor_insert_mode ? 0x55FF55 : 0xFFCC66);
        }
    }

    draw_rect(0, render_h - 40, render_w, 40, 0x1C1C1C);
    char status_msg[128];
    strcpy(status_msg, "Editor: ");
    strcat(status_msg, editor_file_name);
    strcat(status_msg, " | L:");
    itoa(terminal_editor_cursor_y + 1, status_msg + strlen(status_msg), 10);
    strcat(status_msg, " C:");
    itoa(terminal_editor_cursor_x + 1, status_msg + strlen(status_msg), 10);
    draw_string(10, status_y, status_msg, 0xFFFFFF);
  } else if (terminal_mode == TERM_MODE_TOP) {
    task_info_t tasks[32];
    int task_count = task_snapshot(tasks, 32);
    uint32_t total_mem = pmm_get_total_memory();
    uint32_t used_mem = pmm_get_used_memory();
    uint32_t free_mem = pmm_get_free_memory();
    uint32_t total_ticks = timer_ticks ? timer_ticks : 1;
    uint32_t switch_total = task_total_switches();
    int running = 0, ready = 0, sleeping = 0, zombie = 0;
    char line[160];
    int row_y = 116;

    for (int i = 0; i < task_count; i++) {
      switch (tasks[i].state) {
      case TASK_RUNNING:
        running++;
        break;
      case TASK_READY:
        ready++;
        break;
      case TASK_SLEEPING:
        sleeping++;
        break;
      case TASK_ZOMBIE:
        zombie++;
        break;
      }
    }

    draw_string(10, 35, "LiwusOS top", 0xFFFFFF);
    draw_string(10, 52, "Monitor do kernel em tempo real | q para sair",
                0xAAAAAA);

    strcpy(line, "Tasks: ");
    itoa(task_count, line + strlen(line), 10);
    strcat(line, " | RUN ");
    itoa(running, line + strlen(line), 10);
    strcat(line, " READY ");
    itoa(ready, line + strlen(line), 10);
    strcat(line, " SLEEP ");
    itoa(sleeping, line + strlen(line), 10);
    strcat(line, " ZOMB ");
    itoa(zombie, line + strlen(line), 10);
    draw_string(10, 70, line, 0xDDDDDD);

    strcpy(line, "Ticks: ");
    itoa((int)timer_ticks, line + strlen(line), 10);
    strcat(line, " | Switches: ");
    itoa((int)switch_total, line + strlen(line), 10);
    draw_string(320, 35, line, 0xDDDDDD);

    strcpy(line, "Mem used ");
    itoa((int)(used_mem / 1024), line + strlen(line), 10);
    strcat(line, " KB | free ");
    itoa((int)(free_mem / 1024), line + strlen(line), 10);
    strcat(line, " KB | total ");
    itoa((int)(total_mem / 1024), line + strlen(line), 10);
    strcat(line, " KB");
    draw_string(320, 52, line, 0xDDDDDD);

    draw_rect(8, 92, render_w - 16, content_bottom - 92, 0x181818);
    draw_string(16, 98, "PID  PPID TYPE  STATE  CPU%  HEAPKB  NAME", 0xFFFFFF);

    for (int i = 0; i < task_count && row_y <= content_bottom - 20; i++, row_y += 16) {
      uint32_t heap_bytes = tasks[i].heap_end > tasks[i].heap_start
                                ? tasks[i].heap_end - tasks[i].heap_start
                                : 0;
      uint32_t cpu_pct = (tasks[i].cpu_ticks * 100U) / total_ticks;
      char pid[16], ppid[16], cpu[16], heap[16];

      itoa(tasks[i].id, pid, 10);
      if (tasks[i].parent_id >= 0) {
        itoa(tasks[i].parent_id, ppid, 10);
      } else {
        strcpy(ppid, "-");
      }
      itoa((int)cpu_pct, cpu, 10);
      itoa((int)(heap_bytes / 1024), heap, 10);

      draw_string(16, row_y, pid, 0xDDDDDD);
      draw_string(56, row_y, ppid, 0xAAAAAA);
      draw_string(104, row_y, tasks[i].user_mode ? "USER" : "KERN",
                  tasks[i].user_mode ? 0x66CCFF : 0xFFCC66);
      draw_string(152, row_y, task_state_name(tasks[i].state), 0xDDDDDD);
      draw_string(216, row_y, cpu, 0x55FF55);
      draw_string(272, row_y, heap, 0xAAAAAA);
      draw_string(344, row_y, tasks[i].name[0] ? tasks[i].name : "?",
                  0xDDDDDD);
    }

    draw_rect(0, render_h - 40, render_w, 40, 0x1C1C1C);
    draw_string(10, status_y, "Tasks, memoria PMM e ticks reais do kernel",
                0xFFFFFF);
  } else {
    // 3. Desenhar Conteúdo
    int x = 10, y = 35;
    char *str = output_text;
    int line_count = 0;
    
    // Contar total de linhas
    for (char *p = output_text; *p; p++) {
        if (*p == '\n') line_count++;
    }
    
    // Determinar quantas linhas cabem (aprox. 16 pixels por linha)
    int max_visible_lines = (content_bottom - 35) / 16;
    int start_line = 0;
    if (line_count > max_visible_lines) {
        start_line = line_count - max_visible_lines;
    }
    
    // Pular linhas até start_line
    int current_l = 0;
    while (*str && current_l < start_line) {
        if (*str == '\n') current_l++;
        str++;
    }

    // Desenhar apenas o que cabe
    while (*str) {
      if (*str == '\n') {
        x = 10;
        y += 16;
      } else {
        if (x < render_w - 10) {
            draw_char(x, y, *str, 0xAAAAAA);
            x += 8;
        }
      }
      if (y > content_bottom - 16) {
        break;
      }
      str++;
    }

    draw_rect(0, render_h - 40, render_w, 40, 0x1C1C1C);
    draw_string(10, prompt_y, prompt_line, 0x00FF00);

    if ((timer_ticks / 50) % 2 == 0) {
      draw_rect(10 + strlen(prompt_line) * 8, prompt_y, 8, 16, 0x00FF00);
    }
  }

  if (terminal_console_mode) {
    refresh_screen();
  } else {
    video_reset_target();
    wl_commit(term_surface);
  }
}

static void terminal_view_append(char *dst, size_t dst_size, const char *src) {
  size_t len;

  if (!dst || !src || dst_size == 0) {
    return;
  }

  len = strlen(dst);
  if (len >= dst_size - 1) {
    return;
  }

  strncpy(dst + len, src, dst_size - len - 1);
  dst[dst_size - 1] = '\0';
}

static void terminal_view_append_uint(char *dst, size_t dst_size,
                                      uint32_t value) {
  char tmp[24];
  itoa((int)value, tmp, 10);
  terminal_view_append(dst, dst_size, tmp);
}

static void terminal_append_ip(uint32_t ip) {
  char num[16];

  itoa((int)(ip & 0xFF), num, 10);
  terminal_append_output(num);
  terminal_append_output(".");
  itoa((int)((ip >> 8) & 0xFF), num, 10);
  terminal_append_output(num);
  terminal_append_output(".");
  itoa((int)((ip >> 16) & 0xFF), num, 10);
  terminal_append_output(num);
  terminal_append_output(".");
  itoa((int)((ip >> 24) & 0xFF), num, 10);
  terminal_append_output(num);
}

static void terminal_append_mac(const uint8_t *mac) {
  char hex[3];

  if (!mac) {
    terminal_append_output("(null)");
    return;
  }

  for (int i = 0; i < 6; i++) {
    hex_to_str(mac[i], hex);
    terminal_append_output(hex);
    if (i < 5) {
      terminal_append_output(":");
    }
  }
}

static void terminal_append_uint(uint32_t value) {
  char num[16];
  itoa((int)value, num, 10);
  terminal_append_output(num);
}

static void terminal_list_dir(const char *path) {
  fs_node_t *node = vfs_open(path);
  if (!node) {
    terminal_append_output("Erro: Nao foi possível abrir o diretorio.\n");
    return;
  }

  struct dirent *de = NULL;
  int i = 0;
  char sizebuf[32];

  while ((de = readdir_fs(node, i)) != 0) {
    fs_node_t *file = finddir_fs(node, de->name);
    if (file && (file->flags & FS_DIRECTORY)) {
      terminal_append_output("[DIR] ");
      terminal_append_output(de->name);
      terminal_append_output("\n");
    } else {
      terminal_append_output("      ");
      terminal_append_output(de->name);
      terminal_append_output("  ");
      if (file) {
        itoa((int)file->length, sizebuf, 10);
        terminal_append_output(sizebuf);
        terminal_append_output(" B");
      }
      terminal_append_output("\n");
    }
    if (file) kfree(file);
    i++;
  }
  kfree(node);
}

// Helper to tokenize command
void exec_command_term(const char *cmd_raw) {
  char cmd_buf[256];
  strncpy(cmd_buf, cmd_raw, sizeof(cmd_buf) - 1);
  cmd_buf[sizeof(cmd_buf) - 1] = '\0';

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
  char resolved_a[160];
  char resolved_b[160];

  if (strcmp(cmd, "help") == 0) {
    terminal_append_output("Comandos disponiveis:\n");
    terminal_append_output("  ls, cd, pwd, cat - Arquivos\n");
    terminal_append_output("  touch, mkdir, rm - Criar/Remover\n");
    terminal_append_output("  zip, unzip       - Comprimir\n");
    terminal_append_output("  view [imagem]    - Ver PNG/JPG\n");
    terminal_append_output("  free, df         - Memoria e Disco\n");
    terminal_append_output("  ifconfig, ping   - Rede\n");
    terminal_append_output("  uname, whoami    - Sistema e Usuario\n");
    terminal_append_output("  uptime, date     - Tempo\n");
    terminal_append_output("  lua, crun        - Desenvolvimento\n");
    terminal_append_output("  clear, reboot    - Controle\n");
  } else if (strcmp(cmd, "uname") == 0) {
    terminal_append_output("LiwusOS 2.0-brabo (i686-elf)\n");
  } else if (strcmp(cmd, "whoami") == 0) {
    terminal_append_output("davidev (localhost)\n");
  } else if (strcmp(cmd, "uptime") == 0) {
    terminal_append_output("Uptime: ");
    terminal_append_uint(timer_ticks / 100);
    terminal_append_output(" segundos.\n");
  } else if (strcmp(cmd, "free") == 0) {
    uint32_t total = pmm_get_total_memory() / 1024 / 1024;
    uint32_t used = pmm_get_used_memory() / 1024 / 1024;
    terminal_append_output("Memoria RAM (MB):\n");
    terminal_append_output("Total: "); terminal_append_uint(total);
    terminal_append_output("\nUsada: "); terminal_append_uint(used);
    terminal_append_output("\nLivre: "); terminal_append_uint(total - used);
    terminal_append_output("\n");
  } else if (strcmp(cmd, "echo") == 0) {
    for (int i = 1; i < argc; i++) {
      terminal_append_output(args[i]);
      terminal_append_output(" ");
    }
    terminal_append_output("\n");
  } else if (strcmp(cmd, "df") == 0) {
    terminal_append_output("Filesystem      Size  Used  Avail Use%\n");
    terminal_append_output("initrd (/)      512K  512K     0  100%\n");
    terminal_append_output("SDFS (C:/)      100M   12K   99M    1%\n");
  } else if (strcmp(cmd, "date") == 0) {
    terminal_append_output("Segunda, 13 de Abril de 2026\n");
  } else if (strcmp(cmd, "clear") == 0) {
    output_text[0] = '\0';
    if (terminal_console_mode) {
      vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    }
  } else if (strcmp(cmd, "pwd") == 0) {
    terminal_print_path(terminal_cwd);
  } else if (strcmp(cmd, "ls") == 0) {
    const char *target = argc >= 2 ? args[1] : terminal_cwd;
    terminal_join_filename(target, resolved_a, sizeof(resolved_a));
    
    fs_node_t *node = vfs_open(resolved_a);
    if (!node) {
      terminal_append_output("Caminho nao encontrado.\n");
    } else if (!(node->flags & FS_DIRECTORY)) {
      terminal_append_output("Nao e um diretorio.\n");
      kfree(node);
    } else {
      terminal_list_dir(resolved_a);
      kfree(node);
    }
  } else if (strcmp(cmd, "cd") == 0) {
    const char *target = argc >= 2 ? args[1] : "/";
    terminal_join_filename(target, resolved_a, sizeof(resolved_a));
    
    fs_node_t *node = vfs_open(resolved_a);
    if (!node || !(node->flags & FS_DIRECTORY)) {
      terminal_append_output("Diretorio invalido.\n");
    } else {
      strncpy(terminal_cwd, resolved_a, sizeof(terminal_cwd) - 1);
      terminal_cwd[sizeof(terminal_cwd) - 1] = '\0';
      terminal_format_prompt();
    }
    if (node) kfree(node);
  } else if (strcmp(cmd, "touch") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: touch <arquivo>\n");
    } else {
      terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
      const char* s_path = terminal_get_sdfs_path(resolved_a);
      if (s_path && sdfs_create_file(s_path) == 0) {
        sdfs_write_file(s_path, (uint8_t *)"", 0);
        terminal_append_output("Arquivo ");
        terminal_append_output(resolved_a);
        terminal_append_output(" criado.\n");
      } else {
        terminal_append_output("Falha ao criar: Verifique se o caminho esta no disco /house/localhost.\n");
      }
    }
  } else if (strcmp(cmd, "create") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: create <file> \"content\"\n");
    } else {
      terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
      const char* s_path = terminal_get_sdfs_path(resolved_a);
      if (!s_path) {
          terminal_append_output("Erro: Escrita permitida apenas em /house/localhost.\n");
      } else {
          char *content = "";
          // Procura conteúdo após o nome do arquivo, lidando com aspas se houver
          char *raw_cmd = (char *)cmd_raw;
          char *ptr = strstr(raw_cmd, args[1]);
          if (ptr) {
            ptr += strlen(args[1]);
            while (*ptr == ' ') ptr++;
            if (*ptr == '"') {
              ptr++;
              content = ptr;
              char *end = strchr(content, '"');
              if (end) *end = '\0';
            } else {
              content = ptr;
            }
          }
          
          sdfs_create_file(s_path);
          if (sdfs_write_file(s_path, (uint8_t *)content, strlen(content)) >= 0) {
            terminal_append_output("Arquivo '");
            terminal_append_output(args[1]);
            terminal_append_output("' criado com sucesso.\n");
          } else {
            terminal_append_output("Erro ao escrever arquivo.\n");
          }
      }
    }
  } else if (strcmp(cmd, "mkdir") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: mkdir <pasta>\n");
    } else {
      terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
      const char* s_path = terminal_get_sdfs_path(resolved_a);
      if (s_path && sdfs_create_dir(s_path) == 0) {
        terminal_append_output("Pasta criada.\n");
      } else {
        terminal_append_output("Falha ao criar pasta: Verifique se o caminho esta em /house/localhost.\n");
      }
    }
  } else if (strcmp(cmd, "rm") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: rm <arquivo>\n");
    } else {
      terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
      const char* s_path = terminal_get_sdfs_path(resolved_a);
      if (s_path && sdfs_delete(s_path) == 0) {
        terminal_append_output("Item removido.\n");
      } else {
        terminal_append_output("Falha ao remover item.\n");
      }
    }
  } else if (strcmp(cmd, "cp") == 0) {
    if (argc < 3) {
      terminal_append_output("Usage: cp <origem> <destino>\n");
    } else {
      terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
      terminal_join_filename(args[2], resolved_b, sizeof(resolved_b));
      if (terminal_copy_file(resolved_a, resolved_b, false) == 0) {
        terminal_append_output("Arquivo copiado.\n");
      } else {
        terminal_append_output("Falha ao copiar arquivo (apenas SDFS suportado).\n");
      }
    }
  } else if (strcmp(cmd, "mv") == 0) {
    if (argc < 3) {
      terminal_append_output("Usage: mv <origem> <destino>\n");
    } else {
      terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
      terminal_join_filename(args[2], resolved_b, sizeof(resolved_b));
      const char* s_a = terminal_get_sdfs_path(resolved_a);
      const char* s_b = terminal_get_sdfs_path(resolved_b);
      if (s_a && s_b && sdfs_rename(s_a, s_b) == 0) {
          terminal_append_output("Item movido.\n");
      } else if (terminal_copy_file(resolved_a, resolved_b, true) == 0) {
        terminal_append_output("Item movido.\n");
      } else {
        terminal_append_output("Falha ao mover item.\n");
      }
    }
  } else if (strcmp(cmd, "unzip") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: unzip <arquivo.zip>\n");
    } else {
      terminal_append_output("Descompactando via Zlib...\n");
      // TODO: Implementar lógica de extração real
    }
  } else if (strcmp(cmd, "zip") == 0) {
    if (argc < 3) {
      terminal_append_output("Usage: zip <saida.zip> <entrada>\n");
    } else {
      terminal_append_output("Compactando via Zlib...\n");
    }
  } else if (strcmp(cmd, "top") == 0) {
    terminal_open_top();
  } else if (strcmp(cmd, "ifconfig") == 0) {
    net_interface_t *netif = net_get_list();

    if (!netif) {
      terminal_append_output("Nenhuma interface de rede registrada.\n");
    } else {
      while (netif) {
        terminal_append_output(netif->name);
        terminal_append_output("  ");
        terminal_append_output(netif->type == NET_TYPE_ETHERNET ? "ethernet"
                                                                : "wifi");
        terminal_append_output("  mac ");
        terminal_append_mac(netif->mac);
        terminal_append_output("  ip ");
        terminal_append_ip(netstack_get_my_ip());
        terminal_append_output("\n");
        netif = netif->next;
      }
    }
  } else if (strcmp(cmd, "ping") == 0) {
    uint32_t ip;
    int count = 4;
    int sent = 0;
    int received = 0;
    uint32_t total_ms = 0;

    if (argc < 2) {
      terminal_append_output("Usage: ping <host|ip> [count]\n");
    } else if (!net_get_list()) {
      terminal_append_output("Rede indisponivel.\n");
    } else {
      if (argc >= 3) {
        count = 0;
        for (int i = 0; args[2][i] >= '0' && args[2][i] <= '9'; i++) {
          count = count * 10 + (args[2][i] - '0');
        }
        if (count <= 0) {
          count = 4;
        }
      }
      ip = net_resolve_host(args[1]);
      terminal_append_output("PING ");
      terminal_append_output(args[1]);
      terminal_append_output(" (");
      terminal_append_ip(ip);
      terminal_append_output(")\n");
      terminal_redraw();

      for (int i = 0; i < count; i++) {
        int elapsed_ticks;
        sent++;
        elapsed_ticks = netstack_ping(ip, 200);
        if (elapsed_ticks >= 0) {
          uint32_t elapsed_ms = (uint32_t)elapsed_ticks * 10U;
          received++;
          total_ms += elapsed_ms;
          terminal_append_output("64 bytes from ");
          terminal_append_ip(ip);
          terminal_append_output(": icmp_seq=");
          terminal_append_uint((uint32_t)(i + 1));
          terminal_append_output(" time=");
          terminal_append_uint(elapsed_ms);
          terminal_append_output(" ms\n");
        } else {
          terminal_append_output("Request timeout for icmp_seq=");
          terminal_append_uint((uint32_t)(i + 1));
          terminal_append_output("\n");
        }
        terminal_redraw();
      }

      terminal_append_output("--- ping statistics ---\n");
      terminal_append_uint((uint32_t)sent);
      terminal_append_output(" packets transmitted, ");
      terminal_append_uint((uint32_t)received);
      terminal_append_output(" received, ");
      terminal_append_uint((uint32_t)(sent - received));
      terminal_append_output(" lost\n");
      if (received > 0) {
        terminal_append_output("avg time = ");
        terminal_append_uint(total_ms / (uint32_t)received);
        terminal_append_output(" ms\n");
      }
    }
  } else if (strcmp(cmd, "wget") == 0) {
    static char response[16384];
    int got;

    if (argc < 2) {
      terminal_append_output("Usage: wget <url> [arquivo]\n");
    } else if (!net_get_list()) {
      terminal_append_output("Rede indisponivel.\n");
    } else if (strstr(args[1], "https://") == args[1]) {
      terminal_append_output(
          "HTTPS ainda nao e suportado. Use URLs http:// por enquanto.\n");
    } else {
      terminal_append_output("Baixando ");
      terminal_append_output(args[1]);
      terminal_append_output(" ...\n");
      terminal_redraw();

      memset(response, 0, sizeof(response));
      got = http_get_url(args[1], response, sizeof(response) - 1);
      if (got < 0) {
        terminal_append_output("Falha no download.\n");
      } else if (argc >= 3) {
        terminal_join_filename(args[2], resolved_a, sizeof(resolved_a));
        const char* s_path = terminal_get_sdfs_path(resolved_a);
        if (s_path) {
            sdfs_create_file(s_path);
            sdfs_write_file(s_path, (uint8_t *)response, (uint32_t)got);
            terminal_append_output("Salvo em ");
            terminal_append_output(resolved_a);
            terminal_append_output("\n");
        } else {
            terminal_append_output("Erro: wget so pode salvar em /house/localhost.\n");
        }
      } else {
        terminal_append_output("Download concluido: ");
        terminal_append_uint((uint32_t)got);
        terminal_append_output(" bytes recebidos com sucesso.\n");
        terminal_append_output("Status: CONTEUDO RECEBIDO\n");
      }
    }
  } else if (strcmp(cmd, "browser") == 0) {
    terminal_append_output(
        "GUI hibernada no modo terminal-only. Navegador indisponivel por agora.\n");
  } else if (strcmp(cmd, "liwfetch") == 0) {
    terminal_append_output(
        "LiwusOS Wayland Edition\nArchitecture: LGX Compositor\n");
  } else if (strcmp(cmd, "reboot") == 0) {
    sys_reboot();
  } else if (strcmp(cmd, "cat") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: cat <arquivo>\n");
    } else {
      terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
      fs_node_t *node = vfs_open(resolved_a);
      if (node) {
        uint8_t *buf = kmalloc(node->length + 1);
        uint32_t read = read_fs(node, 0, node->length, buf);
        terminal_append_output_n((const char *)buf, (int)read);
        terminal_append_output("\n");
        kfree(buf);
        kfree(node);
      } else {
        terminal_append_output("Erro ao ler arquivo.\n");
      }
    }
  } else if (strcmp(cmd, "unzip") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: unzip <arquivo.zip>\n");
    } else {
      terminal_append_output("Descompactando via Zlib...\n");
      // Lógica de unzip será integrada aqui
    }
  } else if (strcmp(cmd, "zip") == 0) {
    if (argc < 3) {
      terminal_append_output("Usage: zip <saida.zip> <entrada>\n");
    } else {
      terminal_append_output("Compactando via Zlib...\n");
    }
  } else if (strncmp(cmd, "./", 2) == 0) {
    char *ext = strrchr(cmd, '.');
    if (ext && (strcmp(ext, ".png") == 0 || strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0)) {
      terminal_append_output("Abrindo imagem...\n");
      // Aqui chamaremos o visualizador de imagens
      char view_cmd[256];
      strcpy(view_cmd, "view ");
      strcat(view_cmd, cmd + 2);
      exec_command_term(view_cmd);
    } else {
      terminal_append_output("Executando programa...\n");
      terminal_join_filename(cmd + 2, resolved_a, sizeof(resolved_a));
      launch_initrd_program(resolved_a);
    }
  } else if (strcmp(cmd, "format") == 0) {
    int progress = 0;
    terminal_append_output("Formatando disco C:/ (SDFS)...\n");
    if (sdfs_format() == 0) {
      terminal_append_output("Disco formatado com sucesso! Reinicie para montar.\n");
    } else {
      terminal_append_output("Falha ao formatar disco.\n");
    }
  } else if (strcmp(cmd, "doomgeneric") == 0 || strcmp(cmd, "doom") == 0) {
    terminal_append_output("Iniciando doomgeneric...\n");
    terminal_redraw();
    char *doom_argv[] = {"doomgeneric", "-iwad", "freedoom1.wad", NULL};
    if (launch_initrd_program_argv("doomgeneric", doom_argv) < 0) {
      terminal_append_output("Falha ao iniciar doomgeneric: ");
      terminal_append_output(get_launch_last_error());
      terminal_append_output("\n");
    }
  } else if (strcmp(cmd, "editor") == 0) {
    terminal_append_output("Iniciando editor...\n");
    terminal_redraw();
    char *editor_argv[16] = {"editor", NULL};
    int editor_argc = 1;
    for (int i = 1; i < argc && i < 15; i++) {
      editor_argv[editor_argc++] = args[i];
    }
    editor_argv[editor_argc] = NULL;
    if (launch_initrd_program_argv("editor", editor_argv) < 0) {
      terminal_append_output("Falha ao iniciar editor: ");
      terminal_append_output(get_launch_last_error());
      terminal_append_output("\n");
    }
  } else if (strcmp(cmd, "lua") == 0 || terminal_has_lua_suffix(cmd)) {
    char *lua_argv[4] = {"lua", NULL, NULL, NULL};

    if (strcmp(cmd, "lua") == 0) {
      if (argc >= 2) {
        terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
        lua_argv[1] = resolved_a;
      }
    } else {
      terminal_join_filename(cmd, resolved_a, sizeof(resolved_a));
      lua_argv[1] = resolved_a;
    }

    terminal_append_output("Executando Lua...\n");
    terminal_redraw();
    if (launch_initrd_program_argv("lua", lua_argv) < 0) {
      terminal_append_output("Falha ao iniciar Lua: ");
      terminal_append_output(get_launch_last_error());
      terminal_append_output("\n");
    }
  } else if (strcmp(cmd, "view") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: view <imagem.png/jpg>\n");
    } else {
      terminal_join_filename(args[1], resolved_a, sizeof(resolved_a));
      terminal_append_output("Abrindo imagem ");
      terminal_append_output(resolved_a);
      terminal_append_output("...\n");
      terminal_redraw();
      char *view_argv[] = {"view.liwpkg", resolved_a, NULL};
      int pid = launch_initrd_program_argv("view.liwpkg", view_argv);
      if (pid < 0) {
        terminal_append_output("Falha ao abrir imagem: ");
        terminal_append_output(get_launch_last_error());
        terminal_append_output("\n");
      } else {
        int status;
        sys_waitpid(pid, &status, 0);
        terminal_append_output("Visualizacao encerrada.\n");
        terminal_redraw();
      }
    }
  } else if (strcmp(cmd, "crun") == 0) {
    if (argc < 2) {
      terminal_append_output("Usage: crun <file.c>\n");
    } else {
      terminal_append_output("Compilando e rodando C nativo...\n");
      char *crun_argv[] = {"crun", args[1], NULL};
      launch_initrd_program_argv("crun", crun_argv);
    }
  } else if (strcmp(cmd, "liw") == 0 || strcmp(cmd, "exec") == 0) {
    // Handle "liw ..." directly or "exec liw ..."
    // If just "liw", we run passing the rest as args.
    // If "exec prog args...", we run prog with args.

    char *prog;
    char **sub_argv;
    if (strcmp(cmd, "exec") == 0) {
      if (argc < 2) {
        terminal_append_output("Usage: exec <program> [args]\n");
        terminal_redraw();
        return;
      }
      prog = args[1];
      sub_argv = &args[1]; // O programa e seus argumentos
    } else {
      // It is "liw"
      prog = "liw";
      sub_argv = args; // "liw" e os argumentos seguintes
    }

    // Call syscall
    terminal_append_output("Executing ");
    terminal_append_output(prog);
    terminal_append_output("...\n");
    terminal_redraw();

    int ret = launch_initrd_program_argv(prog, sub_argv);
    if (ret < 0) {
      terminal_append_output("Failed to execute: ");
      terminal_append_output(get_launch_last_error());
      terminal_append_output("\n");
    }
  } else {
    terminal_append_output("Comando desconhecido. Digite 'help'.\n");
  }

  // Se o último caractere da saída não for um \n, adicionamos um para o prompt não ficar grudado
  if (output_text[0] != '\0' && output_text[strlen(output_text) - 1] != '\n') {
    terminal_append_output("\n");
  }

  terminal_redraw();
}

void init_terminal_app() {
  terminal_reset_input();

  if (terminal_console_mode) {
    term_surface = NULL;
    term_win = NULL;
    terminal_output_dirty = true;
    terminal_redraw();
    return;
  }

  if (lvgl_shell_enabled()) {
    term_surface = NULL;
    term_win = NULL;
    terminal_output_dirty = true;
    return;
  }

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
  if (lvgl_shell_enabled()) {
    lvgl_shell_show_terminal();
    return;
  }
  if (!term_win)
    init_terminal_app();
  term_win->visible = true;
  term_win->is_focused = true;
}

static int terminal_editor_line_length(int y) {
  int cur_y = 0, i = 0, len = 0;
  while (editor_buffer[i]) {
    if (cur_y == y) {
      if (editor_buffer[i] == '\n') return len;
      len++;
    }
    if (editor_buffer[i] == '\n') cur_y++;
    i++;
  }
  return len;
}

static int terminal_editor_count_lines() {
  int lines = 1, i = 0;
  while (editor_buffer[i]) {
    if (editor_buffer[i] == '\n') lines++;
    i++;
  }
  return lines;
}

static int terminal_editor_get_index(int x, int y) {
  int cur_x = 0, cur_y = 0, i = 0;
  while (editor_buffer[i]) {
    if (cur_x == x && cur_y == y) return i;
    if (editor_buffer[i] == '\n') {
      if (cur_y == y) return i;
      cur_y++;
      cur_x = 0;
    } else {
      cur_x++;
    }
    i++;
  }
  return i;
}

static void terminal_autocomplete(void) {
  if (input_ptr == 0) return;

  // Pega a última palavra digitada
  int start = input_ptr - 1;
  while (start > 0 && input_buffer[start-1] != ' ') start--;
  
  char partial[64];
  strncpy(partial, &input_buffer[start], sizeof(partial)-1);
  partial[sizeof(partial)-1] = '\0';
  int partial_len = strlen(partial);

  fs_node_t *dir_node = vfs_open(terminal_cwd);
  if (!dir_node) return;

  struct dirent *de;
  int i = 0;
  char *match = NULL;
  int match_count = 0;

  while ((de = readdir_fs(dir_node, i++)) != NULL) {
    if (strncmp(de->name, partial, partial_len) == 0) {
      match = de->name;
      match_count++;
    }
  }

  // Se houver apenas UM match, completamos
  if (match_count == 1) {
    int to_add = strlen(match) - partial_len;
    for (int j = 0; j < to_add && input_ptr < (int)sizeof(input_buffer) - 1; j++) {
      char c = match[partial_len + j];
      input_buffer[input_ptr++] = c;
      if (terminal_console_mode) vga_putc(c);
    }
    input_buffer[input_ptr] = '\0';
  } else if (match_count > 1) {
    // Se houver vários, listamos (estilo bash)
    terminal_append_output("\n");
    i = 0;
    while ((de = readdir_fs(dir_node, i++)) != NULL) {
      if (strncmp(de->name, partial, partial_len) == 0) {
        terminal_append_output(de->name);
        terminal_append_output("  ");
      }
    }
    terminal_append_output("\n");
    terminal_append_output(prompt_line);
    terminal_append_output(input_buffer);
  }
}

void update_terminal_key(char k) {
  if (!term_surface && !lvgl_shell_enabled() && !terminal_console_mode)
    return;

  // Debug: Print scancode/char to serial
  serial_print("KBD: char=");
  write_serial(k >= 32 ? k : '.');
  serial_print(" hex=");
  char hexbuf[3];
  hex_to_str((uint8_t)k, hexbuf);
  serial_print(hexbuf);
  serial_print("\n");

  if (terminal_mode == TERM_MODE_EDITOR) {
    int total_len = (int)strlen(editor_buffer);
    if (k == 27) { editor_insert_mode = false; terminal_redraw(); return; }
    
    if (!editor_insert_mode) {
      if (k == 17) k = 'k'; // UP
      else if (k == 18) k = 'j'; // DOWN
      else if (k == 19) k = 'h'; // LEFT
      else if (k == 20) k = 'l'; // RIGHT

      if (editor_pending_cmd == 'd' && k == 'd') {
          // Deletar linha (dd)
          int start = terminal_editor_get_index(0, terminal_editor_cursor_y);
          int end = terminal_editor_get_index(0, terminal_editor_cursor_y + 1);
          memmove(&editor_buffer[start], &editor_buffer[end], total_len - end + 1);
          if (terminal_editor_cursor_y >= terminal_editor_count_lines()) 
              terminal_editor_cursor_y = terminal_editor_count_lines() - 1;
          terminal_editor_cursor_x = 0;
          editor_pending_cmd = 0;
          terminal_redraw();
          return;
      }
      if (editor_pending_cmd == 'y' && k == 'y') {
          // Copiar linha (yy)
          int start = terminal_editor_get_index(0, terminal_editor_cursor_y);
          int end = terminal_editor_get_index(0, terminal_editor_cursor_y + 1);
          int count = end - start;
          if (count > (int)sizeof(editor_clipboard) - 1) count = sizeof(editor_clipboard) - 1;
          memcpy(editor_clipboard, &editor_buffer[start], count);
          editor_clipboard[count] = '\0';
          editor_pending_cmd = 0;
          terminal_redraw();
          return;
      }
      editor_pending_cmd = 0;

      switch (k) {
        case 'i': editor_insert_mode = true; break;
        case 's': terminal_editor_save(); break;
        case 'q': terminal_editor_close(true); break;
        case 'd': editor_pending_cmd = 'd'; break;
        case 'y': editor_pending_cmd = 'y'; break;
        case 'p': {
            // Colar linha (p)
            if (editor_clipboard[0]) {
                int pos = terminal_editor_get_index(0, terminal_editor_cursor_y + 1);
                int clip_len = strlen(editor_clipboard);
                if (total_len + clip_len < (int)sizeof(editor_buffer) - 1) {
                    memmove(&editor_buffer[pos + clip_len], &editor_buffer[pos], total_len - pos + 1);
                    memcpy(&editor_buffer[pos], editor_clipboard, clip_len);
                    terminal_editor_cursor_y++;
                    terminal_editor_cursor_x = 0;
                }
            }
            break;
        }
        case 'w': { // Próxima palavra
            int idx = terminal_editor_get_index(terminal_editor_cursor_x, terminal_editor_cursor_y);
            while (editor_buffer[idx] && editor_buffer[idx] != ' ' && editor_buffer[idx] != '\n') idx++;
            while (editor_buffer[idx] && (editor_buffer[idx] == ' ' || editor_buffer[idx] == '\n')) idx++;
            // Atualizar x, y baseado no novo index
            int tx = 0, ty = 0;
            for(int i=0; i<idx; i++) {
                if (editor_buffer[i] == '\n') { ty++; tx = 0; }
                else tx++;
            }
            terminal_editor_cursor_x = tx; terminal_editor_cursor_y = ty;
            break;
        }
        case 'b': { // Palavra anterior
            int idx = terminal_editor_get_index(terminal_editor_cursor_x, terminal_editor_cursor_y);
            if (idx > 0) idx--;
            while (idx > 0 && (editor_buffer[idx] == ' ' || editor_buffer[idx] == '\n')) idx--;
            while (idx > 0 && editor_buffer[idx-1] != ' ' && editor_buffer[idx-1] != '\n') idx--;
            int tx = 0, ty = 0;
            for(int i=0; i<idx; i++) {
                if (editor_buffer[i] == '\n') { ty++; tx = 0; }
                else tx++;
            }
            terminal_editor_cursor_x = tx; terminal_editor_cursor_y = ty;
            break;
        }
        case 'h': if (terminal_editor_cursor_x > 0) terminal_editor_cursor_x--; break;
        case 'l': if (terminal_editor_cursor_x < terminal_editor_line_length(terminal_editor_cursor_y)) terminal_editor_cursor_x++; break;
        case 'j':
          if (terminal_editor_cursor_y < terminal_editor_count_lines() - 1) {
            terminal_editor_cursor_y++;
            int mx = terminal_editor_line_length(terminal_editor_cursor_y);
            if (terminal_editor_cursor_x > mx) terminal_editor_cursor_x = mx;
          }
          break;
        case 'k':
          if (terminal_editor_cursor_y > 0) {
            terminal_editor_cursor_y--;
            int mx = terminal_editor_line_length(terminal_editor_cursor_y);
            if (terminal_editor_cursor_x > mx) terminal_editor_cursor_x = mx;
          }
          break;
        case 'x': {
          int idx = terminal_editor_get_index(terminal_editor_cursor_x, terminal_editor_cursor_y);
          if (editor_buffer[idx])
            memmove(&editor_buffer[idx], &editor_buffer[idx+1], strlen(&editor_buffer[idx+1]) + 1);
          break;
        }
        case 'o': {
          int eol = terminal_editor_get_index(terminal_editor_line_length(terminal_editor_cursor_y), terminal_editor_cursor_y);
          memmove(&editor_buffer[eol+1], &editor_buffer[eol], total_len - eol + 1);
          editor_buffer[eol] = '\n';
          terminal_editor_cursor_y++;
          terminal_editor_cursor_x = 0;
          editor_insert_mode = true;
          break;
        }
        case 'A':
          terminal_editor_cursor_x = terminal_editor_line_length(terminal_editor_cursor_y);
          editor_insert_mode = true;
          break;
        case '0': terminal_editor_cursor_x = 0; break;
        case '$': terminal_editor_cursor_x = terminal_editor_line_length(terminal_editor_cursor_y); break;
        case 'G': terminal_editor_cursor_y = terminal_editor_count_lines() - 1; terminal_editor_cursor_x = 0; break;
        case 'g': terminal_editor_cursor_y = 0; terminal_editor_cursor_x = 0; break;
      }
      terminal_redraw();
      return;
    }
    
    // MODO INSERÇÃO
    int idx = terminal_editor_get_index(terminal_editor_cursor_x, terminal_editor_cursor_y);
    
    if (k == 17) { // UP
        if (terminal_editor_cursor_y > 0) {
            terminal_editor_cursor_y--;
            int mx = terminal_editor_line_length(terminal_editor_cursor_y);
            if (terminal_editor_cursor_x > mx) terminal_editor_cursor_x = mx;
        }
    } else if (k == 18) { // DOWN
        if (terminal_editor_cursor_y < terminal_editor_count_lines() - 1) {
            terminal_editor_cursor_y++;
            int mx = terminal_editor_line_length(terminal_editor_cursor_y);
            if (terminal_editor_cursor_x > mx) terminal_editor_cursor_x = mx;
        }
    } else if (k == 19) { // LEFT
        if (terminal_editor_cursor_x > 0) terminal_editor_cursor_x--;
        else if (terminal_editor_cursor_y > 0) {
            terminal_editor_cursor_y--;
            terminal_editor_cursor_x = terminal_editor_line_length(terminal_editor_cursor_y);
        }
    } else if (k == 20) { // RIGHT
        if (terminal_editor_cursor_x < terminal_editor_line_length(terminal_editor_cursor_y)) {
            terminal_editor_cursor_x++;
        } else if (terminal_editor_cursor_y < terminal_editor_count_lines() - 1) {
            terminal_editor_cursor_y++;
            terminal_editor_cursor_x = 0;
        }
    } else if (k == '\b') { // Backspace
      if (idx > 0) {
        if (terminal_editor_cursor_x > 0) {
          terminal_editor_cursor_x--;
        } else if (terminal_editor_cursor_y > 0) {
          terminal_editor_cursor_y--;
          terminal_editor_cursor_x = terminal_editor_line_length(terminal_editor_cursor_y);
        }
        memmove(&editor_buffer[idx-1], &editor_buffer[idx], total_len - idx + 1);
      }
    } else if (k == '\n' || k == '\r') {
      if (total_len < (int)sizeof(editor_buffer) - 1) {
        // Garante que o caractere inserido seja EXATAMENTE o newline que o C/Lua esperam
        memmove(&editor_buffer[idx+1], &editor_buffer[idx], total_len - idx + 1);
        editor_buffer[idx] = '\n';
        terminal_editor_cursor_y++;
        terminal_editor_cursor_x = 0;
      }
    } else if ((unsigned char)k >= 32 && (unsigned char)k <= 126) { // Caracteres Visíveis Apenas
      if (total_len < (int)sizeof(editor_buffer) - 1) {
        memmove(&editor_buffer[idx+1], &editor_buffer[idx], total_len - idx + 1);
        editor_buffer[idx] = k;
        terminal_editor_cursor_x++;
      }
    }
    terminal_redraw();
    return;
  }

  if (k == '\t') {
    terminal_autocomplete();
    terminal_redraw();
    return;
  }

  if (terminal_mode == TERM_MODE_TOP) {
    if (k == 'q' || k == 27) {
      terminal_mode = TERM_MODE_SHELL;
      terminal_append_output("top finalizado.\n");
      terminal_redraw();
    }
    return;
  }

  if (k == '\t') {
    terminal_autocomplete();
    terminal_redraw();
    return;
  }

  if (k == '\n') {
    terminal_append_output("\n");
    if (input_ptr > 0) {
      serial_print("EXEC: ");
      serial_print(input_buffer);
      serial_print("\n");
      exec_command_term(input_buffer);
    }
    terminal_reset_input();
    terminal_append_output(prompt_line);
  } else if (k == '\b') {
    if (input_ptr > 0) {
      input_ptr--;
      input_buffer[input_ptr] = '\0';
      if (terminal_console_mode) {
        vga_putc('\b');
      }
      terminal_output_dirty = true;
    }
  } else if ((unsigned char)k >= 32 && input_ptr < (int)sizeof(input_buffer) - 1) {
    input_buffer[input_ptr++] = k;
    input_buffer[input_ptr] = '\0';
    if (terminal_console_mode) {
      vga_putc(k);
    }
    terminal_output_dirty = true;
  }
}

void terminal_submit_line(const char *line) {
  if (!line || terminal_mode != TERM_MODE_SHELL) {
    return;
  }

  strncpy(input_buffer, line, sizeof(input_buffer) - 1);
  input_buffer[sizeof(input_buffer) - 1] = '\0';
  input_ptr = (int)strlen(input_buffer);
  terminal_format_prompt();
  terminal_append_output(prompt_line);
  terminal_append_output("\n");
  exec_command_term(input_buffer);
  terminal_reset_input();
}

const char *terminal_get_text_view(uint32_t now_ticks) {
  if (terminal_mode == TERM_MODE_SHELL) {
    return output_text;
  }

  terminal_text_view[0] = '\0';

  if (terminal_mode == TERM_MODE_EDITOR) {
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         "Liwim Console Edition | ");
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         editor_file_name);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view), "\n");
    terminal_view_append(
        terminal_text_view, sizeof(terminal_text_view),
        editor_insert_mode ? ">> MODO INSERCAO <<" : ">> MODO NORMAL (h/l move, i insere, s salva, q sai) <<");
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view), "\n\n");

    // Copia o buffer e insere o cursor visual
    int view_ptr = strlen(terminal_text_view);
    for(int i=0; i<(int)strlen(editor_buffer); i++) {
        if (i == editor_cursor_pos) {
            terminal_text_view[view_ptr++] = '[';
            terminal_text_view[view_ptr++] = editor_buffer[i];
            terminal_text_view[view_ptr++] = ']';
        } else {
            terminal_text_view[view_ptr++] = editor_buffer[i];
        }
        if (view_ptr >= (int)sizeof(terminal_text_view) - 5) break;
    }
    if (editor_cursor_pos == (int)strlen(editor_buffer)) {
        terminal_text_view[view_ptr++] = '_';
    }
    terminal_text_view[view_ptr] = '\0';
    
    return terminal_text_view;
  }

  if (terminal_mode == TERM_MODE_TOP) {
    task_info_t tasks[32];
    int task_count = task_snapshot(tasks, 32);
    uint32_t total_mem = pmm_get_total_memory();
    uint32_t used_mem = pmm_get_used_memory();
    uint32_t free_mem = pmm_get_free_memory();
    uint32_t total_ticks = timer_ticks ? timer_ticks : 1;
    uint32_t switch_total = task_total_switches();
    int running = 0, ready = 0, sleeping = 0, zombie = 0;

    terminal_live_last_tick = now_ticks;

    for (int i = 0; i < task_count; i++) {
      switch (tasks[i].state) {
      case TASK_RUNNING:
        running++;
        break;
      case TASK_READY:
        ready++;
        break;
      case TASK_SLEEPING:
        sleeping++;
        break;
      case TASK_ZOMBIE:
        zombie++;
        break;
      }
    }

    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         "LiwusOS top\n");
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         "Monitor do kernel em tempo real | q para sair\n\n");
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         "Tasks: ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              (uint32_t)task_count);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         " | RUN ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              (uint32_t)running);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         " READY ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              (uint32_t)ready);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         " SLEEP ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              (uint32_t)sleeping);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         " ZOMB ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              (uint32_t)zombie);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view), "\n");

    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         "Ticks: ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              timer_ticks);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         " | Switches: ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              switch_total);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view), "\n");

    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         "Mem used ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              used_mem / 1024);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         " KB | free ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              free_mem / 1024);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         " KB | total ");
    terminal_view_append_uint(terminal_text_view, sizeof(terminal_text_view),
                              total_mem / 1024);
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         " KB\n\n");
    terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                         "PID  PPID TYPE  STATE  CPU%  HEAPKB  NAME\n");

    for (int i = 0; i < task_count; i++) {
      uint32_t heap_bytes = tasks[i].heap_end > tasks[i].heap_start
                                ? tasks[i].heap_end - tasks[i].heap_start
                                : 0;
      uint32_t cpu_pct = (tasks[i].cpu_ticks * 100U) / total_ticks;
      char pid[16], ppid[16], cpu[16], heap[16];

      itoa(tasks[i].id, pid, 10);
      if (tasks[i].parent_id >= 0) {
        itoa(tasks[i].parent_id, ppid, 10);
      } else {
        strcpy(ppid, "-");
      }
      itoa((int)cpu_pct, cpu, 10);
      itoa((int)(heap_bytes / 1024), heap, 10);

      terminal_view_append(terminal_text_view, sizeof(terminal_text_view), pid);
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                           "    ");
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view), ppid);
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                           "   ");
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                           tasks[i].user_mode ? "USER  " : "KERN  ");
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                           task_state_name(tasks[i].state));
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                           "   ");
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view), cpu);
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                           "%   ");
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view), heap);
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                           "     ");
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view),
                           tasks[i].name[0] ? tasks[i].name : "?");
      terminal_view_append(terminal_text_view, sizeof(terminal_text_view), "\n");
    }
  }

  return terminal_text_view;
}

int terminal_get_mode(void) { return (int)terminal_mode; }
