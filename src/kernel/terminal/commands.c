#include "terminal.h"
#include "vga.h"
#include "serial.h"
#include "string.h"
#include "kheap.h"
#include "vfs.h"
#include "task.h"
#include "pmm.h"
#include "io.h"

extern task_t *current_task;

// Define command table structure for help
extern const terminal_command_t commands[];
extern const int NUM_COMMANDS; // Or pass this another way, I will just hardcode help text for now.

void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Available commands:\n");
    vga_puts("  help    - Shows this help message\n");
    vga_puts("  clear   - Clears the terminal screen\n");
    vga_puts("  echo    - Prints text to the terminal\n");
    vga_puts("  ls      - Lists files in the current directory\n");
    vga_puts("  pwd     - Prints the current working directory\n");
    vga_puts("  reboot  - Reboots the system\n");
    vga_puts("  version - Shows the OS version\n");
    vga_puts("  meminfo - Shows memory information\n");
}

void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        vga_puts(argv[i]);
        serial_print(argv[i]);
        if (i < argc - 1) {
            vga_putc(' ');
            serial_print(" ");
        }
    }
    vga_putc('\n');
    serial_print("\n");
}

void cmd_pwd(int argc, char **argv) {
    (void)argc; (void)argv;
    if (current_task) {
        vga_puts(current_task->cwd);
        vga_puts("\n");
        serial_print(current_task->cwd);
        serial_print("\n");
    }
}

void cmd_ls(int argc, char **argv) {
    const char *path = current_task ? current_task->cwd : "/";
    if (argc > 1) {
        path = argv[1];
    }
    
    fs_node_t *dir = vfs_open(path);
    if (!dir) {
        vga_puts("ls: cannot access '");
        vga_puts(path);
        vga_puts("': No such file or directory\n");
        return;
    }
    
    if (!(dir->flags & FS_DIRECTORY)) {
        vga_puts(path);
        vga_puts("\n");
        return;
    }
    
    struct dirent *node = 0;
    uint32_t i = 0;
    while ((node = readdir_fs(dir, i)) != 0) {
        // Just print name since we are in the directory
        vga_puts(node->name);
        vga_puts("  ");
        i++;
    }
    vga_puts("\n");
}

void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Rebooting system...\n");
    serial_print("Rebooting system...\n");
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    while (1) { asm volatile ("hlt"); }
}

void cmd_version(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("LiwusOS Kernel v1.0\n");
    serial_print("LiwusOS Kernel v1.0\n");
}

void cmd_meminfo(int argc, char **argv) {
    (void)argc; (void)argv;
    // pmm_get_free_memory is available? Let's just print a placeholder if not.
    vga_puts("Memory info not fully implemented.\n");
    serial_print("Memory info not fully implemented.\n");
}
