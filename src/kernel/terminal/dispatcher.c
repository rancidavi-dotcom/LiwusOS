#include "terminal.h"
#include "string.h"
#include "vga.h"
#include "serial.h"

// Define command table
static const terminal_command_t commands[] = {
    {"help", cmd_help, "Shows this help message"},
    {"clear", cmd_clear, "Clears the terminal screen"},
    {"echo", cmd_echo, "Prints text to the terminal"},
    {"ls", cmd_ls, "Lists files in the current directory"},
    {"pwd", cmd_pwd, "Prints the current working directory"},
    {"reboot", cmd_reboot, "Reboots the system"},
    {"version", cmd_version, "Shows the OS version"},
    {"meminfo", cmd_meminfo, "Shows memory information"},
    {"diskinfo", cmd_diskinfo, "Shows disk space information"},
    {"ip", cmd_ip, "Shows current IP address"},
    {"ping", cmd_ping, "Send ICMP ECHO_REQUEST to network hosts"},
    {"wget", cmd_wget, "Download files from the web (HTTP)"},
    {"host", cmd_host, "DNS lookup utility"}
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
