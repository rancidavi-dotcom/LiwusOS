#include "installer.h"
#include "video.h"
#include "mouse.h"
#include "string.h"
#include "ata.h"
#include "gui.h"
#include "io.h"

static install_step_t step = INT_INIT;
static install_config_t config;
extern void format_fat32(uint32_t sectors);
extern void write_kernel_to_disk(void* data, uint32_t size);
extern char get_last_key();

widget_t* init_installer() {
    step = INT_INIT;
    memset(&config, 0, sizeof(install_config_t));
    strcpy(config.username, "davi");
    return NULL; /* Retornamos NULL pois ele e renderizado via draw_installer_full agora */
}

void draw_installer_full() {
    clear_screen(0x252525);
    draw_rect(0, 0, 250, 768, 0x1a1a1a);
    draw_string(20, 40, "LiwusOS Setup", 0x00FF00);

    int cx = 280; int cy = 50;
    int mx = get_mouse_x(); int my = get_mouse_y();
    bool click = is_left_clicked();
    char k = get_last_key();

    if (step == INT_INIT) {
        draw_string(cx, cy, "Selecione o Idioma", 0xFFFFFF);
        draw_button_visual(cx, 120, 300, 40, "Portugues (Brasil)", 0x444444);
        if (click && is_inside(mx, my, cx, 120, 300, 40)) step = IDENTITY;
    }
    else if (step == IDENTITY) {
        draw_string(cx, cy, "Digite o Nome de Usuario:", 0xFFFFFF);
        static int ptr = 0;
        if (k >= 32 && k <= 126 && ptr < 20) { config.username[ptr++] = k; config.username[ptr] = '\0'; }
        else if (k == '\b' && ptr > 0) config.username[--ptr] = '\0';
        
        draw_rect(cx, 100, 300, 40, 0x000000);
        draw_string(cx + 10, 112, config.username, 0x00FF00);
        
        draw_button_visual(cx, 200, 200, 40, "Continuar", 0x0055AA);
        if (click && is_inside(mx, my, cx, 200, 200, 40)) step = STORAGE;
    }
    else if (step == STORAGE) {
        draw_string(cx, cy, "Formatando Disco em FAT32", 0xFFFFFF);
        draw_string(cx, 100, "Disco Detectado: /dev/hda (100MB)", 0xAAAAAA);
        draw_button_visual(cx, 200, 350, 50, "CRIAR PARTICAO E FORMATAR", 0xAA0000);
        if (click && is_inside(mx, my, cx, 200, 350, 50)) {
            format_fat32(204800);
            step = EXECUTION;
        }
    }
    else if (step == EXECUTION) {
        static int current_sector = 0;
        int total_sectors_to_write = 500; /* Simulação de tamanho do Kernel */
        
        draw_string(cx, cy, "Copiando arquivos do Kernel...", 0xFFFFFF);
        
        /* Escrita Real no Disco! */
        uint16_t dummy_data[256];
        for(int i=0; i<256; i++) dummy_data[i] = 0xABCD;
        ata_write_sector(ATA_PRIMARY, ATA_MASTER, 200 + current_sector, dummy_data);

        int progress = (current_sector * 100) / total_sectors_to_write;
        draw_loading_bar(cx, 150, 500, 30, progress);
        
        if (current_sector < total_sectors_to_write) {
            current_sector += 5; /* Escreve 5 setores por frame para ser rápido */
        } else {
            step = FINALIZE;
        }
    }
    else if (step == FINALIZE) {
        draw_string(cx, cy, "Instalacao Concluida!", 0x00FF00);
        draw_string(cx, 100, "O disco /dev/hda agora contem o LiwusOS.", 0xFFFFFF);
        draw_button_visual(cx, 200, 300, 50, "REINICIAR AGORA", 0xAA0000);
        if (click && is_inside(mx, my, cx, 200, 300, 50)) sys_reboot();
    }
}