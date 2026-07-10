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
    vga_puts("  diskinfo- Shows disk space information\n");
    vga_puts("  ip      - Shows current IP address\n");
    vga_puts("  ping    - Send ICMP ECHO_REQUEST to network hosts\n");
    vga_puts("  wget    - Download files from the web (HTTP)\n");
    vga_puts("  host    - DNS lookup utility\n");
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
    extern uint64_t pmm_get_total_memory(void);
    extern uint64_t pmm_get_free_memory(void);
    extern uint64_t pmm_get_used_memory(void);
    
    int total_mb = (int)(pmm_get_total_memory() / (1024 * 1024));
    int free_mb  = (int)(pmm_get_free_memory() / (1024 * 1024));
    int used_mb  = (int)(pmm_get_used_memory() / (1024 * 1024));
    
    char buf[32];
    extern char *itoa(int value, char *str, int base);
    
    vga_puts("System Memory Information:\n");
    
    vga_puts("  Total RAM: ");
    itoa(total_mb, buf, 10);
    vga_puts(buf);
    vga_puts(" MB\n");
    
    vga_puts("  Used RAM:  ");
    itoa(used_mb, buf, 10);
    vga_puts(buf);
    vga_puts(" MB\n");
    
    vga_puts("  Free RAM:  ");
    itoa(free_mb, buf, 10);
    vga_puts(buf);
    vga_puts(" MB\n");
}

void cmd_diskinfo(int argc, char **argv) {
    (void)argc; (void)argv;
    extern void sdfs_get_usage(uint32_t *total_blocks, uint32_t *used_blocks);
    
    uint32_t total_blocks = 0, used_blocks = 0;
    sdfs_get_usage(&total_blocks, &used_blocks);
    
    if (total_blocks == 0) {
        vga_puts("No disk mounted or SDFS not initialized.\n");
        return;
    }
    
    uint32_t total_mb = (total_blocks * 4096) / (1024 * 1024);
    uint32_t used_mb = (used_blocks * 4096) / (1024 * 1024);
    uint32_t free_mb = total_mb - used_mb;
    
    bool use_kb = false;
    if (total_mb == 0) {
        use_kb = true;
        total_mb = (total_blocks * 4096) / 1024;
        used_mb = (used_blocks * 4096) / 1024;
        free_mb = total_mb - used_mb;
    }
    
    char buf[32];
    extern char *itoa(int value, char *str, int base);
    
    vga_puts("SDFS Disk Information:\n");
    
    vga_puts("  Total Disk: ");
    itoa(total_mb, buf, 10);
    vga_puts(buf);
    vga_puts(use_kb ? " KB\n" : " MB\n");
    
    vga_puts("  Used Disk:  ");
    itoa(used_mb, buf, 10);
    vga_puts(buf);
    vga_puts(use_kb ? " KB\n" : " MB\n");
    
    vga_puts("  Free Disk:  ");
    itoa(free_mb, buf, 10);
    vga_puts(buf);
    vga_puts(use_kb ? " KB\n" : " MB\n");
}

static void print_ip(uint32_t ip) {
    char buf[16];
    extern char *itoa(int value, char *str, int base);
    itoa((int)(ip & 0xFF), buf, 10);
    vga_puts(buf);
    vga_puts(".");
    itoa((int)((ip >> 8) & 0xFF), buf, 10);
    vga_puts(buf);
    vga_puts(".");
    itoa((int)((ip >> 16) & 0xFF), buf, 10);
    vga_puts(buf);
    vga_puts(".");
    itoa((int)((ip >> 24) & 0xFF), buf, 10);
    vga_puts(buf);
}

void cmd_ip(int argc, char **argv) {
    (void)argc; (void)argv;
    extern uint32_t netstack_get_my_ip(void);
    uint32_t my_ip = netstack_get_my_ip();
    vga_puts("eth0: ");
    if (my_ip == 0) {
        vga_puts("Offline (No IP)\n");
    } else {
        print_ip(my_ip);
        vga_puts("\n");
    }
}

void cmd_ping(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: ping <host or ip>\n");
        return;
    }
    extern uint32_t net_resolve_host(const char *host);
    extern int netstack_ping(uint32_t dest_ip, uint32_t timeout_ticks);
    
    vga_puts("Resolving ");
    vga_puts(argv[1]);
    vga_puts("...\n");
    
    uint32_t ip = net_resolve_host(argv[1]);
    if (ip == 0) {
        vga_puts("ping: unknown host ");
        vga_puts(argv[1]);
        vga_puts("\n");
        return;
    }
    
    vga_puts("PING ");
    print_ip(ip);
    vga_puts(" 32 bytes of data.\n");
    
    for (int i = 0; i < 4; i++) {
        int r = netstack_ping(ip, 20); // 20 ticks timeout (approx 2s at 10Hz)
        if (r >= 0) {
            vga_puts("Reply from ");
            print_ip(ip);
            vga_puts(": time=");
            char buf[16];
            extern char *itoa(int value, char *str, int base);
            itoa(r * 100, buf, 10); // Assume 1 tick = 100ms
            vga_puts(buf);
            vga_puts("ms\n");
        } else {
            vga_puts("Request timed out.\n");
        }
    }
}

void cmd_host(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: host <hostname>\n");
        return;
    }
    extern uint32_t net_resolve_host(const char *host);
    uint32_t ip = net_resolve_host(argv[1]);
    if (ip == 0) {
        vga_puts("Host ");
        vga_puts(argv[1]);
        vga_puts(" not found.\n");
    } else {
        vga_puts(argv[1]);
        vga_puts(" has address ");
        print_ip(ip);
        vga_puts("\n");
    }
}

void cmd_wget(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: wget <http://url>\n");
        return;
    }
    extern int http_get_url(const char *url, char *response, uint32_t max_len);
    
    vga_puts("Connecting to ");
    vga_puts(argv[1]);
    vga_puts("...\n");
    
    char *buf = (char *)kmalloc(8192);
    if (!buf) {
        vga_puts("wget: out of memory\n");
        return;
    }
    
    int bytes = http_get_url(argv[1], buf, 8191);
    if (bytes < 0) {
        vga_puts("wget: failed to connect or download.\n");
    } else {
        buf[bytes] = '\0';
        vga_puts("\n--- HTTP RESPONSE ---\n");
        vga_puts(buf);
        vga_puts("\n---------------------\n");
    }
    kfree(buf);
}
