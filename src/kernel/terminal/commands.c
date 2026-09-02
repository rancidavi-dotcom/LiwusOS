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

void cmd_ip(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("Network stack not available in this build.\n");
}

void cmd_ping(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("ping: Network stack not available in this build.\n");
}

void cmd_wget(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("wget: Network stack not available in this build.\n");
}

void cmd_host(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_puts("host: Network stack not available in this build.\n");
}

