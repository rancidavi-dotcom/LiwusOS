#include "terminal.h"
#include "string.h"
#include "vga.h"
#include "serial.h"

// Executa um programa compilado (ELF) a partir do filesystem SDFS, repassando
// argumentos. Uso: run <programa> [args...]
//
// O caminho é relativo à raiz do SDFS (que corresponde ao mount VFS
// /house/localhost). Ex.: "run hello" roda /house/localhost/hello.
// Também aceita caminhos completos VFS, ex.: "run /house/localhost/hello".
void cmd_run(int argc, char **argv) {
    extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
    if (argc < 2 || !argv[1]) return;

    // Normaliza o caminho para a forma esperada por sys_execve (SDFS-relativo).
    static char prog_path[256];
    const char *p = argv[1];
    static const char mount_prefix[] = "/house/localhost/";
    if (strncmp(p, mount_prefix, sizeof(mount_prefix) - 1) == 0) {
        p += sizeof(mount_prefix) - 1;          // "hello" relativo ao SDFS root
    }
    if (p[0] == '/') p++;                        // sem barra inicial (sys_execve a adiciona)
    strncpy(prog_path, p, 255);
    prog_path[255] = '\0';

    // Monta argv: [programa, args..., NULL]
    char *prog_argv[17];
    int i;
    prog_argv[0] = prog_path;
    for (i = 1; i < argc && i < 16; i++) {
        prog_argv[i] = argv[i + 1];
    }
    prog_argv[i] = NULL;

    int pid = launch_initrd_program_argv(prog_path, prog_argv);

    vga_puts("[kernel] run ");
    vga_puts(prog_path);
    vga_puts(" pid=");
    serial_print("[kernel] run ");
    serial_print(prog_path);
    serial_print(" pid=");
    char pidbuf[16];
    if (pid < 0) {
        vga_puts("error\n");
        serial_print("error\n");
    } else {
        itoa(pid, pidbuf, 10);
        vga_puts(pidbuf);
        vga_puts("\n");
        serial_print(pidbuf);
        serial_print("\n");
    }
}

// Define command table
static const terminal_command_t commands[] = {
    {"help", cmd_help, "Shows this help message"},
    {"clear", cmd_clear, "Clears the terminal screen"},
    {"echo", cmd_echo, "Prints text to the terminal"},
    {"sed", cmd_sed, "Write text to file (ex: sed \"texto\" > arquivo.txt)"},
    {"ls", cmd_ls, "Lists files in the current directory"},
    {"cd", cmd_cd, "Change directory"},
    {"pwd", cmd_pwd, "Prints the current working directory"},
    {"cat", cmd_cat, "Display file contents"},
    {"mkdir", cmd_mkdir, "Create directory"},
    {"touch", cmd_touch, "Create empty file"},
    {"rm", cmd_rm, "Remove file or directory"},
    {"mv", cmd_mv, "Move/rename file"},
    {"cp", cmd_cp, "Copy file"},
    {"reboot", cmd_reboot, "Reboots the system (ACPI reset)"},
    {"shutdown", cmd_shutdown, "Powers off the system (ACPI S5)"},
    {"version", cmd_version, "Shows the OS version"},
    {"meminfo", cmd_meminfo, "Shows memory information"},
    {"ps", cmd_ps, "Lista tarefas (pid, prioridade, estado, cpu)"},
    {"diskinfo", cmd_diskinfo, "Shows disk space information"},
    {"run", cmd_run, "Run a compiled ELF program (ex: run hello)"},
    {"kblayout", cmd_kblayout, "Set keyboard layout (abnt2|us|us-intl)"},
    {"msense", cmd_msense, "Set mouse sensitivity (1-10) and acceleration (on|off)"},
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(terminal_command_t))

void terminal_execute(int argc, char **argv) {
    if (argc == 0 || !argv[0]) return;

    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }

    vga_puts("Command not found: ");
    vga_puts(argv[0]);
    vga_puts("\n");
    serial_print("Command not found: ");
    serial_print(argv[0]);
    serial_print("\n");
}