#include "explorer.h"
#include "sdfs.h"
#include "initrd.h"
#include "kheap.h"
#include "string.h"
#include "video.h"

wl_surface_t *explorer_win = NULL;

typedef enum {
  EXPLORER_VIEW_HOME = 0,
  EXPLORER_VIEW_BROWSER = 1
} explorer_view_t;

typedef enum {
  EXPLORER_SOURCE_INITRD = 0,
  EXPLORER_SOURCE_DISK = 1
} explorer_source_t;

typedef enum {
  EXPLORER_FIELD_NONE = 0,
  EXPLORER_FIELD_NAME = 1,
  EXPLORER_FIELD_CONTENT = 2
} explorer_field_t;

typedef struct {
  char name[32];
  uint32_t size;
  bool is_dir;
  bool is_parent;
} explorer_entry_t;

static wl_buffer_t explorer_buffer;
static explorer_view_t current_view = EXPLORER_VIEW_HOME;
static explorer_source_t current_source = EXPLORER_SOURCE_DISK;
static explorer_field_t active_field = EXPLORER_FIELD_NONE;
static explorer_entry_t entries[32];
static int entry_count = 0;
static int selected_index = -1;
static char selected_name[32];
static char name_field[32];
static char editor_buffer[4096];
static char current_disk_path[128] = "/";
static char status_line[128] = "Explorer pronto.";

#define EX_BG 0xFF111315
#define EX_HEADER 0xFF17191C
#define EX_PANEL 0xFF1B1E22
#define EX_PANEL_ALT 0xFF23272C
#define EX_PANEL_SOFT 0xFF2A2F35
#define EX_BORDER 0xFF333941
#define EX_TEXT 0xFFF4F6F8
#define EX_MUTED 0xFF9CA5AF
#define EX_BLUE 0xFF4D84E2
#define EX_BLUE_SOFT 0xFF263140
#define EX_BLUE_BRIGHT 0xFF77A5FF
#define EX_GREEN 0xFF5A9C6A
#define EX_GREEN_SOFT 0xFF242D26
#define EX_AMBER 0xFFAA8A54
#define EX_AMBER_SOFT 0xFF312B22
#define EX_RED 0xFFB06161
#define EX_RED_SOFT 0xFF342526
#define EX_DARK 0xFF131518
#define EX_SIDEBAR_ACTIVE 0xFF2A2E34
#define EX_CARD 0xFF202328
#define EX_CARD_HIGHLIGHT 0xFF2A2F35

static void explorer_set_status(const char *text) {
  strncpy(status_line, text ? text : "", sizeof(status_line) - 1);
  status_line[sizeof(status_line) - 1] = '\0';
}

static bool explorer_is_focused(void) {
  return explorer_win && explorer_win->visible && explorer_win->is_focused;
}

static void explorer_clear_editor(void) {
  selected_index = -1;
  selected_name[0] = '\0';
  name_field[0] = '\0';
  editor_buffer[0] = '\0';
}

static void explorer_focus(void) {
  if (explorer_win) {
    explorer_win->visible = true;
    explorer_win->is_focused = true;
  }
}

static void explorer_sanitize_copy(char *dst, uint32_t dst_size, const uint8_t *src,
                                   uint32_t src_size) {
  uint32_t limit = src_size;

  if (limit >= dst_size) {
    limit = dst_size - 1;
  }

  for (uint32_t i = 0; i < limit; i++) {
    char ch = (char)src[i];
    if (ch == '\r') {
      dst[i] = ' ';
    } else if ((unsigned char)ch < 32 && ch != '\n' && ch != '\t') {
      dst[i] = '.';
    } else {
      dst[i] = ch;
    }
  }

  dst[limit] = '\0';
}

static void explorer_set_disk_path(const char *path) {
  strncpy(current_disk_path, path ? path : "/", sizeof(current_disk_path) - 1);
  current_disk_path[sizeof(current_disk_path) - 1] = '\0';
  if (!current_disk_path[0]) {
    strcpy(current_disk_path, "/");
  }
}

static const char *explorer_source_title(explorer_source_t source) {
  return source == EXPLORER_SOURCE_DISK ? "Disco Local (C:)"
                                        : "Sistema Live (X:)";
}

static const char *explorer_source_subtitle(explorer_source_t source) {
  return source == EXPLORER_SOURCE_DISK ? "Arquivos persistentes FAT32"
                                        : "Conteudo da imagem live";
}

static const char *explorer_source_hint(explorer_source_t source) {
  return source == EXPLORER_SOURCE_DISK ? "Leitura e gravacao"
                                        : "Somente leitura";
}

static uint32_t explorer_source_accent(explorer_source_t source) {
  return source == EXPLORER_SOURCE_DISK ? EX_GREEN : EX_BLUE;
}

static uint32_t explorer_source_tint(explorer_source_t source) {
  return source == EXPLORER_SOURCE_DISK ? EX_GREEN_SOFT : EX_BLUE_SOFT;
}

static void explorer_format_path_label(char *out, uint32_t out_size) {
  uint32_t out_len = 0;
  const char *prefix = current_source == EXPLORER_SOURCE_DISK ? "C:\\" : "X:\\";
  const char *path = current_source == EXPLORER_SOURCE_DISK ? current_disk_path : "/";

  strncpy(out, prefix, out_size - 1);
  out[out_size - 1] = '\0';
  out_len = strlen(out);

  if (strcmp(path, "/") == 0) {
    return;
  }

  for (uint32_t i = 1; path[i] && out_len < out_size - 1; i++) {
    out[out_len++] = path[i] == '/' ? '\\' : path[i];
  }
  out[out_len] = '\0';
}

static void explorer_disk_join_path(const char *name, char *out,
                                    uint32_t out_size) {
  uint32_t out_len;

  if (strcmp(current_disk_path, "/") == 0) {
    strcpy(out, "/");
    out_len = strlen(out);
    if (out_len < out_size - 1) {
      strncpy(out + out_len, name, out_size - out_len - 1);
      out[out_size - 1] = '\0';
    }
    return;
  }

  strncpy(out, current_disk_path, out_size - 1);
  out[out_size - 1] = '\0';
  if (out[strlen(out) - 1] != '/') {
    strcat(out, "/");
  }
  out_len = strlen(out);
  if (out_len < out_size - 1) {
    strncpy(out + out_len, name, out_size - out_len - 1);
    out[out_size - 1] = '\0';
  }
}

static void explorer_disk_parent_path(char *out, uint32_t out_size) {
  int last_sep = -1;
  int len = strlen(current_disk_path);

  if (strcmp(current_disk_path, "/") == 0) {
    strcpy(out, "/");
    return;
  }

  for (int i = 0; i < len; i++) {
    if (current_disk_path[i] == '/') {
      last_sep = i;
    }
  }

  if (last_sep <= 0) {
    strcpy(out, "/");
    return;
  }

  if (last_sep >= (int)out_size) {
    last_sep = (int)out_size - 1;
  }
  memcpy(out, current_disk_path, last_sep);
  out[last_sep] = '\0';
}

static void explorer_switch_to_browser(explorer_source_t source) {
  current_view = EXPLORER_VIEW_BROWSER;
  current_source = source;
  active_field = EXPLORER_FIELD_NONE;
  explorer_clear_editor();
  if (source == EXPLORER_SOURCE_DISK && !current_disk_path[0]) {
    explorer_set_disk_path("/");
  }
}

static void explorer_enter_home(void) {
  current_view = EXPLORER_VIEW_HOME;
  active_field = EXPLORER_FIELD_NONE;
  explorer_clear_editor();
  explorer_set_status("Escolha um local para navegar.");
}

static void explorer_refresh_entries(void) {
  entry_count = 0;

  if (current_source == EXPLORER_SOURCE_INITRD) {
    for (int i = 0; i < 32; i++) {
      char *name = initrd_list_files(i);
      uint32_t size = 0;

      if (!name) {
        break;
      }
      if (strcmp(name, "./") == 0) {
        continue;
      }

      strncpy(entries[entry_count].name,
              (name[0] == '.' && name[1] == '/') ? name + 2 : name,
              sizeof(entries[entry_count].name) - 1);
      entries[entry_count].name[sizeof(entries[entry_count].name) - 1] = '\0';
      initrd_get_file(name, &size);
      entries[entry_count].size = size;
      entries[entry_count].is_dir = false;
      entries[entry_count].is_parent = false;
      entry_count++;
    }
    explorer_set_status("Sistema live carregado em modo somente leitura.");
    return;
  }

  if (!sdfs_is_mounted()) {
    explorer_set_status("Disco FAT32 nao montado.");
    return;
  }

  if (strcmp(current_disk_path, "/") != 0 && entry_count < 32) {
    strcpy(entries[entry_count].name, "..");
    entries[entry_count].size = 0;
    entries[entry_count].is_dir = true;
    entries[entry_count].is_parent = true;
    entry_count++;
  }

  for (int i = 0; i < 32; i++) {
    int is_dir = 0;
    uint32_t size = 0;

    if (entry_count >= 32) {
      break;
    }
    if (!sdfs_list_dir_entry(current_disk_path, i, entries[entry_count].name,
                              &is_dir, &size)) {
      break;
    }

    entries[entry_count].size = size;
    entries[entry_count].is_dir = is_dir != 0;
    entries[entry_count].is_parent = false;
    entry_count++;
  }

  if (entry_count == 0) {
    explorer_set_status("Disco local vazio.");
  } else {
    char path_label[64];
    strcpy(path_label, "Conteudo em ");
    strcat(path_label, current_disk_path);
    explorer_set_status(path_label);
  }
}

static void explorer_load_selected(void) {
  void *data = NULL;
  uint32_t size = 0;
  char full_path[160];

  if (selected_index < 0 || selected_index >= entry_count) {
    return;
  }

  strcpy(selected_name, entries[selected_index].name);
  strcpy(name_field, entries[selected_index].name);

  if (entries[selected_index].is_dir) {
    if (current_source == EXPLORER_SOURCE_DISK) {
      if (entries[selected_index].is_parent) {
        char parent[128];
        explorer_disk_parent_path(parent, sizeof(parent));
        explorer_set_disk_path(parent);
      } else {
        explorer_disk_join_path(entries[selected_index].name, full_path,
                                sizeof(full_path));
        explorer_set_disk_path(full_path);
      }
      explorer_clear_editor();
      active_field = EXPLORER_FIELD_NONE;
      explorer_refresh_entries();
      return;
    }

    strcpy(editor_buffer, "[Diretorio]");
    explorer_set_status("Diretorio do sistema selecionado.");
    active_field = EXPLORER_FIELD_NONE;
    return;
  }

  if (current_source == EXPLORER_SOURCE_INITRD) {
    data = initrd_get_file(entries[selected_index].name, &size);
    if (!data) {
      char prefixed[40];
      strcpy(prefixed, "./");
      strcat(prefixed, entries[selected_index].name);
      data = initrd_get_file(prefixed, &size);
    }
  } else {
    explorer_disk_join_path(entries[selected_index].name, full_path,
                            sizeof(full_path));
    data = sdfs_read_file(full_path, &size);
  }

  if (!data) {
    editor_buffer[0] = '\0';
    explorer_set_status("Falha ao abrir arquivo.");
    return;
  }

  explorer_sanitize_copy(editor_buffer, sizeof(editor_buffer),
                         (const uint8_t *)data, size);
  if (current_source == EXPLORER_SOURCE_DISK) {
    kfree(data);
  }
  active_field = EXPLORER_FIELD_CONTENT;
  explorer_set_status("Arquivo carregado.");
}

static void explorer_save(void) {
  char full_path[160];

  if (current_source != EXPLORER_SOURCE_DISK) {
    explorer_set_status("Arquivos do sistema sao somente leitura.");
    return;
  }
  if (!sdfs_is_mounted()) {
    explorer_set_status("Disco FAT32 indisponivel.");
    return;
  }
  if (!name_field[0]) {
    explorer_set_status("Defina um nome de arquivo antes de salvar.");
    return;
  }

  explorer_disk_join_path(name_field, full_path, sizeof(full_path));
  sdfs_create_file(full_path);
  sdfs_write_file(full_path, (uint8_t *)editor_buffer,
                        strlen(editor_buffer));
  strcpy(selected_name, name_field);
  explorer_refresh_entries();
  explorer_set_status("Arquivo salvo no disco local.");
}

static void explorer_delete(void) {
  char full_path[160];

  if (current_source != EXPLORER_SOURCE_DISK) {
    explorer_set_status("So e possivel excluir no disco local.");
    return;
  }
  if (!selected_name[0]) {
    explorer_set_status("Selecione um item para excluir.");
    return;
  }

  explorer_disk_join_path(selected_name, full_path, sizeof(full_path));
  if (sdfs_delete(full_path) == 0) {
    explorer_clear_editor();
    explorer_refresh_entries();
    explorer_set_status("Item excluido.");
  } else {
    explorer_set_status("Falha ao excluir item.");
  }
}

static void explorer_rename(void) {
  char old_path[160];
  char new_path[160];

  if (current_source != EXPLORER_SOURCE_DISK) {
    explorer_set_status("So e possivel renomear no disco local.");
    return;
  }
  if (!selected_name[0] || !name_field[0]) {
    explorer_set_status("Selecione um arquivo e informe o novo nome.");
    return;
  }
  if (strcmp(selected_name, name_field) == 0) {
    explorer_set_status("O nome novo e igual ao atual.");
    return;
  }

  explorer_disk_join_path(selected_name, old_path, sizeof(old_path));
  explorer_disk_join_path(name_field, new_path, sizeof(new_path));
  if (sdfs_rename(old_path, new_path) == 0) {
    strcpy(selected_name, name_field);
    explorer_refresh_entries();
    explorer_set_status("Item renomeado.");
  } else {
    explorer_set_status("Falha ao renomear item.");
  }
}

static void explorer_new_file(void) {
  explorer_clear_editor();
  strcpy(name_field, "NOVO.TXT");
  editor_buffer[0] = '\0';
  active_field = EXPLORER_FIELD_CONTENT;
  explorer_set_status("Novo arquivo criado. Digite o conteudo e salve.");
}

static void explorer_new_folder(void) {
  char full_path[160];
  const char *folder_name = name_field[0] ? name_field : "NOVA";

  if (current_source != EXPLORER_SOURCE_DISK) {
    explorer_set_status("Pastas so podem ser criadas no disco local.");
    return;
  }

  explorer_disk_join_path(folder_name, full_path, sizeof(full_path));
  if (sdfs_create_dir(full_path) == 0) {
    explorer_refresh_entries();
    explorer_set_status("Pasta criada.");
  } else {
    explorer_set_status("Falha ao criar pasta. Use nome 8.3 livre.");
  }
}

static void explorer_draw_text_block(int x, int y, int w, int h, const char *text,
                                     uint32_t color) {
  int cx = x;
  int cy = y;

  while (*text && cy <= y + h - 16) {
    if (*text == '\n') {
      cx = x;
      cy += 16;
    } else {
      if (cx > x + w - 8) {
        cx = x;
        cy += 16;
      }
      if (cy <= y + h - 16) {
        draw_char(cx, cy, *text, color);
      }
      cx += 8;
    }
    text++;
  }
}

static void explorer_format_size(uint32_t size, char *out, uint32_t out_size) {
  if (size >= 1024 * 1024) {
    itoa((int)(size / (1024 * 1024)), out, 10);
    strcat(out, " MB");
  } else if (size >= 1024) {
    itoa((int)(size / 1024), out, 10);
    strcat(out, " KB");
  } else {
    itoa((int)size, out, 10);
    strcat(out, " B");
  }
  out[out_size - 1] = '\0';
}

static void explorer_draw_panel(int x, int y, int w, int h, uint32_t color) {
  draw_rect(x, y, w, h, color);
  draw_rect(x, y, w, 1, EX_BORDER);
  draw_rect(x, y + h - 1, w, 1, EX_BORDER);
  draw_rect(x, y, 1, h, EX_BORDER);
  draw_rect(x + w - 1, y, 1, h, EX_BORDER);
}

static void explorer_draw_button(int x, int y, int w, int h, const char *label,
                                 uint32_t color) {
  draw_rounded_rect(x, y, w, h, 8, color);
  draw_rect(x, y + h - 1, w, 1, 0x40000000);
  draw_string(x + 12, y + 9, label, 0xFFFFFFFF);
}

static void explorer_draw_sidebar_item(int x, int y, int w, const char *label,
                                       const char *sub, bool active,
                                       uint32_t accent) {
  explorer_draw_panel(x, y, w, 54, active ? EX_SIDEBAR_ACTIVE : EX_PANEL_ALT);
  if (active) {
    draw_rect(x, y + 8, 3, 38, accent);
  }
  draw_rect(x + 16, y + 16, 12, 12, accent);
  draw_rect(x + 16, y + 31, 12, 2, accent);
  draw_string(x + 28, y + 12, label, EX_TEXT);
  draw_string(x + 28, y + 30, sub, EX_MUTED);
}

static void explorer_draw_entry_row(int index, int row_y) {
  char sizebuf[16] = {0};
  uint32_t row_color = index == selected_index ? EX_SIDEBAR_ACTIVE : EX_PANEL_ALT;
  uint32_t icon_color =
      entries[index].is_dir
          ? (entries[index].is_parent ? EX_MUTED : EX_AMBER)
          : EX_BLUE;

  explorer_draw_panel(206, row_y, 318, 40, row_color);
  draw_rect(220, row_y + 12, 16, 12, icon_color);
  draw_rect(220, row_y + 26, 16, 2, icon_color);
  draw_string(248, row_y + 11, entries[index].name, EX_TEXT);

  if (entries[index].is_parent) {
    draw_string(434, row_y + 11, "voltar", EX_MUTED);
  } else if (entries[index].is_dir) {
    draw_string(410, row_y + 11, "pasta", EX_MUTED);
  } else {
    explorer_format_size(entries[index].size, sizebuf, sizeof(sizebuf));
    draw_string(412, row_y + 11, sizebuf, EX_MUTED);
  }
}

static void explorer_draw_drive_card(int x, int y, int w, int h,
                                     explorer_source_t source,
                                     const char *footer) {
  uint32_t accent = explorer_source_accent(source);
  uint32_t body = EX_CARD;
  uint32_t strip = explorer_source_tint(source);

  explorer_draw_panel(x, y, w, h, body);
  draw_rect(x, y, 6, h, accent);
  draw_rounded_rect(x + 18, y + 18, 70, 50, 10, strip);
  draw_rect(x + 30, y + 32, 44, 8, accent);
  draw_rect(x + 30, y + 44, 44, 16, accent);
  draw_string(x + 84, y + 20, explorer_source_title(source), EX_TEXT);
  draw_string(x + 84, y + 42, explorer_source_subtitle(source), EX_MUTED);
  draw_rect(x + 18, y + h - 34, w - 36, 1, EX_BORDER);
  draw_string(x + 18, y + h - 24, footer, EX_TEXT);
}

static void explorer_draw_home(void) {
  explorer_draw_panel(206, 96, 776, 458, EX_PANEL);
  draw_string(234, 118, "Este Computador", EX_TEXT);
  draw_string(234, 140,
              "Abra um volume para navegar, ler e editar arquivos.",
              EX_MUTED);

  draw_string(234, 184, "Dispositivos e unidades", EX_MUTED);
  explorer_draw_drive_card(234, 206, 350, 132, EXPLORER_SOURCE_DISK,
                           sdfs_is_mounted() ? "Pronto para uso"
                                              : "Disco indisponivel");
  explorer_draw_drive_card(602, 206, 350, 132, EXPLORER_SOURCE_INITRD,
                           "Imagem live do sistema");

  explorer_draw_panel(234, 370, 718, 150, EX_CARD_HIGHLIGHT);
  draw_string(256, 392, "Acesso rapido", EX_TEXT);
  draw_string(256, 418, "Disco Local (C:): arquivos persistentes do usuario",
              EX_MUTED);
  draw_string(256, 438, "Sistema Live (X:): apps e assets da imagem inicial",
              EX_MUTED);
  draw_string(256, 458, "Use o disco local para criar, editar e salvar texto.",
              EX_TEXT);
  draw_string(256, 484, "Use o sistema live para consultar os arquivos da ISO.",
              EX_TEXT);
}

static void explorer_draw_browser(void) {
  char path_label[128];
  char info[64] = {0};
  char sizebuf[16] = {0};

  explorer_format_path_label(path_label, sizeof(path_label));

  explorer_draw_panel(206, 96, 318, 458, EX_PANEL);
  draw_string(228, 118, explorer_source_title(current_source), EX_TEXT);
  draw_string(228, 140, explorer_source_subtitle(current_source), EX_MUTED);
  explorer_draw_panel(226, 164, 278, 34, EX_PANEL_SOFT);
  draw_string(240, 173, path_label, EX_TEXT);
  for (int i = 0; i < entry_count && i < 8; i++) {
    explorer_draw_entry_row(i, 212 + (i * 44));
  }

  explorer_draw_panel(542, 96, 440, 458, EX_PANEL);
  draw_string(564, 118, "Detalhes", EX_TEXT);
  draw_string(564, 140, explorer_source_hint(current_source), EX_MUTED);

  explorer_draw_button(560, 164, 86, 32, "Salvar", EX_BLUE);
  explorer_draw_button(654, 164, 102, 32, "Renomear", EX_PANEL_SOFT);
  explorer_draw_button(764, 164, 82, 32, "Excluir", EX_RED_SOFT);
  explorer_draw_button(854, 164, 108, 32, "Novo TXT", EX_PANEL_SOFT);

  explorer_draw_panel(560, 214, 382, 58,
                      active_field == EXPLORER_FIELD_NAME ? EX_SIDEBAR_ACTIVE
                                                          : EX_PANEL_SOFT);
  draw_string(578, 226, "Nome", EX_MUTED);
  draw_string(578, 246, name_field[0] ? name_field : "Nenhum item selecionado",
              EX_TEXT);
  if (active_field == EXPLORER_FIELD_NAME) {
    draw_rect(574, 260, 350, 2, EX_BLUE_BRIGHT);
  }

  explorer_draw_panel(560, 288, 382, 196,
                      active_field == EXPLORER_FIELD_CONTENT ? EX_SIDEBAR_ACTIVE
                                                             : EX_PANEL_SOFT);
  draw_string(578, 300, "Conteudo", EX_MUTED);
  explorer_draw_text_block(578, 322, 346, 144,
                           editor_buffer[0] ? editor_buffer
                                            : "Selecione um arquivo ou crie um novo.",
                           EX_TEXT);
  if (active_field == EXPLORER_FIELD_CONTENT) {
    draw_rect(574, 470, 350, 2, EX_BLUE_BRIGHT);
  }

  if (selected_index >= 0 && selected_index < entry_count) {
    explorer_draw_panel(560, 498, 382, 40, EX_PANEL_ALT);
    draw_string(578, 510, entries[selected_index].is_dir ? "Diretorio"
                                                          : "Arquivo",
                EX_MUTED);
    explorer_format_size(entries[selected_index].size, sizebuf, sizeof(sizebuf));
    draw_string(844, 510, sizebuf, EX_TEXT);
  }

  itoa(entry_count, info, 10);
  strcat(info, " itens");
  draw_string(226, 522, info, EX_MUTED);
}

static void explorer_redraw(void) {
  if (!explorer_win) {
    return;
  }

  video_set_target(explorer_buffer.pixels, explorer_buffer.width,
                   explorer_buffer.height);

  draw_rect(0, 0, explorer_buffer.width, explorer_buffer.height, EX_BG);
  draw_rect(0, 0, explorer_buffer.width, 80, EX_HEADER);
  draw_string(24, 18, "Explorer", EX_TEXT);
  draw_string(24, 40,
              "Gerencie volumes, navegue pelos arquivos e edite o disco local",
              EX_MUTED);

  explorer_draw_panel(18, 96, 170, 458, EX_HEADER);
  draw_string(34, 116, "Navegacao", EX_MUTED);
  explorer_draw_sidebar_item(28, 136, 150, "Este Computador",
                             "Visao geral dos volumes",
                             current_view == EXPLORER_VIEW_HOME, EX_BLUE);
  explorer_draw_sidebar_item(
      28, 196, 150, "Sistema (X:)", "Arquivos live e apps",
      current_view == EXPLORER_VIEW_BROWSER &&
          current_source == EXPLORER_SOURCE_INITRD,
      EX_BLUE);
  explorer_draw_sidebar_item(
      28, 256, 150, "Disco Local (C:)", "Volume persistente FAT32",
      current_view == EXPLORER_VIEW_BROWSER &&
          current_source == EXPLORER_SOURCE_DISK,
      EX_GREEN);

  draw_string(34, 344, "Ferramentas", EX_MUTED);
  explorer_draw_sidebar_item(28, 364, 150, "Voltar", "Subir diretorio",
                             false, EX_MUTED);
  explorer_draw_sidebar_item(28, 424, 150, "Nova Pasta", "Criar diretorio",
                             false, EX_AMBER);
  explorer_draw_sidebar_item(28, 484, 150, "Novo Arquivo", "Criar TXT vazio",
                             false, EX_BLUE);

  if (current_view == EXPLORER_VIEW_HOME) {
    explorer_draw_home();
  } else {
    explorer_draw_browser();
  }

  explorer_draw_panel(18, 570, 964, 32, EX_DARK);
  draw_string(30, 579, status_line, 0xFFFFFFFF);

  video_reset_target();
  wl_commit(explorer_win);
}

widget_t *init_explorer() {
  if (explorer_win) {
    return explorer_win;
  }

  explorer_buffer.width = 1000;
  explorer_buffer.height = 620;
  explorer_buffer.pixels = (uint32_t *)kmalloc(explorer_buffer.width *
                                               explorer_buffer.height * 4);
  explorer_buffer.shm = true;

  explorer_win = wl_create_surface(explorer_buffer.width, explorer_buffer.height,
                                   WL_SURFACE_TOPLEVEL);
  explorer_win->x = 48;
  explorer_win->y = 64;
  strcpy(explorer_win->title, "Explorador de Arquivos");
  wl_attach_buffer(explorer_win, &explorer_buffer);

  explorer_set_disk_path("/");
  explorer_enter_home();
  explorer_refresh_entries();
  explorer_redraw();
  return explorer_win;
}

void explorer_click_handler(int rx, int ry) {
  if (!explorer_win) {
    return;
  }

  explorer_focus();

  if (rx >= 28 && rx <= 178) {
    if (ry >= 136 && ry <= 190) {
      explorer_enter_home();
      explorer_redraw();
      return;
    }
    if (ry >= 196 && ry <= 250) {
      explorer_switch_to_browser(EXPLORER_SOURCE_INITRD);
      explorer_refresh_entries();
      explorer_redraw();
      return;
    }
    if (ry >= 256 && ry <= 310) {
      explorer_switch_to_browser(EXPLORER_SOURCE_DISK);
      explorer_refresh_entries();
      explorer_redraw();
      return;
    }
    if (ry >= 364 && ry <= 418) {
      if (current_view == EXPLORER_VIEW_BROWSER &&
          current_source == EXPLORER_SOURCE_DISK &&
          strcmp(current_disk_path, "/") != 0) {
        char parent[128];
        explorer_disk_parent_path(parent, sizeof(parent));
        explorer_set_disk_path(parent);
        explorer_clear_editor();
        explorer_refresh_entries();
      } else {
        explorer_enter_home();
      }
      explorer_redraw();
      return;
    }
    if (ry >= 424 && ry <= 478) {
      explorer_new_folder();
      explorer_redraw();
      return;
    }
    if (ry >= 484 && ry <= 538) {
      explorer_new_file();
      current_view = EXPLORER_VIEW_BROWSER;
      current_source = EXPLORER_SOURCE_DISK;
      explorer_redraw();
      return;
    }
  }

  if (current_view == EXPLORER_VIEW_HOME) {
    if (rx >= 234 && rx <= 584 && ry >= 206 && ry <= 338) {
      explorer_switch_to_browser(EXPLORER_SOURCE_DISK);
      explorer_refresh_entries();
      explorer_redraw();
      return;
    }
    if (rx >= 602 && rx <= 952 && ry >= 206 && ry <= 338) {
      explorer_switch_to_browser(EXPLORER_SOURCE_INITRD);
      explorer_refresh_entries();
      explorer_redraw();
    }
    return;
  }

  if (ry >= 164 && ry <= 196) {
    if (rx >= 560 && rx <= 646) {
      explorer_save();
    } else if (rx >= 654 && rx <= 756) {
      explorer_rename();
    } else if (rx >= 764 && rx <= 846) {
      explorer_delete();
    } else if (rx >= 854 && rx <= 962) {
      explorer_new_file();
    }
    explorer_redraw();
    return;
  }

  if (rx >= 206 && rx <= 524 && ry >= 212 && ry <= 564) {
    int row = (ry - 212) / 44;
    if (row >= 0 && row < entry_count && row < 8) {
      selected_index = row;
      explorer_load_selected();
      if (!entries[row].is_dir) {
        active_field = EXPLORER_FIELD_CONTENT;
      }
      explorer_redraw();
    }
    return;
  }

  if (rx >= 560 && rx <= 942 && ry >= 214 && ry <= 272) {
    active_field = EXPLORER_FIELD_NAME;
    explorer_redraw();
    return;
  }

  if (rx >= 560 && rx <= 942 && ry >= 288 && ry <= 484) {
    active_field = EXPLORER_FIELD_CONTENT;
    explorer_redraw();
  }
}

void update_explorer_key(char k) {
  size_t len;

  if (!explorer_is_focused() || current_view != EXPLORER_VIEW_BROWSER ||
      active_field == EXPLORER_FIELD_NONE || !k) {
    return;
  }

  if (k == '\t') {
    active_field = active_field == EXPLORER_FIELD_NAME ? EXPLORER_FIELD_CONTENT
                                                       : EXPLORER_FIELD_NAME;
    explorer_redraw();
    return;
  }

  if (active_field == EXPLORER_FIELD_NAME) {
    len = strlen(name_field);
    if (k == '\b') {
      if (len > 0) {
        name_field[len - 1] = '\0';
      }
    } else if (k >= 32 && k <= 126 && len < sizeof(name_field) - 1) {
      name_field[len] = k;
      name_field[len + 1] = '\0';
    }
    explorer_redraw();
    return;
  }

  len = strlen(editor_buffer);
  if (k == '\b') {
    if (len > 0) {
      editor_buffer[len - 1] = '\0';
    }
  } else if (k == '\n') {
    if (len < sizeof(editor_buffer) - 1) {
      editor_buffer[len] = '\n';
      editor_buffer[len + 1] = '\0';
    }
  } else if (k >= 32 && k <= 126 && len < sizeof(editor_buffer) - 1) {
    editor_buffer[len] = k;
    editor_buffer[len + 1] = '\0';
  }
  explorer_redraw();
}

wl_surface_t *get_explorer_surface(void) { return explorer_win; }

void open_explorer() {
  if (!explorer_win) {
    init_explorer();
  }
  explorer_win->visible = true;
  explorer_win->is_focused = true;
  explorer_redraw();
}
