#include "terminal.h"
#include "vga.h"
#include "serial.h"
#include "string.h"
#include "kheap.h"
#include "vfs.h"
#include "task.h"
#include "pmm.h"
#include "io.h"
#include "fs/sdfs.h"
#include "net.h"
#include "netstack.h"
#include "http.h"
#include "tcp.h"
#include "timer.h"
#include "acpi.h"
#include "keyboard.h"
#include "mouse.h"

extern char *itoa(int value, char *str, int base);

static void vga_print_ip(uint32_t ip) {
    char b[16];
    itoa((int)(ip & 0xFF), b, 10); vga_puts(b); vga_puts(".");
    itoa((int)((ip >> 8) & 0xFF), b, 10); vga_puts(b); vga_puts(".");
    itoa((int)((ip >> 16) & 0xFF), b, 10); vga_puts(b); vga_puts(".");
    itoa((int)((ip >> 24) & 0xFF), b, 10); vga_puts(b);
}

static void join_path(const char *base, const char *rel, char *out, size_t out_size) {
    if (rel[0] == '/') {
        strncpy(out, rel, out_size - 1);
    } else {
        size_t base_len = strlen(base);
        size_t rel_len = strlen(rel);
        if (base_len + 1 + rel_len >= out_size) {
            strncpy(out, rel, out_size - 1);
        } else {
            strcpy(out, base);
            if (base_len > 0 && base[base_len - 1] != '/') {
                out[base_len] = '/';
                strcpy(out + base_len + 1, rel);
            } else {
                strcpy(out + base_len, rel);
            }
        }
    }
    out[out_size - 1] = '\0';
}

static void vfs_to_sdfs_path(const char *vfs_path, char *out, size_t out_size) {
    const char *mount = "/house/localhost";
    size_t mount_len = strlen(mount);
    if (strncmp(vfs_path, mount, mount_len) == 0) {
        const char *rel = vfs_path + mount_len;
        if (*rel == '/') {
            strncpy(out, rel, out_size - 1);
        } else {
            // at mount root, use "/"
            strncpy(out, "/", out_size - 1);
        }
    } else {
        // not under SDFS mount, use as-is (will fail anyway)
        strncpy(out, vfs_path, out_size - 1);
    }
    out[out_size - 1] = '\0';
}

// Define command table structure for help
extern const terminal_command_t commands[];
extern const int NUM_COMMANDS; // Or pass this another way, I will just hardcode help text for now.

void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Available commands:\n");
    vga_puts("  help    - Shows this help message\n");
    vga_puts("  clear   - Clears the terminal screen\n");
    vga_puts("  echo    - Prints text to the terminal\n");
    vga_puts("  sed     - Write text to file (sed \"texto\" > arquivo.txt)\n");
    vga_puts("  ls      - Lists files in the current directory\n");
    vga_puts("  cd      - Change directory\n");
    vga_puts("  pwd     - Prints the current working directory\n");
    vga_puts("  cat     - Display file contents\n");
    vga_puts("  mkdir   - Create directory\n");
    vga_puts("  touch   - Create empty file\n");
    vga_puts("  rm      - Remove file or directory\n");
    vga_puts("  mv      - Move/rename file\n");
    vga_puts("  cp      - Copy file\n");
    vga_puts("  reboot  - Reboots the system\n");
    vga_puts("  version - Shows the OS version\n");
    vga_puts("  meminfo - Shows memory information\n");
    vga_puts("  ps      - Lists tasks (pid, priority, state, cpu)\n");
    vga_puts("  diskinfo- Shows disk space information\n");
    vga_puts("  ip      - Shows current IP address\n");
    vga_puts("  ping    - Send ICMP ECHO_REQUEST to network hosts\n");
    vga_puts("  scan    - Scan the local network and list devices found\n");
    vga_puts("  wget    - Download files from the web (HTTP)\n");
    vga_puts("  host    - DNS lookup utility\n");
    vga_puts("  httpd   - Serve the filesystem over HTTP (httpd [port])\n");
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

/*
 * sed "texto" > arquivo.txt          -> cria/sobrescreve arquivo com o texto
 * sed "texto" >> arquivo.txt         -> acrescenta o texto ao final do arquivo
 * sed "linha1\nlinha2" > arquivo.txt -> \n vira quebra de linha
 */
void cmd_sed(int argc, char **argv) {
    int append = 0;
    int file_arg = -1;
    int text_end = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], ">") == 0 || strcmp(argv[i], ">>") == 0) {
            append = (argv[i][1] == '>');
            file_arg = i + 1;
            text_end = i;
            break;
        }
    }

    if (file_arg <= 0 || file_arg >= argc) {
        vga_puts("Usage: sed \"texto\" > arquivo.txt\n");
        vga_puts("       sed \"texto\" >> arquivo.txt   (append)\n");
        return;
    }

    /* Junta os tokens antes do '>' com espacos */
    static char text[1024];
    int n = 0;
    for (int i = 1; i < text_end; i++) {
        const char *a = argv[i];
        while (*a && n < (int)sizeof(text) - 2) {
            if (*a == '\\' && (a[1] == 'n' || a[1] == 't')) {
                text[n++] = (a[1] == 'n') ? '\n' : '\t';
                a += 2;
                continue;
            }
            text[n++] = *a++;
        }
        if (i < text_end - 1 && n < (int)sizeof(text) - 2) text[n++] = ' ';
    }
    text[n] = '\0';

    /* Resolve o caminho (VFS) do arquivo de destino */
    char path[256];
    if (argv[file_arg][0] == '/') {
        strncpy(path, argv[file_arg], sizeof(path) - 1);
    } else if (current_task) {
        join_path(current_task->cwd, argv[file_arg], path, sizeof(path));
    } else {
        strncpy(path, argv[file_arg], sizeof(path) - 1);
    }
    path[sizeof(path) - 1] = '\0';

    char sdfs_path[256];
    vfs_to_sdfs_path(path, sdfs_path, sizeof(sdfs_path));

    uint32_t written;
    if (append) {
        uint32_t old_size = 0;
        uint8_t *old = (uint8_t *)sdfs_read_file(sdfs_path, &old_size);
        uint32_t total = old_size + (n > 0 ? 1 : 0) + (uint32_t)n;
        uint8_t *out = (uint8_t *)kmalloc(total ? total : 1);
        if (!out) {
            vga_puts("sed: Out of memory\n");
            if (old) kfree(old);
            return;
        }
        uint32_t pos = 0;
        if (old_size > 0) {
            memcpy(out + pos, old, old_size);
            pos += old_size;
            if (old[old_size - 1] != '\n') out[pos++] = '\n';
        }
        memcpy(out + pos, text, (size_t)n);
        pos += (uint32_t)n;
        if (old) kfree(old);
        written = sdfs_write_file(sdfs_path, out, pos);
        kfree(out);
    } else {
        sdfs_create_file(sdfs_path);
        written = sdfs_write_file(sdfs_path, (uint8_t *)text, (uint32_t)n);
    }

    char b[16];
    vga_puts("sed: wrote ");
    itoa((int)written, b, 10); vga_puts(b);
    vga_puts(" bytes to ");
    vga_puts(argv[file_arg]);
    vga_puts("\n");
    serial_print("sed: wrote to ");
    serial_print(argv[file_arg]);
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
        vga_puts(node->name);
        vga_puts("  ");
        i++;
    }
    vga_puts("\n");
}

static void resolve_path(const char *cwd, const char *path, char *out, size_t out_size) {
    if (strcmp(path, ".") == 0) {
        strncpy(out, cwd, out_size - 1);
        out[out_size - 1] = '\0';
        return;
    }
    if (strcmp(path, "..") == 0) {
        char *last_slash = strrchr(cwd, '/');
        if (last_slash && last_slash != cwd) {
            *last_slash = '\0';
            strncpy(out, cwd, out_size - 1);
            *last_slash = '/';
        } else {
            strncpy(out, "/", out_size - 1);
        }
        out[out_size - 1] = '\0';
        return;
    }
    if (path[0] == '/') {
        strncpy(out, path, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        join_path(cwd, path, out, out_size);
    }
}

void cmd_cd(int argc, char **argv) {
    if (!current_task) return;
    const char *path = argc > 1 ? argv[1] : "/";
    
    char resolved[256];
    resolve_path(current_task->cwd, path, resolved, sizeof(resolved));

    /* Normalize: strip trailing slashes (except root "/") so paths from
     * completion (e.g. "Nova Pasta/") resolve cleanly in SDFS. */
    size_t rlen = strlen(resolved);
    while (rlen > 1 && resolved[rlen - 1] == '/') {
        resolved[rlen - 1] = '\0';
        rlen--;
    }

    fs_node_t *dir = vfs_open(resolved);
    if (!dir) {
        vga_puts("cd: ");
        vga_puts(path);
        vga_puts(": No such file or directory\n");
        return;
    }
    
    if (!(dir->flags & FS_DIRECTORY)) {
        vga_puts("cd: ");
        vga_puts(path);
        vga_puts(": Not a directory\n");
        return;
    }
    
    strncpy(current_task->cwd, resolved, sizeof(current_task->cwd) - 1);
    current_task->cwd[sizeof(current_task->cwd) - 1] = '\0';
}

void cmd_cat(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: cat <file>\n");
        return;
    }
    
    char path[256];
    if (argv[1][0] == '/') {
        strncpy(path, argv[1], sizeof(path) - 1);
    } else if (current_task) {
        join_path(current_task->cwd, argv[1], path, sizeof(path));
    } else {
        strncpy(path, argv[1], sizeof(path) - 1);
    }
    path[sizeof(path) - 1] = '\0';
    
    fs_node_t *file = vfs_open(path);
    if (!file) {
        vga_puts("cat: ");
        vga_puts(argv[1]);
        vga_puts(": No such file or directory\n");
        return;
    }
    
    if (file->flags & FS_DIRECTORY) {
        vga_puts("cat: ");
        vga_puts(argv[1]);
        vga_puts(": Is a directory\n");
        return;
    }
    
    uint32_t size = file->length;
    if (size == 0) return;
    
    void *buf = kmalloc(size + 1);
    if (!buf) {
        vga_puts("cat: Out of memory\n");
        return;
    }
    
    if (read_fs(file, 0, size, buf) > 0) {
        ((char *)buf)[size] = '\0';
        vga_puts((char *)buf);
    }
    kfree(buf);
}

void cmd_mkdir(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: mkdir <dir>\n");
        return;
    }
    
    char path[256];
    if (argv[1][0] == '/') {
        strncpy(path, argv[1], sizeof(path) - 1);
    } else if (current_task) {
        join_path(current_task->cwd, argv[1], path, sizeof(path));
    } else {
        strncpy(path, argv[1], sizeof(path) - 1);
    }
    path[sizeof(path) - 1] = '\0';
    
    char sdfs_path[256];
    vfs_to_sdfs_path(path, sdfs_path, sizeof(sdfs_path));
    
    int ret = sdfs_create_dir(sdfs_path);
    if (ret != 0) {
        vga_puts("mkdir: Failed to create directory\n");
    }
}

void cmd_touch(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: touch <file>\n");
        return;
    }
    
    char path[256];
    if (argv[1][0] == '/') {
        strncpy(path, argv[1], sizeof(path) - 1);
    } else if (current_task) {
        join_path(current_task->cwd, argv[1], path, sizeof(path));
    } else {
        strncpy(path, argv[1], sizeof(path) - 1);
    }
    path[sizeof(path) - 1] = '\0';
    
    fs_node_t *file = vfs_open(path);
    if (file) {
        return;
    }
    
    fs_node_t *new_file = vfs_create(path, 0644);
    if (!new_file) {
        vga_puts("touch: Failed to create file\n");
    }
}

void cmd_rm(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: rm <file|dir>\n");
        return;
    }
    
    char path[256];
    if (argv[1][0] == '/') {
        strncpy(path, argv[1], sizeof(path) - 1);
    } else if (current_task) {
        join_path(current_task->cwd, argv[1], path, sizeof(path));
    } else {
        strncpy(path, argv[1], sizeof(path) - 1);
    }
    path[sizeof(path) - 1] = '\0';
    
    char sdfs_path[256];
    vfs_to_sdfs_path(path, sdfs_path, sizeof(sdfs_path));
    
    int ret = sdfs_delete(sdfs_path);
    if (ret != 0) {
        vga_puts("rm: Failed to remove\n");
    }
}

void cmd_mv(int argc, char **argv) {
    if (argc < 3) {
        vga_puts("Usage: mv <src> <dst>\n");
        return;
    }
    
    char src[256], dst[256];
    if (argv[1][0] == '/') strncpy(src, argv[1], sizeof(src) - 1);
    else if (current_task) join_path(current_task->cwd, argv[1], src, sizeof(src));
    else strncpy(src, argv[1], sizeof(src) - 1);
    src[sizeof(src) - 1] = '\0';
    
    if (argv[2][0] == '/') strncpy(dst, argv[2], sizeof(dst) - 1);
    else if (current_task) join_path(current_task->cwd, argv[2], dst, sizeof(dst));
    else strncpy(dst, argv[2], sizeof(dst) - 1);
    dst[sizeof(dst) - 1] = '\0';
    
    char sdfs_src[256], sdfs_dst[256];
    vfs_to_sdfs_path(src, sdfs_src, sizeof(sdfs_src));
    vfs_to_sdfs_path(dst, sdfs_dst, sizeof(sdfs_dst));
    
    int ret = sdfs_rename(sdfs_src, sdfs_dst);
    if (ret != 0) {
        vga_puts("mv: Failed to move/rename\n");
    }
}

void cmd_cp(int argc, char **argv) {
    if (argc < 3) {
        vga_puts("Usage: cp <src> <dst>\n");
        return;
    }
    
    char src[256], dst[256];
    if (argv[1][0] == '/') strncpy(src, argv[1], sizeof(src) - 1);
    else if (current_task) join_path(current_task->cwd, argv[1], src, sizeof(src));
    else strncpy(src, argv[1], sizeof(src) - 1);
    src[sizeof(src) - 1] = '\0';
    
    if (argv[2][0] == '/') strncpy(dst, argv[2], sizeof(dst) - 1);
    else if (current_task) join_path(current_task->cwd, argv[2], dst, sizeof(dst));
    else strncpy(dst, argv[2], sizeof(dst) - 1);
    dst[sizeof(dst) - 1] = '\0';
    
    fs_node_t *src_file = vfs_open(src);
    if (!src_file) {
        vga_puts("cp: Source not found\n");
        return;
    }
    
    if (src_file->flags & FS_DIRECTORY) {
        vga_puts("cp: Directories not supported\n");
        return;
    }
    
    uint32_t size = src_file->length;
    void *buf = kmalloc(size);
    if (!buf) {
        vga_puts("cp: Out of memory\n");
        return;
    }
    
    if (read_fs(src_file, 0, size, buf) > 0) {
        fs_node_t *dst_file = vfs_create(dst, 0644);
        if (dst_file) {
            write_fs(dst_file, 0, size, buf);
        } else {
            vga_puts("cp: Failed to create destination\n");
        }
    }
    kfree(buf);
}

void cmd_reboot(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Rebooting system...\n");
    serial_print("Rebooting system...\n");
    if (acpi_reboot()) {
        while (1) { asm volatile ("hlt"); }
    }
    uint8_t good = 0x02;
    while (good & 0x02)
        good = inb(0x64);
    outb(0x64, 0xFE);
    while (1) { asm volatile ("hlt"); }
}

void cmd_shutdown(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Shutting down (ACPI S5)...\n");
    serial_print("Shutting down (ACPI S5)...\n");
    if (acpi_poweroff()) {
        while (1) { asm volatile ("hlt"); }
    }
    vga_puts("ACPI poweroff indisponivel; reiniciando.\n");
    serial_print("ACPI poweroff indisponivel; reiniciando.\n");
    cmd_reboot(argc, argv);
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

void cmd_ps(int argc, char **argv) {
    (void)argc; (void)argv;
    task_info_t tasks[48];
    extern char *itoa(int value, char *str, int base);

    int n = task_snapshot(tasks, 48);

    vga_puts("PID  PRIO  STATE  SWITCH  CPU   NAME\n");
    serial_print("PID  PRIO  STATE  SWITCH  CPU   NAME\n");

    char b[24];
    for (int i = 0; i < n; i++) {
        itoa(tasks[i].id, b, 10);
        vga_puts(b); serial_print(b); vga_puts("  ");

        itoa(tasks[i].priority, b, 10);
        vga_puts(b); serial_print(b); vga_puts("     ");

        vga_puts(task_state_name(tasks[i].state));
        serial_print(task_state_name(tasks[i].state));
        vga_puts("   ");

        itoa((int)tasks[i].switch_count, b, 10);
        vga_puts(b); serial_print(b); vga_puts("  ");

        itoa((int)tasks[i].cpu_ticks, b, 10);
        vga_puts(b); serial_print(b); vga_puts("  ");

        vga_puts(tasks[i].name);
        serial_print(tasks[i].name);
        vga_puts("\n");
        serial_print("\n");
    }
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

void cmd_ip(int argc, char **argv) {
    (void)argc; (void)argv;
    net_interface_t *netif = net_get_list();
    if (!netif) {
        vga_puts("No network interface registered.\n");
        return;
    }
    while (netif) {
        vga_puts(netif->name);
        vga_puts("  ");
        vga_puts(netif->type == NET_TYPE_ETHERNET ? "ethernet" : "wifi");
        vga_puts("  mac ");
        char b[16];
        for (int i = 0; i < 6; i++) {
            itoa(netif->mac[i], b, 16);
            if (strlen(b) == 1) vga_puts("0");
            vga_puts(b);
            if (i < 5) vga_puts(":");
        }
        vga_puts("  ip ");
        vga_print_ip(netstack_get_my_ip());
        vga_puts("\n");
        netif = netif->next;
    }
}

void cmd_ping(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: ping <host|ip> [count]\n");
        return;
    }
    if (!net_get_list()) {
        vga_puts("Rede indisponivel.\n");
        return;
    }

    int count = 4;
    if (argc >= 3) {
        count = 0;
        for (int i = 0; argv[2][i] >= '0' && argv[2][i] <= '9'; i++)
            count = count * 10 + (argv[2][i] - '0');
        if (count <= 0) count = 4;
    }

    uint32_t ip = net_resolve_host(argv[1]);
    vga_puts("PING ");
    vga_puts(argv[1]);
    vga_puts(" (");
    vga_print_ip(ip);
    vga_puts(")\n");

    int sent = 0, received = 0;
    uint32_t total_ms = 0;
    int elapsed_ticks;
    for (int i = 0; i < count; i++) {
        sent++;
        elapsed_ticks = netstack_ping(ip, 200);
        if (elapsed_ticks >= 0) {
            uint32_t elapsed_ms = (uint32_t)elapsed_ticks * 10U;
            received++;
            total_ms += elapsed_ms;
            vga_puts("64 bytes from ");
            vga_print_ip(ip);
            vga_puts(": icmp_seq=");
            char b[16];
            itoa(i + 1, b, 10); vga_puts(b);
            vga_puts(" time=");
            itoa((int)elapsed_ms, b, 10); vga_puts(b);
            vga_puts(" ms\n");
        } else {
            vga_puts("Request timeout for icmp_seq=");
            char b[16];
            itoa(i + 1, b, 10); vga_puts(b);
            vga_puts("\n");
        }
    }

    vga_puts("--- ping statistics ---\n");
    char b[16];
    itoa(sent, b, 10); vga_puts(b);
    vga_puts(" packets transmitted, ");
    itoa(received, b, 10); vga_puts(b);
    vga_puts(" received, ");
    itoa(sent - received, b, 10); vga_puts(b);
    vga_puts(" lost\n");
    if (received > 0) {
        vga_puts("avg time = ");
        itoa((int)(total_ms / (uint32_t)received), b, 10); vga_puts(b);
        vga_puts(" ms\n");
    }
}

void cmd_scan(int argc, char **argv) {
    (void)argc; (void)argv;
    if (!net_get_list()) {
        vga_puts("Rede indisponivel.\n");
        return;
    }

    vga_puts("Scanning local network...\n");
    int found = netstack_scan_net();

    char b[16];
    vga_puts("\nDevices found: ");
    itoa(found, b, 10);
    vga_puts(b);
    vga_puts("\n\n");

    vga_puts("  This host: ");
    vga_print_ip(netstack_get_my_ip());
    vga_puts(" / ");
    vga_print_ip(netstack_get_netmask());
    vga_puts("\n");
    vga_puts("  Gateway  : ");
    vga_print_ip(netstack_get_gateway());
    vga_puts("\n\n");

    uint32_t gw = netstack_get_gateway();
    uint32_t self = netstack_get_my_ip();
    for (int idx = 0; idx < netstack_arp_cache_count(); idx++) {
        uint32_t ip;
        uint8_t mac[6];
        if (netstack_arp_cache_get(idx, &ip, mac) != 0) continue;
        vga_puts("  ");
        vga_print_ip(ip);
        vga_puts("  ");
        char h[8];
        for (int k = 0; k < 6; k++) {
            itoa(mac[k] >> 4, h, 16); vga_puts(h);
            itoa(mac[k] & 0xF, h, 16); vga_puts(h);
            if (k < 5) vga_puts(":");
        }
        if (ip == gw) vga_puts("   (gateway)");
        if (ip == self) vga_puts("   (this host)");
        vga_puts("\n");
    }
}

void cmd_wget(int argc, char **argv) {
    static char response[16384];
    int got;

    if (argc < 2) {
        vga_puts("Usage: wget <url> [arquivo]\n");
    } else if (!net_get_list()) {
        vga_puts("Rede indisponivel.\n");
    } else if (strstr(argv[1], "https://") == argv[1]) {
        vga_puts("HTTPS ainda nao e suportado. Use URLs http:// por enquanto.\n");
    } else {
        vga_puts("Baixando ");
        vga_puts(argv[1]);
        vga_puts(" ...\n");

        memset(response, 0, sizeof(response));
        got = http_get_url(argv[1], response, sizeof(response) - 1);
        if (got < 0) {
            vga_puts("Falha no download.\n");
        } else if (argc >= 3) {
            char sdfs_path[256];
            if (argv[2][0] == '/') {
                strncpy(sdfs_path, argv[2], sizeof(sdfs_path) - 1);
            } else {
                vfs_to_sdfs_path(argv[2], sdfs_path, sizeof(sdfs_path));
            }
            sdfs_path[sizeof(sdfs_path) - 1] = '\0';
            sdfs_create_file(sdfs_path);
            sdfs_write_file(sdfs_path, (uint8_t *)response, (uint32_t)got);
            vga_puts("Salvo em ");
            vga_puts(argv[2]);
            vga_puts(" (");
            char b[16];
            itoa(got, b, 10); vga_puts(b);
            vga_puts(" bytes)\n");
        } else {
            vga_puts("Download concluido (");
            char b[16];
            itoa(got, b, 10); vga_puts(b);
            vga_puts(" bytes), mostrando...\n");
            vga_puts(response);
            vga_puts("\n");
        }
    }
}

void cmd_host(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: host <hostname>\n");
        return;
    }
    uint32_t ip = net_resolve_host(argv[1]);
    if (ip == 0) {
        vga_puts("host: resolution failed\n");
        return;
    }
    vga_puts(argv[1]);
    vga_puts(" -> ");
    vga_print_ip(ip);
    vga_puts("\n");
}

/* ============================ httpd ================================ */
static const char *httpd_content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (strcmp(dot, ".html") == 0 || strcmp(dot, ".htm") == 0) return "text/html";
    if (strcmp(dot, ".txt") == 0) return "text/plain";
    if (strcmp(dot, ".css") == 0) return "text/css";
    if (strcmp(dot, ".js") == 0) return "text/javascript";
    if (strcmp(dot, ".png") == 0) return "image/png";
    if (strcmp(dot, ".jpg") == 0 || strcmp(dot, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dot, ".gif") == 0) return "image/gif";
    return "application/octet-stream";
}

static uint32_t httpd_append(char *buf, uint32_t max, uint32_t pos, const char *s) {
    uint32_t sl = strlen(s);
    if (pos + sl >= max) sl = max - 1 - pos;
    memcpy(buf + pos, s, sl);
    buf[pos + sl] = '\0';
    return pos + sl;
}

static uint32_t httpd_append_num(char *buf, uint32_t max, uint32_t pos, int value) {
    char nb[16];
    itoa(value, nb, 10);
    return httpd_append(buf, max, pos, nb);
}

static void httpd_escape(const char *in, char *out, uint32_t out_size) {
    uint32_t o = 0;
    while (*in && o + 8 < out_size) {
        if (*in == '<') { memcpy(out + o, "&lt;", 4); o += 4; }
        else if (*in == '>') { memcpy(out + o, "&gt;", 4); o += 4; }
        else if (*in == '&') { memcpy(out + o, "&amp;", 5); o += 5; }
        else if (*in == '"') { memcpy(out + o, "&quot;", 6); o += 6; }
        else out[o++] = *in;
        in++;
    }
    out[o] = '\0';
}

static void httpd_send_acked(tcp_socket_t *sock, const uint8_t *data,
                             uint32_t len) {
  uint32_t seq = tcp_send(sock, data, len);
  uint32_t target = seq + len;
  int attempts = 0;
  while (sock->ack_received < target && attempts < 12) {
    uint32_t start = timer_ticks;
    while (sock->ack_received < target && (timer_ticks - start) < 25) {
      switch_task();
    }
    if (sock->ack_received >= target) return;
    attempts++;
    tcp_resend(sock, seq, data, len);
  }
}

static void httpd_send_text(tcp_socket_t *sock, const char *status, const char *body) {
    char hdr[160];
    char cl[16];
    itoa(strlen(body), cl, 10);
    strcpy(hdr, "HTTP/1.1 ");
    strcat(hdr, status);
    strcat(hdr, "\r\nContent-Type: text/html\r\nContent-Length: ");
    strcat(hdr, cl);
    strcat(hdr, "\r\nConnection: close\r\nServer: LiwusOS\r\n\r\n");
    httpd_send_acked(sock, (const uint8_t *)hdr, strlen(hdr));
    httpd_send_acked(sock, (const uint8_t *)body, strlen(body));
}

static void httpd_list_dir(tcp_socket_t *sock, const char *vpath, fs_node_t *node) {
    uint32_t count = 0;
    while (readdir_fs(node, count)) count++;

    char *page = (char *)kmalloc(65536);
    if (!page) {
        httpd_send_text(sock, "500 Internal Server Error",
                        "<html><body><h1>500 - sem memoria</h1></body></html>");
        return;
    }
    uint32_t pos = httpd_append(page, 65536, 0,
        "<html><head><title>LiwusOS</title></head><body><h1>Indice de ");
    pos = httpd_append(page, 65536, pos, vpath);
    pos = httpd_append(page, 65536, pos, "</h1><hr><ul>");

    for (uint32_t i = 0; i < count; i++) {
        struct dirent *ent = readdir_fs(node, i);
        if (!ent) break;

        char href[512];
        if (strcmp(vpath, "/") == 0) {
            strcpy(href, "/");
        } else {
            strncpy(href, vpath, sizeof(href) - 1);
            href[sizeof(href) - 1] = '\0';
        }
        uint32_t hl = strlen(href);
        if (hl > 0 && href[hl - 1] != '/') { href[hl] = '/'; href[hl + 1] = '\0'; }
        strncat(href, ent->name, sizeof(href) - strlen(href) - 1);

        char esc[160];
        httpd_escape(ent->name, esc, sizeof(esc));

        pos = httpd_append(page, 65536, pos, "<li><a href=\"");
        pos = httpd_append(page, 65536, pos, href);
        pos = httpd_append(page, 65536, pos, "\">");
        pos = httpd_append(page, 65536, pos, esc);
        pos = httpd_append(page, 65536, pos, "</a>");

        if (strcmp(ent->name, ".") != 0 && strcmp(ent->name, "..") != 0) {
            fs_node_t *child = finddir_fs(node, ent->name);
            if (child && !(child->flags & FS_DIRECTORY)) {
                pos = httpd_append(page, 65536, pos, " <i>(");
                pos = httpd_append_num(page, 65536, pos, (int)child->length);
                pos = httpd_append(page, 65536, pos, " bytes)</i>");
            }
        }
        pos = httpd_append(page, 65536, pos, "</li>\n");
    }
    pos = httpd_append(page, 65536, pos, "</ul><hr><i>LiwusOS httpd</i></body></html>");
    page[pos] = '\0';

    httpd_send_text(sock, "200 OK", page);
    kfree(page);
}

static void httpd_serve_file(tcp_socket_t *sock, const char *vpath, fs_node_t *f) {
    const char *ct = httpd_content_type(vpath);
    uint32_t size = f->length;

    char hdr[256];
    char cl[16];
    itoa(size, cl, 10);
    strcpy(hdr, "HTTP/1.1 200 OK\r\nContent-Type: ");
    strcat(hdr, ct);
    strcat(hdr, "\r\nContent-Length: ");
    strcat(hdr, cl);
    strcat(hdr, "\r\nConnection: close\r\nServer: LiwusOS\r\n\r\n");
    httpd_send_acked(sock, (const uint8_t *)hdr, strlen(hdr));

    uint8_t *chunk = (uint8_t *)kmalloc(1024);
    if (!chunk) return;
    uint32_t off = 0;
    while (off < size) {
        uint32_t n = read_fs(f, off, 1024, chunk);
        if (n == 0) break;
        httpd_send_acked(sock, chunk, n);
        off += n;
    }
    kfree(chunk);
}

static bool httpd_is_running = false;
static uint16_t httpd_port = 80;

static void httpd_server_task(void) {
    uint16_t port = httpd_port;

    static char req[4096];
    static char tmp[1024];
    static char path[256];

    tcp_socket_t *listener = tcp_listen(port);
    if (!listener) {
        serial_print("[httpd] falha ao criar listener\n");
        return;
    }

    serial_print("[httpd] servindo filesystem em http://");
    {
        char bip[16];
        uint32_t ip = netstack_get_my_ip();
        itoa((int)(ip & 0xFF), bip, 10); serial_print(bip); serial_print(".");
        itoa((int)((ip >> 8) & 0xFF), bip, 10); serial_print(bip); serial_print(".");
        itoa((int)((ip >> 16) & 0xFF), bip, 10); serial_print(bip); serial_print(".");
        itoa((int)((ip >> 24) & 0xFF), bip, 10); serial_print(bip);
    }
    char b[16];
    itoa(port, b, 10);
    serial_print(":");
    serial_print(b);
    serial_print("/\n");

    for (;;) {
        tcp_socket_t *sock = NULL;
        uint32_t accept_start = timer_ticks;
        while ((sock = tcp_accept(port)) == NULL) {
            if ((timer_ticks - accept_start) > 4000) break;
            switch_task();
        }
        if (!sock) continue; // timeout, nenhuma conexao nova

        uint32_t len = 0;
        uint32_t idle = timer_ticks;
        while (len < sizeof(req) - 1) {
            int n = tcp_receive(sock, (uint8_t *)tmp, sizeof(tmp));
            if (n > 0) {
                if (len + (uint32_t)n >= sizeof(req)) n = (int)(sizeof(req) - 1 - len);
                memcpy(req + len, tmp, (uint32_t)n);
                len += n;
                req[len] = '\0';
                if (strstr(req, "\r\n\r\n") || strstr(req, "\n\n")) break;
                idle = timer_ticks;
            } else if (n < 0) {
                break;
            } else if ((timer_ticks - idle) > 300) {
                break;
            }
            switch_task();
        }
        req[len] = '\0';

        path[0] = '\0';
        if (len > 4 && strncmp(req, "GET ", 4) == 0) {
            char *sp = strchr(req + 4, ' ');
            if (sp && sp > req + 4) {
                uint32_t plen = (uint32_t)(sp - (req + 4));
                if (plen >= sizeof(path)) plen = sizeof(path) - 1;
                memcpy(path, req + 4, plen);
                path[plen] = '\0';
                char *q = strchr(path, '?');
                if (q) *q = '\0';
            }
        }
        if (path[0] == '\0') strcpy(path, "/");

        serial_print("[httpd] GET ");
        serial_print(path);
        serial_print("\n");

        fs_node_t *f = vfs_open(path);
        if (!f) {
            httpd_send_text(sock, "404 Not Found",
                "<html><body><h1>404 - nao achado</h1><p>LiwusOS httpd</p></body></html>");
        } else if (f->flags & FS_DIRECTORY) {
            httpd_list_dir(sock, path, f);
        } else {
            httpd_serve_file(sock, path, f);
        }

        tcp_close(sock);
        uint32_t wait = timer_ticks + 10;
        while (timer_ticks < wait) switch_task();
    }
}

void cmd_httpd(int argc, char **argv) {
    uint16_t port = 80;
    if (argc >= 2) {
        uint16_t p = 0;
        const char *d = argv[1];
        while (*d >= '0' && *d <= '9') { p = (uint16_t)(p * 10 + (*d - '0')); d++; }
        if (p) port = p;
    }

    if (httpd_is_running) {
        serial_print("[httpd] ja rodando em background na porta ");
        vga_puts("[httpd] ja rodando na porta ");
        char b[16];
        itoa(httpd_port, b, 10);
        serial_print(b);
        vga_puts(b);
        serial_print("\n");
        vga_puts("\n");
        return;
    }

    httpd_is_running = true;
    httpd_port = port;
    create_task_named(httpd_server_task, "httpd");

    serial_print("[httpd] iniciado na porta ");
    vga_puts("[httpd] iniciado (background) na porta ");
    {
        char b[16];
        itoa(port, b, 10);
        serial_print(b);
        vga_puts(b);
    }
    serial_print(" em background\n");
    vga_puts("\n");
}

void cmd_kblayout(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Current layout: ");
        vga_puts(keyboard_layout_name(keyboard_get_layout()));
        vga_puts("\n");
        vga_puts("Available layouts:\n");
        vga_puts("  abnt2     - ABNT2 (Portuguese Brazil)\n");
        vga_puts("  us        - US QWERTY\n");
        vga_puts("  us-intl   - US International (with dead keys)\n");
        vga_puts("Usage: kblayout <abnt2|us|us-intl>\n");
        return;
    }

    if (strcmp(argv[1], "abnt2") == 0) {
        keyboard_set_layout(KB_LAYOUT_ABNT2);
    } else if (strcmp(argv[1], "us") == 0) {
        keyboard_set_layout(KB_LAYOUT_US);
    } else if (strcmp(argv[1], "us-intl") == 0) {
        keyboard_set_layout(KB_LAYOUT_US_INTL);
    } else {
        vga_puts("Unknown layout: ");
        vga_puts(argv[1]);
        vga_puts("\n");
        return;
    }
    vga_puts("Keyboard layout set to: ");
    vga_puts(keyboard_layout_name(keyboard_get_layout()));
    vga_puts("\n");
}

void cmd_msense(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Mouse sensitivity: ");
        char b[16];
        itoa(mouse_get_sensitivity(), b, 10);
        vga_puts(b);
        vga_puts("\n");
        vga_puts("Mouse acceleration: ");
        vga_puts(mouse_get_acceleration() ? "enabled" : "disabled");
        vga_puts("\n");
        vga_puts("Usage: msense <1-10> [on|off]\n");
        return;
    }

    int sens = 0;
    for (int i = 0; argv[1][i] >= '0' && argv[1][i] <= '9'; i++) {
        sens = sens * 10 + (argv[1][i] - '0');
    }
    if (sens >= 1 && sens <= 10) {
        mouse_set_sensitivity((uint8_t)sens);
        vga_puts("Mouse sensitivity set to: ");
        char b[16];
        itoa(sens, b, 10);
        vga_puts(b);
        vga_puts("\n");
    } else {
        vga_puts("Invalid sensitivity (1-10)\n");
        return;
    }

    if (argc >= 3) {
        if (strcmp(argv[2], "on") == 0) {
            mouse_set_acceleration(true);
            vga_puts("Mouse acceleration: enabled\n");
        } else if (strcmp(argv[2], "off") == 0) {
            mouse_set_acceleration(false);
            vga_puts("Mouse acceleration: disabled\n");
        }
    }
}

