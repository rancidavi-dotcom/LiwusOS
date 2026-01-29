#include "editor.h"
#include "sdfs.h"
#include "kheap.h"
#include "string.h"
#include "timer.h"
#include "video.h"

typedef enum {
  EDITOR_MODE_NORMAL = 0,
  EDITOR_MODE_INSERT = 1
} editor_mode_t;

typedef enum {
  EDITOR_FOCUS_FILES = 0,
  EDITOR_FOCUS_NAME  = 1,
  EDITOR_FOCUS_TEXT  = 2
} editor_focus_t;

typedef struct {
  char     name[32];
  uint32_t size;
  bool     is_dir;
  bool     is_parent;
} editor_entry_t;

static wl_surface_t  *editor_win = NULL;
static wl_buffer_t    editor_backing;
static editor_entry_t editor_entries[40];
static int            editor_entry_count    = 0;
static int            editor_selected_index = -1;
static editor_mode_t  editor_mode           = EDITOR_MODE_NORMAL;
static editor_focus_t editor_focus          = EDITOR_FOCUS_FILES;
static char           editor_path[128]      = "/";
static char           editor_name[32]       = "";
static char           editor_current_file[32] = "";
static char           editor_text[8192]     = "";
static char           editor_status[160]    = "Liwim pronto. Tecle n para novo arquivo.";

// Vim-like state
static int cursor_x = 0;
static int cursor_y = 0;
static int scroll_y = 0;

// ─────────────────────────────────────────────
//  Layout constants
// ─────────────────────────────────────────────
#define ED_CHAR_W    8
#define ED_LINE_H   16
#define ED_TOP_H    16   // título
#define ED_BOT_H    32   // status (16) + cmdline (16)
// Gutter: " 999 │ "  →  6 chars × 8px = 48px + separador 1px + 6px pad = 55px
#define ED_GUTTER_W 55
#define ED_GUTTER_SEP  (ED_GUTTER_W - 9)   // posição da barra │
#define ED_TEXT_OFF_X  (ED_GUTTER_W)        // texto começa aqui

// ─────────────────────────────────────────────
//  Paleta de cores (GitHub Dark–inspired)
// ─────────────────────────────────────────────
#define ED_BG          0xFF0D1117
#define ED_GUTTER_BG   0xFF161B22
#define ED_CURLINE_BG  0xFF1C2433
#define ED_SEP         0xFF21262D
#define ED_STATUSBAR   0xFF21262D
#define ED_CMDLINE     0xFF161B22

#define ED_TEXT        0xFFE6EDF3
#define ED_LINENUM     0xFF3D444D   // números de linha normais (escuros)
#define ED_LINENUM_CUR 0xFFCDD9E5   // número da linha atual (claro)
#define ED_TILDE       0xFF2D333B   // '~' depois do EOF
#define ED_MUTED       0xFF6E7681

#define ED_CURSOR_N    0xFF79C0FF   // cursor NORMAL  (bloco azul)
#define ED_CURSOR_I    0xFF56D364   // cursor INSERT  (barra verde)
#define ED_CURSOR_TXT  0xFF0D1117   // texto sobre cursor

#define ED_MODE_N      0xFFE3B341   // indicador NORMAL  (dourado)
#define ED_MODE_I      0xFF56D364   // indicador INSERT  (verde)

#define ED_ACCENT      0xFF79C0FF
#define ED_GREEN       0xFF56D364
#define ED_RED         0xFFF85149

// ─────────────────────────────────────────────
//  Utilitários internos
// ─────────────────────────────────────────────
static void editor_set_status(const char *text) {
  strncpy(editor_status, text ? text : "", sizeof(editor_status) - 1);
  editor_status[sizeof(editor_status) - 1] = '\0';
}

static bool editor_is_focused(void) {
  return editor_win && editor_win->visible && editor_win->is_focused;
}

static void editor_clear_document(void) {
  editor_name[0]         = '\0';
  editor_current_file[0] = '\0';
  editor_text[0]         = '\0';
}

static void editor_path_join(const char *name, char *out, uint32_t out_size) {
  uint32_t len;
  if (strcmp(editor_path, "/") == 0) {
    strcpy(out, "/");
    len = strlen(out);
    if (len < out_size - 1) {
      strncpy(out + len, name, out_size - len - 1);
      out[out_size - 1] = '\0';
    }
    return;
  }
  strncpy(out, editor_path, out_size - 1);
  out[out_size - 1] = '\0';
  len = strlen(out);
  if (len > 0 && out[len - 1] != '/') strcat(out, "/");
  len = strlen(out);
  if (len < out_size - 1) {
    strncpy(out + len, name, out_size - len - 1);
    out[out_size - 1] = '\0';
  }
}

static void editor_parent_path(char *out, uint32_t out_size) {
  int last_sep = -1;
  int len = strlen(editor_path);
  if (strcmp(editor_path, "/") == 0) { strcpy(out, "/"); return; }
  for (int i = 0; i < len; i++)
    if (editor_path[i] == '/') last_sep = i;
  if (last_sep <= 0) { strcpy(out, "/"); return; }
  if (last_sep >= (int)out_size) last_sep = (int)out_size - 1;
  memcpy(out, editor_path, last_sep);
  out[last_sep] = '\0';
}

static void editor_set_path(const char *path) {
  strncpy(editor_path, path ? path : "/", sizeof(editor_path) - 1);
  editor_path[sizeof(editor_path) - 1] = '\0';
  if (!editor_path[0]) strcpy(editor_path, "/");
}

// ─────────────────────────────────────────────
//  File operations (sem alteração)
// ─────────────────────────────────────────────
static void editor_refresh_entries(void) {
  editor_entry_count    = 0;
  editor_selected_index = -1;

  if (!sdfs_is_mounted()) {
    editor_set_status("Disco C:/ indisponivel.");
    return;
  }
  if (strcmp(editor_path, "/") != 0 && editor_entry_count < 40) {
    strcpy(editor_entries[editor_entry_count].name, "..");
    editor_entries[editor_entry_count].size      = 0;
    editor_entries[editor_entry_count].is_dir    = true;
    editor_entries[editor_entry_count].is_parent = true;
    editor_entry_count++;
  }
  for (int i = 0; i < 40 && editor_entry_count < 40; i++) {
    int      is_dir = 0;
    uint32_t size   = 0;
    if (!sdfs_list_dir_entry(editor_path, i,
                              editor_entries[editor_entry_count].name,
                              &is_dir, &size))
      break;
    editor_entries[editor_entry_count].size      = size;
    editor_entries[editor_entry_count].is_dir    = is_dir != 0;
    editor_entries[editor_entry_count].is_parent = false;
    editor_entry_count++;
  }
  if (editor_entry_count > 0) editor_selected_index = 0;
  if (editor_entry_count == 0) {
    editor_set_status("Diretorio vazio. Tecle n para novo arquivo.");
  } else {
    char msg[160] = "Caminho atual: ";
    strcat(msg, editor_path);
    editor_set_status(msg);
  }
}

static void editor_open_selected(void) {
  char     full_path[160];
  void    *data;
  uint32_t size = 0;

  if (editor_selected_index < 0 || editor_selected_index >= editor_entry_count)
    return;
  if (editor_entries[editor_selected_index].is_dir) {
    if (editor_entries[editor_selected_index].is_parent) {
      char parent[128];
      editor_parent_path(parent, sizeof(parent));
      editor_set_path(parent);
    } else {
      editor_path_join(editor_entries[editor_selected_index].name, full_path,
                       sizeof(full_path));
      editor_set_path(full_path);
    }
    editor_clear_document();
    editor_focus = EDITOR_FOCUS_FILES;
    editor_mode  = EDITOR_MODE_NORMAL;
    editor_refresh_entries();
    return;
  }
  editor_path_join(editor_entries[editor_selected_index].name, full_path,
                   sizeof(full_path));
  data = sdfs_read_file(full_path, &size);
  if (!data) { editor_set_status("Falha ao abrir arquivo."); return; }

  strncpy(editor_name, editor_entries[editor_selected_index].name,
          sizeof(editor_name) - 1);
  editor_name[sizeof(editor_name) - 1] = '\0';
  strncpy(editor_current_file, editor_entries[editor_selected_index].name,
          sizeof(editor_current_file) - 1);
  editor_current_file[sizeof(editor_current_file) - 1] = '\0';

  if (size >= sizeof(editor_text)) size = sizeof(editor_text) - 1;
  memcpy(editor_text, data, size);
  editor_text[size] = '\0';
  kfree(data);

  cursor_x     = 0;
  cursor_y     = 0;
  scroll_y     = 0;
  editor_focus = EDITOR_FOCUS_TEXT;
  editor_mode  = EDITOR_MODE_NORMAL;
  editor_set_status("Arquivo carregado. i: INSERT  s: salvar  q: sair");
}

static void editor_new_file(void) {
  editor_clear_document();
  strcpy(editor_name, "NOVO.TXT");
  editor_focus = EDITOR_FOCUS_NAME;
  editor_mode  = EDITOR_MODE_INSERT;
  editor_set_status("Novo arquivo: edite o nome e TAB para o texto.");
}

static void editor_save_file(void) {
  char full_path[160];
  if (!sdfs_is_mounted()) { editor_set_status("Disco C:/ indisponivel."); return; }
  if (!editor_name[0])     { editor_set_status("Defina um nome para salvar."); return; }
  editor_path_join(editor_name, full_path, sizeof(full_path));
  sdfs_create_file(full_path);
  sdfs_write_file(full_path, (uint8_t *)editor_text, strlen(editor_text));
  strncpy(editor_current_file, editor_name, sizeof(editor_current_file) - 1);
  editor_current_file[sizeof(editor_current_file) - 1] = '\0';
  editor_refresh_entries();
  editor_set_status("Arquivo salvo.");
}

// ─────────────────────────────────────────────
//  Helpers de texto (sem alteração)
// ─────────────────────────────────────────────
static int editor_get_index(int x, int y) {
  int cur_x = 0, cur_y = 0, i = 0;
  while (editor_text[i]) {
    if (cur_x == x && cur_y == y) return i;
    if (editor_text[i] == '\n') {
      if (cur_y == y) return i;
      cur_y++; cur_x = 0;
    } else { cur_x++; }
    i++;
  }
  return i;
}

static int editor_line_length(int y) {
  int cur_y = 0, i = 0, len = 0;
  while (editor_text[i]) {
    if (cur_y == y) {
      if (editor_text[i] == '\n') return len;
      len++;
    }
    if (editor_text[i] == '\n') cur_y++;
    i++;
  }
  return len;
}

static int editor_count_lines(void) {
  int lines = 1, i = 0;
  while (editor_text[i]) {
    if (editor_text[i] == '\n') lines++;
    i++;
  }
  return lines;
}

// ─────────────────────────────────────────────
//  Cursor: bloco em NORMAL, linha fina em INSERT
// ─────────────────────────────────────────────
static void editor_draw_cursor(int px, int py, char ch_under) {
  bool blink_on = (timer_ticks / 18) % 2 == 0;
  if (!blink_on) return;

  if (editor_mode == EDITOR_MODE_NORMAL) {
    // Bloco sólido (cobre o caractere)
    draw_rect(px, py, ED_CHAR_W, ED_LINE_H - 2, ED_CURSOR_N);
    if (ch_under && ch_under != '\n')
      draw_char(px, py, ch_under, ED_CURSOR_TXT);
  } else {
    // Barra fina vertical (INSERT mode)
    draw_rect(px, py, 2, ED_LINE_H - 2, ED_CURSOR_I);
  }
}

// ─────────────────────────────────────────────
//  Área de texto com gutter + linha atual
// ─────────────────────────────────────────────
static void editor_draw_text_block(int x, int y, int w, int h, const char *text,
                                   bool show_cursor) {
  int draw_y       = y + 3;
  int visible_rows = (h - 6) / ED_LINE_H;
  int total_lines  = editor_count_lines();

  // ── fundo do gutter ──────────────────────────────────────────────────
  draw_rect(x, y, ED_GUTTER_W, h, ED_GUTTER_BG);
  // separador │ (1px vertical)
  draw_rect(x + ED_GUTTER_SEP, y, 1, h, ED_SEP);

  // ── "~" em linhas além do fim do arquivo ─────────────────────────────
  int first_after = total_lines - scroll_y;  // primeira linha vazia (relativa)
  for (int rel = (first_after < 0 ? 0 : first_after); rel < visible_rows; rel++) {
    int sy = draw_y + rel * ED_LINE_H;
    // número de linha "vazio": apenas o ~
    draw_char(x + ED_GUTTER_SEP - ED_CHAR_W - 1, sy, '~', ED_TILDE);
  }

  // ── percorre o texto renderizando linha a linha ───────────────────────
  int row = 0, col = 0;
  bool need_linenum = true;  // precisa pintar número desta linha?
  const char *p = text;

  while (true) {
    // ---- cabeçalho da linha (número + highlight) ----------------------
    if (need_linenum) {
      need_linenum = false;
      if (row >= scroll_y && row < scroll_y + visible_rows) {
        int sy = draw_y + (row - scroll_y) * ED_LINE_H;

        // highlight linha atual (fundo)
        if (row == cursor_y)
          draw_rect(x + ED_GUTTER_W, sy, w - ED_GUTTER_W, ED_LINE_H, ED_CURLINE_BG);

        // número de linha (right-aligned dentro do gutter)
        char lnbuf[8];
        itoa(row + 1, lnbuf, 10);
        int  lnlen = strlen(lnbuf);
        int  nx    = x + ED_GUTTER_SEP - 4 - lnlen * ED_CHAR_W;
        uint32_t nc = (row == cursor_y) ? ED_LINENUM_CUR : ED_LINENUM;
        draw_string(nx, sy, lnbuf, nc);
      }
    }

    // ---- fim do buffer -------------------------------------------------
    if (!*p) break;

    // ---- renderiza caractere ------------------------------------------
    if (row >= scroll_y && row < scroll_y + visible_rows) {
      int sy  = draw_y + (row - scroll_y) * ED_LINE_H;
      int px  = x + ED_TEXT_OFF_X + col * ED_CHAR_W;

      if (*p == '\n') {
        // cursor no final da linha (antes do \n)
        if (show_cursor && row == cursor_y && col == cursor_x)
          editor_draw_cursor(px, sy, 0);
        row++;
        col = 0;
        need_linenum = true;
      } else {
        // cursor atrás do caractere
        if (show_cursor && row == cursor_y && col == cursor_x)
          editor_draw_cursor(px, sy, *p);

        // texto (só desenha se couber)
        if (px + ED_CHAR_W <= x + w - 2)
          draw_char(px, sy, *p, ED_TEXT);

        col++;
      }
    } else {
      if (*p == '\n') { row++; col = 0; need_linenum = true; }
      else            { col++; }
    }
    p++;
  }

  // ---- cursor no fim do arquivo ---------------------------------------
  if (show_cursor && row == cursor_y && col == cursor_x) {
    int sy = draw_y + (row - scroll_y) * ED_LINE_H;
    if (sy >= y && sy < y + h - ED_LINE_H) {
      int px = x + ED_TEXT_OFF_X + col * ED_CHAR_W;
      editor_draw_cursor(px, sy, 0);
    }
  }
}

// ─────────────────────────────────────────────
//  Barra de status (estilo Vim)
// ─────────────────────────────────────────────
static void editor_draw_statusbar(int y, int w) {
  draw_rect(0, y, w, ED_LINE_H, ED_STATUSBAR);

  // Nome do arquivo + flag de modificação
  char fname[64] = "";
  if (editor_name[0]) {
    strncpy(fname, editor_name, sizeof(fname) - 1);
  } else {
    strcpy(fname, "[Novo Arquivo]");
  }
  draw_string(8, y, fname, ED_TEXT);

  // Percentual de progresso no arquivo
  int   total  = editor_count_lines();
  int   pct    = (total > 1) ? (cursor_y * 100 / (total - 1)) : 100;
  char  pctbuf[16];
  if (pct == 0)       strcpy(pctbuf, "Topo");
  else if (pct == 100) strcpy(pctbuf, "Fim");
  else { itoa(pct, pctbuf, 10); strcat(pctbuf, "%%"); }

  // Linha,Coluna
  char  posbuf[24];
  itoa(cursor_y + 1, posbuf, 10);
  strcat(posbuf, ",");
  char colbuf[8];
  itoa(cursor_x + 1, colbuf, 10);
  strcat(posbuf, colbuf);

  // Alinha à direita: "  42,10   50%  "
  // Desenha posição a ~120px da direita, percentual a ~60px
  draw_string(w - 128, y, posbuf,  ED_MUTED);
  draw_string(w -  56, y, pctbuf,  ED_MUTED);
}

// ─────────────────────────────────────────────
//  Linha de comando (fundo) – modo + mensagem
// ─────────────────────────────────────────────
static void editor_draw_cmdline(int y, int w) {
  draw_rect(0, y, w, ED_LINE_H, ED_CMDLINE);

  if (editor_focus == EDITOR_FOCUS_NAME) {
    // Campo de nome sendo editado
    draw_string(8, y, "Nome do arquivo: ", ED_MUTED);
    draw_string(8 + 17 * ED_CHAR_W, y, editor_name, ED_TEXT);
    // cursor no nome
    if ((timer_ticks / 18) % 2 == 0) {
      int cx = 8 + (17 + (int)strlen(editor_name)) * ED_CHAR_W;
      draw_rect(cx, y + 1, 2, ED_LINE_H - 2, ED_CURSOR_I);
    }
    return;
  }

  if (editor_mode == EDITOR_MODE_INSERT) {
    draw_string(8, y, "-- INSERT --", ED_MODE_I);
  } else {
    // Em NORMAL: mostra a mensagem de status (como o Vim mostra echoes)
    draw_string(8, y, editor_status, ED_MUTED);
  }
}

// ─────────────────────────────────────────────
//  Redraw principal
// ─────────────────────────────────────────────
static void editor_redraw(void) {
  if (!editor_win) return;
  video_set_target(editor_backing.pixels, editor_backing.width,
                   editor_backing.height);

  int W = editor_backing.width;
  int H = editor_backing.height;

  // ── 1. Fundo ────────────────────────────────────────────────────────
  draw_rect(0, 0, W, H, ED_BG);

  // ── 2. Barra de título (linha 0) ────────────────────────────────────
  draw_rect(0, 0, W, ED_TOP_H, ED_GUTTER_BG);
  draw_string(8, 0, "Liwim", ED_ACCENT);
  draw_string(8 + 6 * ED_CHAR_W, 0, "—", ED_MUTED);
  draw_string(8 + 8 * ED_CHAR_W, 0,
              editor_name[0] ? editor_name : "sem título", ED_TEXT);
  // separador inferior do título
  draw_rect(0, ED_TOP_H - 1, W, 1, ED_SEP);

  // ── 3. Área de texto ────────────────────────────────────────────────
  int text_y = ED_TOP_H;
  int text_h = H - ED_TOP_H - ED_BOT_H;

  // garante foco no texto se não estiver no nome
  if (editor_focus == EDITOR_FOCUS_FILES)
    editor_focus = EDITOR_FOCUS_TEXT;

  editor_draw_text_block(0, text_y, W, text_h, editor_text,
                         editor_focus == EDITOR_FOCUS_TEXT);

  // ── 4. Barra de status (penúltima linha) ───────────────────────────
  int status_y = H - ED_BOT_H;
  editor_draw_statusbar(status_y, W);

  // ── 5. Linha de comando (última linha) ─────────────────────────────
  int cmd_y = H - ED_LINE_H;
  editor_draw_cmdline(cmd_y, W);

  video_reset_target();
  wl_commit(editor_win);
}

// ─────────────────────────────────────────────
//  Init / open
// ─────────────────────────────────────────────
widget_t *init_editor(void) {
  if (editor_win) return editor_win;

  editor_backing.width  = 980;
  editor_backing.height = 600;
  editor_backing.pixels =
      (uint32_t *)kmalloc(editor_backing.width * editor_backing.height * 4);
  editor_backing.shm = true;

  editor_win = wl_create_surface(editor_backing.width, editor_backing.height,
                                  WL_SURFACE_TOPLEVEL);
  editor_win->x       = 70;
  editor_win->y       = 70;
  editor_win->visible = false;
  strcpy(editor_win->title, "Liwim");
  wl_attach_buffer(editor_win, &editor_backing);

  editor_set_path("/");
  editor_refresh_entries();
  editor_redraw();
  return editor_win;
}

void editor_click_handler(int rx, int ry) {
  if (!editor_win) return;
  (void)rx; (void)ry;
  editor_win->is_focused = true;
  editor_win->visible    = true;
  editor_focus           = EDITOR_FOCUS_TEXT;
  editor_redraw();
}

// ─────────────────────────────────────────────
//  Input handler
// ─────────────────────────────────────────────
void update_editor_key(char k) {
  if (!editor_is_focused() || !k) return;

  if (k == 27) { // ESC
    editor_mode = EDITOR_MODE_NORMAL;
    editor_redraw();
    return;
  }

  if (editor_mode == EDITOR_MODE_NORMAL) {
    switch (k) {
    case 'i':
      editor_mode = EDITOR_MODE_INSERT;
      break;
    case 'h': if (cursor_x > 0) cursor_x--; break;
    case 'l': if (cursor_x < editor_line_length(cursor_y)) cursor_x++; break;
    case 'j':
      if (cursor_y < editor_count_lines() - 1) {
        cursor_y++;
        int mx = editor_line_length(cursor_y);
        if (cursor_x > mx) cursor_x = mx;
      }
      break;
    case 'k':
      if (cursor_y > 0) {
        cursor_y--;
        int mx = editor_line_length(cursor_y);
        if (cursor_x > mx) cursor_x = mx;
      }
      break;
    case 'x': {
      int idx = editor_get_index(cursor_x, cursor_y);
      if (editor_text[idx])
        memmove(&editor_text[idx], &editor_text[idx + 1],
                strlen(&editor_text[idx + 1]) + 1);
      break;
    }
    case 'o': {
      int idx = editor_get_index(0, cursor_y + 1);
      // Encontra fim da linha atual
      int eol = editor_get_index(editor_line_length(cursor_y), cursor_y);
      memmove(&editor_text[eol + 1], &editor_text[eol],
              strlen(&editor_text[eol]) + 1);
      editor_text[eol] = '\n';
      cursor_y++;
      cursor_x         = 0;
      editor_mode      = EDITOR_MODE_INSERT;
      (void)idx;
      break;
    }
    case 'A':
      cursor_x    = editor_line_length(cursor_y);
      editor_mode = EDITOR_MODE_INSERT;
      break;
    case '0': cursor_x = 0; break;
    case '$': cursor_x = editor_line_length(cursor_y); break;
    case 'G': cursor_y = editor_count_lines() - 1; cursor_x = 0; break;
    case 'g': cursor_y = 0; cursor_x = 0; break;
    case 'q': editor_win->visible = false; break;
    case 's': editor_save_file(); break;
    case 'n': editor_new_file(); break;
    case '\n': editor_open_selected(); break;
    case '\t': editor_focus = (editor_focus + 1) % 3; break;
    }

    // auto-scroll
    if (cursor_y < scroll_y) scroll_y = cursor_y;
    if (cursor_y >= scroll_y + 20) scroll_y = cursor_y - 19;

    editor_redraw();
    return;
  }

  // ── INSERT MODE ──────────────────────────────────────────────────────
  if (k == '\t') {
    editor_focus = (editor_focus + 1) % 3;
    editor_redraw();
    return;
  }

  if (editor_focus == EDITOR_FOCUS_NAME) {
    size_t len = strlen(editor_name);
    if (k == '\b') {
      if (len > 0) editor_name[len - 1] = '\0';
    } else if (k >= 32 && k <= 126 && len < sizeof(editor_name) - 1) {
      editor_name[len]     = k;
      editor_name[len + 1] = '\0';
    }
  } else { // EDITOR_FOCUS_TEXT
    int idx       = editor_get_index(cursor_x, cursor_y);
    int total_len = strlen(editor_text);

    if (k == '\b') {
      if (idx > 0) {
        if (cursor_x > 0)      cursor_x--;
        else if (cursor_y > 0) { cursor_y--; cursor_x = editor_line_length(cursor_y); }
        memmove(&editor_text[idx - 1], &editor_text[idx], total_len - idx + 1);
      }
    } else if (total_len < (int)sizeof(editor_text) - 1) {
      memmove(&editor_text[idx + 1], &editor_text[idx], total_len - idx + 1);
      editor_text[idx] = (k == '\n') ? '\n' : k;
      if (k == '\n') { cursor_y++; cursor_x = 0; }
      else           { cursor_x++; }
    }
  }

  // auto-scroll
  if (cursor_y < scroll_y) scroll_y = cursor_y;
  if (cursor_y >= scroll_y + 20) scroll_y = cursor_y - 19;

  editor_redraw();
}

wl_surface_t *get_editor_surface(void) { return editor_win; }

void open_editor(void) {
  if (!editor_win) init_editor();
  editor_win->visible    = true;
  editor_win->is_focused = true;
  if (!editor_current_file[0]) editor_mode = EDITOR_MODE_INSERT;
  editor_focus = EDITOR_FOCUS_TEXT;
  editor_redraw();
}

void open_editor_with_file(const char *path) {
  if (!editor_win) init_editor();

  uint32_t size = 0;
  void *data = sdfs_read_file(path, &size);

  // Limpa estado anterior
  editor_text[0] = '\0';
  cursor_x = 0; cursor_y = 0; scroll_y = 0;

  if (data) {
    if (size >= sizeof(editor_text)) size = sizeof(editor_text) - 1;
    memcpy(editor_text, data, size);
    editor_text[size] = '\0';
    kfree(data);

    // Extrair apenas o nome para exibir no título
    const char *last_slash = strrchr(path, '/');
    const char *name = last_slash ? last_slash + 1 : path;
    strncpy(editor_name, name, sizeof(editor_name)-1);
    strncpy(editor_current_file, path, sizeof(editor_current_file)-1);

    editor_mode = EDITOR_MODE_NORMAL;
    editor_set_status("Arquivo aberto com sucesso.");
  } else {
    // Se o arquivo não existe, tratamos como novo com esse nome
    const char *last_slash = strrchr(path, '/');
    const char *name = last_slash ? last_slash + 1 : path;
    strncpy(editor_name, name, sizeof(editor_name)-1);
    strncpy(editor_current_file, path, sizeof(editor_current_file)-1);
    editor_mode = EDITOR_MODE_INSERT;
    editor_set_status("Novo arquivo.");
  }

  editor_win->visible = true;
  editor_win->is_focused = true;
  editor_focus = EDITOR_FOCUS_TEXT;
  editor_redraw();
}