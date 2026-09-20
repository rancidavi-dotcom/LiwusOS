#include "vga.h"
#include "keyboard.h"
#include "serial.h"
#include "io.h"
#include "string.h"
#include <stdint.h>

#define MAX_CMD_LEN 256
#define MAX_ARGS 16

static char cmd_buf[MAX_CMD_LEN];
static int cmd_pos = 0;

void shell_print_prompt(void) {
    vga_puts("liwusos> ");
    serial_print("liwusos> ");
}

void shell_execute(const char *cmd) {
    char *argv[MAX_ARGS];
    int argc = 0;
    char cmd_copy[MAX_CMD_LEN];
    strcpy(cmd_copy, cmd);

    char *token = cmd_copy;
    while (*token && argc < MAX_ARGS) {
        while (*token == ' ') token++;
        if (!*token) break;
        argv[argc++] = token;
        while (*token && *token != ' ') token++;
        if (*token) *token++ = '\0';
    }
    argv[argc] = NULL;

    if (argc == 0) return;

    serial_print("cmd: ");
    serial_print(argv[0]);
    serial_print("\n");

    if (strcmp(argv[0], "help") == 0) {
        vga_puts("Comandos disponiveis:\n");
        vga_puts("  help      - mostra esta ajuda\n");
        vga_puts("  clear     - limpa a tela\n");
        vga_puts("  echo      - ecoa argumentos\n");
        vga_puts("  reboot    - reinicia o sistema\n");
        vga_puts("  shutdown  - desliga o sistema\n");
        vga_puts("  mem       - mostra memoria\n");
        vga_puts("  cpuid     - info do CPU\n");
        vga_puts("  pci       - lista dispositivos PCI\n");
        vga_puts("  ps        - lista tarefas\n");
    }
    else if (strcmp(argv[0], "clear") == 0) {
        vga_puts("\033[2J\033[H");
    }
    else if (strcmp(argv[0], "echo") == 0) {
        for (int i = 1; i < argc; i++) {
            vga_puts(argv[i]);
            if (i < argc - 1) vga_puts(" ");
        }
        vga_puts("\n");
    }
    else if (strcmp(argv[0], "reboot") == 0) {
        vga_puts("Reiniciando...\n");
        outb(0xCF9, 0x06);
    }
    else if (strcmp(argv[0], "shutdown") == 0) {
        vga_puts("Desligando...\n");
        outw(0x604, 0x2000);
    }
    else if (strcmp(argv[0], "mem") == 0) {
        extern uint64_t memory_size;
        char buf[32];
        vga_puts("Memoria total: ");
        itoa(memory_size / 1024 / 1024, buf, 10);
        vga_puts(buf);
        vga_puts(" MB\n");
    }
    else if (strcmp(argv[0], "cpuid") == 0) {
        uint32_t eax, ebx, ecx, edx;
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(0));
        char buf[16];
        vga_puts("Vendor: ");
        vga_puts((char*)&ebx);
        vga_puts((char*)&edx);
        vga_puts((char*)&ecx);
        vga_puts("\n");
        asm volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
        vga_puts("Family: "); itoa((eax >> 8) & 0xF, buf, 10); vga_puts(buf);
        vga_puts(" Model: "); itoa((eax >> 4) & 0xF, buf, 10); vga_puts(buf);
        vga_puts(" Stepping: "); itoa(eax & 0xF, buf, 10); vga_puts(buf);
        vga_puts("\n");
    }
    else if (strcmp(argv[0], "pci") == 0) {
        vga_puts("PCI list not implemented\n");
    }
    else if (strcmp(argv[0], "ps") == 0) {
        extern int task_snapshot(void *out, int max);
        typedef struct { int id, parent_id, state; uint64_t heap_start, heap_end, cpu_ticks, switch_count; int priority; int user_mode; char name[32]; } task_info_t;
        task_info_t tasks[16];
        int count = task_snapshot(tasks, 16);
        vga_puts("PID  NAME           STATE  PRIO  USER\n");
        for (int i = 0; i < count; i++) {
            char buf[16];
            itoa(tasks[i].id, buf, 10); vga_puts(buf); vga_puts("  ");
            vga_puts(tasks[i].name); vga_puts("  ");
            const char *st = tasks[i].state == 0 ? "RUN" : tasks[i].state == 1 ? "RDY" : tasks[i].state == 2 ? "SLP" : "ZMB";
            vga_puts(st); vga_puts("  ");
            itoa(tasks[i].priority, buf, 10); vga_puts(buf); vga_puts("  ");
            vga_puts(tasks[i].user_mode ? "yes" : "no");
            vga_puts("\n");
        }
    }
    else {
        vga_puts("Comando desconhecido: ");
        vga_puts(argv[0]);
        vga_puts("\nDigite 'help' para ajuda.\n");
    }
}

void text_shell(void) {
    vga_puts("=== LiwusOS Text Shell ===\n");
    vga_puts("Digite 'help' para lista de comandos\n\n");

    shell_print_prompt();

    while (1) {
        char c = get_last_key();
        if (c) {
            if (c == '\n' || c == '\r') {
                vga_puts("\n");
                serial_print("\n");
                cmd_buf[cmd_pos] = '\0';
                if (cmd_pos > 0) {
                    shell_execute(cmd_buf);
                }
                cmd_pos = 0;
                shell_print_prompt();
            }
            else if (c == '\b' || c == 127) {
                if (cmd_pos > 0) {
                    cmd_pos--;
                    vga_puts("\b \b");
                    serial_print("\b \b");
                }
            }
            else if (cmd_pos < MAX_CMD_LEN - 1) {
                cmd_buf[cmd_pos++] = c;
                vga_putc(c);
                char tmp[2] = {c, 0};
        serial_print(tmp);
            }
        }
        asm volatile("hlt");
    }
}