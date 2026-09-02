/* Programa de exemplo usado pelo teste de integração do Tiny C Compiler
 * (TCC) dentro do LiwusOS. Compilado em runtime pelo comando "tcc",
 * gera um ELF estatico que o kernel carrega como task userspace.
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    (void)argc;
    printf("OLAR DO TCC! argv[0]=%s\n", argv ? argv[0] : "(null)");
    fflush(stdout);
    return 0;
}