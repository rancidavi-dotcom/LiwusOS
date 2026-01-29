#include "welcome.h"
#include "video.h"

static widget_t* welcome_win;

void on_close_welcome(widget_t* self) {
    (void)self;
    welcome_win->visible = false;
}

widget_t* init_welcome() {
    welcome_win = create_window("Bem-vindo ao LiwusOS", 340, 200, 600, 300);
    
    add_widget(welcome_win, create_label("Ola, Davi! O LiwusOS ja esta rodando.", 20, 30, 0x000000));
    add_widget(welcome_win, create_label("Esta versao inclui suporte a Rede e Internet.", 20, 60, 0x333333));
    add_widget(welcome_win, create_label("Use a Dock abaixo para navegar.", 20, 90, 0x0055AA));
    
    add_widget(welcome_win, create_button("Comecar Agora", 200, 180, 200, 40, on_close_welcome));
    
    welcome_win->visible = true; // Abre automaticamente ao iniciar
    return welcome_win;
}

void open_welcome() {
    welcome_win->visible = true;
    welcome_win->focused = true;
}
