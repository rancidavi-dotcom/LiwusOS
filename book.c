#include "gui.h"
#include <stddef.h>

static int current_page = 0;
static widget_t* book_win;
static widget_t* lbl_content;
static widget_t* lbl_page_num;

const char* pages[] = {
    "Bem-vindo ao LiwusOS!\n\nEste projeto e uma jornada para criar\num sistema operacional moderno e livre.\n\nAutor: Davi VilasBoas Ranci",
    "Versao Atual: v1.0 (C Edition)\n\nO Kernel foi totalmente migrado de\nAssembly para C para melhor performance\ne facilidade de desenvolvimento.",
    "O que ja foi feito:\n- Boot Multiboot via GRUB\n- Gerenciamento de Memoria (PMM/VMM)\n- Multitarefa Preemptiva\n- Driver de Mouse e Teclado\n- LibUI (Framework de Widgets)",
    "Planos Futuros:\n- Suporte a Rede (TCP/IP)\n- Sistema de Arquivos (ATA/HDD)\n- Audio e Drivers de Video Acelerados\n- Mais aplicativos nativos!"
};

void update_book_page() {
    lbl_content->text = pages[current_page];
}

void on_next_click(widget_t* self) {
    if (current_page < 3) {
        current_page++;
        update_book_page();
    }
}

void on_prev_click(widget_t* self) {
    if (current_page > 0) {
        current_page--;
        update_book_page();
    }
}

widget_t* init_book() {
    book_win = create_window("Guia de Inicio - LiwusOS", 300, 150, 450, 350);
    
    lbl_content = create_label(pages[0], 20, 30, 0x000000);
    add_widget(book_win, lbl_content);

    add_widget(book_win, create_button("< Ant", 20, 250, 80, 30, on_prev_click));
    add_widget(book_win, create_button("Prox >", 350, 250, 80, 30, on_next_click));

    return book_win;
}
