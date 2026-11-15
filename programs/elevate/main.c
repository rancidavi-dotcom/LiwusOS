#include "liwus/stdio.h"
#include "liwus/stdlib.h"
#include "liwus/string.h"
#include "liwus/syscall.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Uso: elevate <comando> [argumentos...]\n");
        printf("Executa comandos com privilegios elevados de superusuario (root).\n");
        printf("Exemplos:\n");
        printf("  elevate whoami\n");
        printf("  elevate useradd novo_usuario\n");
        printf("  elevate lpkg sysupgrade\n");
        return 1;
    }

    printf("[ELEVATE] Elevando privilegios para root (UID 0)...\n");
    char cmd[256];
    cmd[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (i > 1) strcat(cmd, " ");
        strncat(cmd, argv[i], sizeof(cmd) - strlen(cmd) - 1);
    }

    /* Tenta executar o comando em ring-3 */
    char path[128] = "/bin/";
    strncat(path, argv[1], sizeof(path) - strlen(path) - 1);
    char lxe_path[128];
    strncpy(lxe_path, path, sizeof(lxe_path) - 1);
    strcat(lxe_path, ".lxe");

    int pid = liw_fork();
    if (pid == 0) {
        /* Filho */
        char *exec_argv[16];
        int j;
        exec_argv[0] = argv[1];
        for (j = 1; j < argc - 1 && j < 15; j++) {
            exec_argv[j] = argv[j + 1];
        }
        exec_argv[j] = NULL;
        liw_execve(lxe_path, exec_argv, NULL);
        liw_execve(path, exec_argv, NULL);
        printf("elevate: comando '%s' nao encontrado.\n", argv[1]);
        liw_exit(127);
    } else if (pid > 0) {
        int status = 0;
        liw_waitpid(pid, &status, 0);
        return status;
    } else {
        printf("elevate: falha ao criar processo de execucao.\n");
        return 1;
    }
}
