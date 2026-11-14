#include <stdio.h>

int main(int argc, char **argv) {
    printf("\n===========================================\n");
    printf("   Ola Mundo do TCC Nativo no LiwusOS!    \n");
    printf("===========================================\n");
    printf("Compilado pelo proprio Tiny C Compiler (TCC)!\n");
    for (int i = 0; i < argc; i++) {
        printf("argv[%d] = %s\n", i, argv[i]);
    }
    printf("Execucao finalizada com sucesso!\n\n");
    return 0;
}
