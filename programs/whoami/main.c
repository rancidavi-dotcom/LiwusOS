#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/syscall.h"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    long uid = liw_syscall0(102);  /* getuid */
    long euid = liw_syscall0(107); /* geteuid */

    if (uid == 0 || euid == 0) {
        printf("root (superusuario - privilegios elevados)\n");
    } else {
        printf("davi (uid=%ld, euid=%ld - usuario padrao)\n", uid, euid);
    }
    return 0;
}
