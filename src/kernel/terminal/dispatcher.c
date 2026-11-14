#include "terminal.h"
#include "string.h"
#include "vga.h"
#include "serial.h"
#include <stdbool.h>

extern char *itoa(int value, char *str, int base);

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
        extern int sys_waitpid(int pid, int *status, int options);
        int status = 0;
        sys_waitpid(pid, &status, 0);
    }
}

void cmd_c_demo(int argc, char **argv) {
    extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
    extern int sys_waitpid(int pid, int *status, int options);
    char *prog_argv[17];
    prog_argv[0] = "c_demo";
    for (int i = 1; i < argc && i < 16; i++) prog_argv[i] = argv[i];
    prog_argv[argc < 16 ? argc : 16] = NULL;

    int pid = launch_initrd_program_argv("/bin/c_demo.lxe", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("c_demo", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("bin/c_demo.lxe", prog_argv);
    if (pid > 0) {
        int status = 0;
        sys_waitpid(pid, &status, 0);
    } else {
        vga_puts("c_demo: falha ao carregar executavel\n");
    }
}

void cmd_tcc(int argc, char **argv) {
    extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
    extern int sys_waitpid(int pid, int *status, int options);
    char *prog_argv[32];
    prog_argv[0] = "tcc";
    for (int i = 1; i < argc && i < 31; i++) prog_argv[i] = argv[i];
    prog_argv[argc < 31 ? argc : 31] = NULL;

    int pid = launch_initrd_program_argv("/bin/tcc.lxe", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("tcc", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("bin/tcc.lxe", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("/sys/bin/tcc.lxe", prog_argv);
    if (pid > 0) {
        int status = 0;
        sys_waitpid(pid, &status, 0);
    } else {
        vga_puts("tcc: falha ao carregar compilador\n");
    }
}

void cmd_kilo(int argc, char **argv) {
    extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
    extern int sys_waitpid(int pid, int *status, int options);
    char *prog_argv[17];
    prog_argv[0] = "kilo";
    for (int i = 1; i < argc && i < 16; i++) prog_argv[i] = argv[i];
    prog_argv[argc < 16 ? argc : 16] = NULL;

    vga_puts("\x1b[2J\x1b[H");
    int pid = launch_initrd_program_argv("/bin/kilo.lxe", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("kilo", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("bin/kilo.lxe", prog_argv);
    if (pid > 0) {
        int status = 0;
        sys_waitpid(pid, &status, 0);
    } else {
        vga_puts("kilo: falha ao carregar editor\n");
    }
    vga_puts("\x1b[2J\x1b[H");
}

int run_doom_app(int argc, char **argv) {
    extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
    extern int sys_waitpid(int pid, int *status, int options);
    extern void vga_clear_framebuffer(uint32_t xrgb);
    extern void desktop_force_redraw(void);
    extern void keyboard_flush(void);

    char *prog_argv[17];
    prog_argv[0] = "doom";
    int n = 1;
    if (argv && argc > 1) {
        for (int i = 1; i < argc && n < 16; i++) {
            prog_argv[n++] = argv[i];
        }
    }
    prog_argv[n] = NULL;

    vga_clear_framebuffer(0x00000000U);
    keyboard_flush();
    int pid = launch_initrd_program_argv("/bin/doom.lxe", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("doom.lxe", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("doom", prog_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("bin/doom.lxe", prog_argv);
    if (pid > 0) {
        int status = 0;
        sys_waitpid(pid, &status, 0);
        keyboard_flush();
        desktop_force_redraw();
        return 0;
    }
    keyboard_flush();
    desktop_force_redraw();
    return -1;
}

void cmd_doom(int argc, char **argv) {
    if (run_doom_app(argc, argv) < 0) {
        vga_puts("doom: falha ao carregar o jogo\n");
    }
}

void cmd_dhcp(int argc, char **argv) {
    (void)argc; (void)argv;
    extern void cmd_ifconfig(int argc, char **argv);
    char *args[] = {"ifconfig", "dhcp"};
    cmd_ifconfig(2, args);
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
    {"kilo", cmd_kilo, "Editor de texto Kilo (ex: kilo arquivo.c)"},
    {"edit", cmd_kilo, "Editor de texto Kilo (ex: edit arquivo.c)"},
    {"doom", cmd_doom, "Jogo DOOM Shareware (tela cheia)"},
    {"dhcp", cmd_dhcp, "Renova / solicita IP do roteador via DHCP"},
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
    {"stat", cmd_stat, "Inspeciona tipo de recurso, badges e detalhes"},
    {"head", cmd_head, "Exibe primeiras linhas de arquivo"},
    {"diskinfo", cmd_diskinfo, "Shows disk space information"},
    {"c_demo", cmd_c_demo, "Executa testes da LibC em Ring 3"},
    {"cdemo", cmd_c_demo, "Executa testes da LibC em Ring 3 (alias)"},
    {"demo", cmd_c_demo, "Executa testes da LibC em Ring 3 (alias)"},
    {"libc", cmd_c_demo, "Executa testes da LibC em Ring 3 (alias)"},
    {"tcc", cmd_tcc, "Compilador C TinyCC Nativo (ex: tcc -v ou tcc -o out.lxe src.c)"},
    {"compile", cmd_tcc, "Compilador C TinyCC Nativo (alias)"},
    {"run", cmd_run, "Run a compiled ELF program (ex: run hello)"},
    {"ifconfig", cmd_ifconfig, "Show network configuration and IP"},
    {"ping", cmd_ping, "Ping an IP address (ex: ping 10.0.2.2)"},
    {"date", cmd_date, "Show current RTC date and time"},
    {"uptime", cmd_uptime, "Show system uptime in seconds"},
    {"kblayout", cmd_kblayout, "Set keyboard layout (abnt2|us|us-intl)"},
    {"msense", cmd_msense, "Set mouse sensitivity (1-10) and acceleration (on|off)"},
    {"beep", cmd_beep, "Toca um sinal sonoro (ex: beep [freq_hz] [ms])"},
    {"sound", cmd_sound, "Controle de áudio HD (test|chime|vol [0-100])"},
    {"screenshot", cmd_screenshot, "Envia uma captura da tela para o PC pela rede"},
    {"lspci", cmd_lspci, "Lista todos os dispositivos PCI do sistema"},
    {"wget", cmd_wget, "Baixa arquivo via HTTP/HTTPS e salva no disco SDFS"},
    {"curl", cmd_curl, "Obtem conteudo de URL via HTTP/HTTPS e exibe no terminal"},
    {"lpkg", cmd_lpkg, "Gerenciador de pacotes LiwusOS (list, install, remove, repo)"},
    {"pkg", cmd_lpkg, "Gerenciador de pacotes LiwusOS (alias para lpkg)"},
};

#define NUM_COMMANDS (sizeof(commands) / sizeof(terminal_command_t))

const terminal_command_t *terminal_get_commands(void) {
    return commands;
}

size_t terminal_get_command_count(void) {
    return NUM_COMMANDS;
}

void terminal_execute(int argc, char **argv) {
    if (argc == 0 || !argv[0]) return;

    for (size_t i = 0; i < NUM_COMMANDS; i++) {
        if (strcmp(argv[0], commands[i].name) == 0) {
            commands[i].func(argc, argv);
            return;
        }
    }

    // Tenta executar como programa externo (em sys:bin/, /bin/, ou no caminho fornecido)
    extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
    static const char *prefixes[] = { "", "sys:bin/", "/bin/", "bin/" };
    for (size_t p = 0; p < sizeof(prefixes) / sizeof(prefixes[0]); p++) {
        char prog_path[256];
        for (int suffix = 0; suffix <= 1; suffix++) {
            strcpy(prog_path, prefixes[p]);
            strcat(prog_path, argv[0]);
            if (suffix == 1) strcat(prog_path, ".lxe");

            char *prog_argv[17];
            int i;
            prog_argv[0] = prog_path;
            for (i = 1; i < argc && i < 16; i++) {
                prog_argv[i] = argv[i];
            }
            prog_argv[i] = NULL;

            int pid = launch_initrd_program_argv(prog_path, prog_argv);
            if (pid > 0) {
                extern int sys_waitpid(int pid, int *status, int options);
                extern bool desktop_is_running(void);
                extern void keyboard_flush(void);
                extern void desktop_force_redraw(void);
                int status = 0;
                sys_waitpid(pid, &status, 0);
                if (desktop_is_running()) {
                    keyboard_flush();
                    desktop_force_redraw();
                }
                return;
            }
        }
    }

    vga_puts("Command not found: ");
    vga_puts(argv[0]);
    vga_puts("\n");
    serial_print("Command not found: ");
    serial_print(argv[0]);
    serial_print("\n");
}
