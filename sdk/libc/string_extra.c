#include <string.h>

char *strerror(int errnum) {
    (void)errnum;
    return "Erro desconhecido";
}

// Outras funções de string se existissem iriam aqui
