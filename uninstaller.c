#include "gui.h"
#include "ata.h"
#include "video.h"
#include "string.h"

static bool wipe_done = false;

void on_confirm_wipe(widget_t* self) {
    uint16_t zero_buffer[256];
    for(int i=0; i<256; i++) zero_buffer[i] = 0;

    /* Apaga o MBR (Setor 0) e os setores de dados */
    draw_string(10, 150, "Limpando trilhas do disco...", 0xFFFF00);
    refresh_screen();

    for(uint32_t s=0; s<100; s++) {
        // Bus 0x1F0 (Primary), Drive 0xA0 (Master), LBA s, Buffer zero_buffer
        ata_write_sector(0x1F0, 0xA0, s, zero_buffer);
    }

    wipe_done = true;
    draw_string(10, 180, "DESINSTALADO! O disco esta limpo.", 0x00FF00);
    refresh_screen();
}

widget_t* init_uninstaller() {
    widget_t* win = create_window("Desinstalador LiwusOS", 400, 300, 400, 250);
    add_widget(win, create_label("Deseja remover o LiwusOS do HD?", 20, 20, 0x000000));
    add_widget(win, create_label("Isso apagara todos os dados!", 20, 45, 0xAA0000));
    add_widget(win, create_button("Confirmar Limpeza", 100, 120, 200, 40, on_confirm_wipe));
    return win;
}
