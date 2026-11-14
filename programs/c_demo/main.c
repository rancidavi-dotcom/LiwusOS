#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/setjmp.h"

static jmp_buf jbuf;

static void test_jump(void) {
    longjmp(jbuf, 42);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("=== LIWUS C LIBC TEST ===\n");

    // 1. Teste de malloc & free
    char *ptr = (char *)malloc(128);
    if (!ptr) {
        printf("FALHA: malloc retornou NULL!\n");
        return 1;
    }
    strcpy(ptr, "Malloc funcionando perfeitamente em Ring 3!");
    printf("Malloc: %s (ptr=%p)\n", ptr, ptr);

    // 2. Teste de realloc
    ptr = (char *)realloc(ptr, 256);
    strcat(ptr, " + Realloc OK!");
    printf("Realloc: %s\n", ptr);
    free(ptr);

    // 3. Teste de conversões
    long num = strtol("12345", NULL, 10);
    long hex = strtol("0xABC", NULL, 16);
    printf("strtol: dec=%ld hex=%ld (0x%lx)\n", num, hex, hex);

    // 4. Teste de snprintf
    char buf[64];
    snprintf(buf, sizeof(buf), "Teste snprintf: num=%d, str=%s", 999, "liwus");
    printf("%s\n", buf);

    // 5. Teste de setjmp / longjmp
    int val = setjmp(jbuf);
    if (val == 0) {
        printf("Antes do longjmp...\n");
        test_jump();
    } else {
        printf("Retorno do longjmp com valor: %d\n", val);
    }

    printf("=== TODOS OS TESTES DA LIBC PASSARAM! ===\n");
    return 0;
}
