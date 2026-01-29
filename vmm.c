#include "vmm.h"
#include "pmm.h"

/* Criamos 16 tabelas de página (16 * 4MB = 64MB) */
uint32_t page_directory[1024] __attribute__((aligned(4096)));
uint32_t page_tables[16][1024] __attribute__((aligned(4096)));

extern void load_page_directory(uint32_t*);
extern void enable_paging();

void init_vmm() {
    /* 1. Limpa o diretório */
    for(int i = 0; i < 1024; i++) {
        page_directory[i] = 0x00000002; /* Supervisor, R/W, Not Present */
    }

    /* 2. Identity Map dos primeiros 64MB */
    for(int t = 0; t < 16; t++) {
        for(int i = 0; i < 1024; i++) {
            page_tables[t][i] = ((t * 1024 + i) * 4096) | 3; /* Present + R/W */
        }
        /* Coloca a tabela no diretório */
        page_directory[t] = ((uint32_t)page_tables[t]) | 3;
    }

    /* 3. Mapeia a região do Framebuffer (Geralmente em 0xFD000000 ou similar) */
    /* Vamos mapear 16MB nessa região para garantir suporte a 4K no futuro */
    static uint32_t video_tables[4][1024] __attribute__((aligned(4096)));
    uint32_t video_base = 0xFD000000; /* Padrão QEMU/Bochs */
    
    for(int t = 0; t < 4; t++) {
        for(int i = 0; i < 1024; i++) {
            video_tables[t][i] = (video_base + (t * 1024 + i) * 4096) | 3;
        }
        page_directory[(video_base >> 22) + t] = ((uint32_t)video_tables[t]) | 3;
    }

    /* 4. Ativa a Paging */
    load_page_directory(page_directory);
    enable_paging();
}
