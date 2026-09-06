#include "terminal.h"
#include "string.h"
#include "vga.h"
#include "serial.h"

extern void lde_app_start(void);
extern const char *get_launch_last_error(void);
void cmd_lde(int argc, char **argv) {
    (void)argc; (void)argv;
    lde_app_start();
    vga_puts("LDE app started. Last launch error: ");
    vga_puts((char*)get_launch_last_error());
    vga_puts("\n");
}

// Executa o Tiny C Compiler (userspace) a partir do initrd, repassando
// os argumentos digitados no terminal (ex.: "tcc -v", "tcc prog.c").
void cmd_tcc(int argc, char **argv) {
    extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
    (void)argc;

    // Passa argv[0]="tcc" seguido dos argumentos do utilizador.
    char **prog_argv = argv;
    if (prog_argv && prog_argv[0])
        prog_argv[0] = "tcc";

    int pid = launch_initrd_program_argv("tcc", prog_argv);

    char buf[64];
    int n = 0;
    buf[n++] = '[';
    buf[n++] = 'k';
    buf[n++] = 'e';
    buf[n++] = 'r';
    buf[n++] = 'n';
    buf[n++] = 'e';
    buf[n++] = 'l';
    buf[n++] = ']';
    buf[n++] = ' ';
    const char *label = "tcc";
    while (*label) buf[n++] = *label++;
    buf[n++] = ':';
    buf[n++] = ' ';
    char pidbuf[16];
    if (pid < 0) {
        buf[n++] = 'e';
        buf[n++] = 'r';
        buf[n++] = 'r';
        buf[n++] = 'o';
        buf[n++] = 'r';
        buf[n++] = '\n';
        buf[n] = '\0';
    } else {
        itoa(pid, pidbuf, 10);
        const char *p = pidbuf;
        while (*p) buf[n++] = *p++;
        char end[] = " started\n";
        const char *e = end;
        while (*e) buf[n++] = *e++;
        buf[n] = '\0';
    }
    vga_puts(buf);
    serial_print(buf);
}

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
    {"ls", cmd_ls, "Lists files in the current directory"},
    {"cd", cmd_cd, "Change directory"},
    {"pwd", cmd_pwd, "Prints the current working directory"},
    {"cat", cmd_cat, "Display file contents"},
    {"mkdir", cmd_mkdir, "Create directory"},
    {"touch", cmd_touch, "Create empty file"},
    {"rm", cmd_rm, "Remove file or directory"},
    {"mv", cmd_mv, "Move/rename file"},
    {"cp", cmd_cp, "Copy file"},
    {"reboot", cmd_reboot, "Reboots the system"},
    {"version", cmd_version, "Shows the OS version"},
    {"meminfo", cmd_meminfo, "Shows memory information"},
    {"diskinfo", cmd_diskinfo, "Shows disk space information"},
    {"ip", cmd_ip, "Shows current IP address"},
    {"ping", cmd_ping, "Send ICMP ECHO_REQUEST to network hosts"},
    {"wget", cmd_wget, "Download files from the web (HTTP)"},
    {"host", cmd_host, "DNS lookup utility"},
    {"tcc", cmd_tcc, "Tiny C Compiler - compila C dentro do OS"},
    {"run", cmd_run, "Run a compiled ELF program (ex: run hello)"},
    {"/lde", cmd_lde, "Launch Liwus Desktop Engine"},
    {"lde", cmd_lde, "Launch Liwus Desktop Engine"}
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
