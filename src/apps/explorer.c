#include "gui.h"
#include "initrd.h"
#include "video.h"
#include "string.h"

// Força a declaração caso o header falhe
extern char* initrd_list_files(int index);

widget_t* explorer_win;
static char* selected_file_content = "Selecione um arquivo para ver o conteudo.";

void on_file_click(widget_t* self) {
    uint32_t size;
    void* data = initrd_get_file(self->text, &size);
    if (data) {
        selected_file_content = (char*)data;
    }
}

widget_t* init_explorer() {
    explorer_win = create_window("Explorador de Arquivos", 150, 150, 600, 450);
    explorer_win->visible = false;

    add_widget(explorer_win, create_label("Arquivos em /initrd:", 10, 10, 0x000000));

    /* Lista os arquivos dinamicamente do Initrd */
    for (int i = 0; i < 5; i++) {
        char* name = initrd_list_files(i);
        if (name) {
            add_widget(explorer_win, create_button(name, 10, 40 + (i * 45), 200, 35, on_file_click));
        }
    }

    return explorer_win;
}

void draw_explorer_content() {
    if (!explorer_win->visible) return;
    int cx = explorer_win->x + 230;
    int cy = explorer_win->y + 70;

    draw_rect(cx, cy, 350, 350, 0xFFFFFF); /* Área de visualização */
    draw_string(cx + 10, cy + 10, selected_file_content, 0x333333);
}

void open_explorer() {
    explorer_win->visible = true;
    explorer_win->focused = true;
}
