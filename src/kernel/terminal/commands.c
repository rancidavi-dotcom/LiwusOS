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
#include "timer.h"
#include "acpi.h"
#include "keyboard.h"
#include "mouse.h"

extern char *itoa(int value, char *str, int base);

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
    if (strncmp(vfs_path, mount, mount_len) == 0 &&
        (vfs_path[mount_len] == '/' || vfs_path[mount_len] == '\0')) {
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
}

void cmd_clear(int argc, char **argv) {
    (void)argc; (void)argv;
    vga_clear(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void cmd_echo(int argc, char **argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], ">") != 0 && strcmp(argv[i], ">>") != 0) continue;
        if (i + 2 != argc) {
            vga_puts("echo: expected one output file\n");
            return;
        }
        char path[256], disk_path[256];
        join_path(current_task ? current_task->process->cwd : "/", argv[i + 1], path, sizeof(path));
        vfs_to_sdfs_path(path, disk_path, sizeof(disk_path));
        uint32_t old_size = 0;
        uint8_t *old = NULL;
        if (argv[i][1] == '>') old = sdfs_read_file(disk_path, &old_size);
        size_t size = old_size + 1;
        for (int j = 1; j < i; j++) size += strlen(argv[j]) + (j > 1);
        uint8_t *buf = kmalloc(size);
        if (!buf) {
            if (old) kfree(old);
            vga_puts("echo: Out of memory\n");
            return;
        }
        size_t pos = old_size;
        if (old) { memcpy(buf, old, old_size); kfree(old); }
        for (int j = 1; j < i; j++) {
            if (j > 1) buf[pos++] = ' ';
            size_t len = strlen(argv[j]);
            memcpy(buf + pos, argv[j], len);
            pos += len;
        }
        buf[pos++] = '\n';
        if (sdfs_write_file(disk_path, buf, pos) != pos)
            vga_puts("echo: Failed to write file\n");
        kfree(buf);
        return;
    }
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
        join_path(current_task->process->cwd, argv[file_arg], path, sizeof(path));
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
        vga_puts(current_task->process->cwd);
        vga_puts("\n");
        serial_print(current_task->process->cwd);
        serial_print("\n");
    }
}

void cmd_ls(int argc, char **argv) {
    const char *path = current_task ? current_task->process->cwd : "/";
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
        vfs_node_put(dir);
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
    vfs_node_put(dir);
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
    resolve_path(current_task->process->cwd, path, resolved, sizeof(resolved));

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
        vfs_node_put(dir);
        return;
    }
    
    strncpy(current_task->process->cwd, resolved, sizeof(current_task->process->cwd) - 1);
    current_task->process->cwd[sizeof(current_task->process->cwd) - 1] = '\0';
    vfs_node_put(dir);
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
        join_path(current_task->process->cwd, argv[1], path, sizeof(path));
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
        vfs_node_put(file);
        return;
    }
    
    uint32_t size = file->length;
    if (size == 0) {
        vfs_node_put(file);
        return;
    }
    
    void *buf = kmalloc(size + 1);
    if (!buf) {
        vga_puts("cat: Out of memory\n");
        vfs_node_put(file);
        return;
    }
    
    uint32_t bytes_read = read_fs(file, 0, size, buf);
    if (bytes_read > 0) {
        ((char *)buf)[bytes_read] = '\0';
        vga_puts((char *)buf);
        serial_print((char *)buf);
    }
    kfree(buf);
    vfs_node_put(file);
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
        join_path(current_task->process->cwd, argv[1], path, sizeof(path));
    } else {
        strncpy(path, argv[1], sizeof(path) - 1);
    }
    path[sizeof(path) - 1] = '\0';
    
    char sdfs_path[256];
    vfs_to_sdfs_path(path, sdfs_path, sizeof(sdfs_path));
    
    int is_dir = 0;
    uint32_t size = 0;
    if (sdfs_path_info(sdfs_path, &is_dir, &size) == 0) {
        const char *message = is_dir ? ": Directory already exists\n" : ": File already exists\n";
        vga_puts("mkdir: ");
        vga_puts(argv[1]);
        vga_puts(message);
        serial_print("mkdir: ");
        serial_print(argv[1]);
        serial_print(message);
        return;
    }

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
        join_path(current_task->process->cwd, argv[1], path, sizeof(path));
    } else {
        strncpy(path, argv[1], sizeof(path) - 1);
    }
    path[sizeof(path) - 1] = '\0';
    
    fs_node_t *file = vfs_open(path);
    if (file) {
        vfs_node_put(file);
        return;
    }
    
    fs_node_t *new_file = vfs_create(path, 0644);
    if (!new_file) {
        vga_puts("touch: Failed to create file\n");
    }
    vfs_node_put(new_file);
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
        join_path(current_task->process->cwd, argv[1], path, sizeof(path));
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
    else if (current_task) join_path(current_task->process->cwd, argv[1], src, sizeof(src));
    else strncpy(src, argv[1], sizeof(src) - 1);
    src[sizeof(src) - 1] = '\0';
    
    if (argv[2][0] == '/') strncpy(dst, argv[2], sizeof(dst) - 1);
    else if (current_task) join_path(current_task->process->cwd, argv[2], dst, sizeof(dst));
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
    else if (current_task) join_path(current_task->process->cwd, argv[1], src, sizeof(src));
    else strncpy(src, argv[1], sizeof(src) - 1);
    src[sizeof(src) - 1] = '\0';
    
    if (argv[2][0] == '/') strncpy(dst, argv[2], sizeof(dst) - 1);
    else if (current_task) join_path(current_task->process->cwd, argv[2], dst, sizeof(dst));
    else strncpy(dst, argv[2], sizeof(dst) - 1);
    dst[sizeof(dst) - 1] = '\0';
    
    fs_node_t *src_file = vfs_open(src);
    if (!src_file) {
        vga_puts("cp: Source not found\n");
        return;
    }
    
    if (src_file->flags & FS_DIRECTORY) {
        vga_puts("cp: Directories not supported\n");
        vfs_node_put(src_file);
        return;
    }
    
    uint32_t size = src_file->length;
    void *buf = kmalloc(size);
    if (!buf) {
        vga_puts("cp: Out of memory\n");
        vfs_node_put(src_file);
        return;
    }
    
    if (read_fs(src_file, 0, size, buf) > 0) {
        fs_node_t *dst_file = vfs_create(dst, 0644);
        if (dst_file) {
            write_fs(dst_file, 0, size, buf);
            vfs_node_put(dst_file);
        } else {
            vga_puts("cp: Failed to create destination\n");
        }
    }
    kfree(buf);
    vfs_node_put(src_file);
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

