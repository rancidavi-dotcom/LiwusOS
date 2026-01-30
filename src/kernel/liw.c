#include "liw.h"
#include "video.h"

#define MAX_PACKAGES 10
liw_pkg_t repo[MAX_PACKAGES];

/* Função utilitária de comparação de string já que não temos a libc completa */
int liw_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void liw_init() {
    /* Simulação de um repositório carregado do "disco" */
    for(int i=0; i<MAX_PACKAGES; i++) repo[i].installed = 0;

    /* Pacotes Disponíveis */
    // liw_pkg_t p1 = {"calc", "1.0", 1024, 0};
    // liw_pkg_t p2 = {"editor", "0.5", 2048, 0};
    // repo[0] = p1; repo[1] = p2;
}

void liw_install(const char* name) {
    draw_string(10, 450, "liw: Buscando pacote...", 0xFFFF00);
    refresh_screen();
    for(volatile int i=0; i<2000000; i++);

    draw_string(10, 470, "liw: Baixando e verificando assinaturas...", 0x00FF00);
    refresh_screen();
    for(volatile int i=0; i<3000000; i++);

    draw_string(10, 490, "liw: Instalando ", 0xFFFFFF);
    draw_string(140, 490, name, 0x00FFFF);
    draw_string(10, 510, "SUCESSO: Pacote configurado!", 0x00FF00);
    refresh_screen();
}
