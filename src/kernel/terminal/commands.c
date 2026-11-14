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
#include "net_manager.h"
#include "rtc.h"
#include "hda.h"
#include "pci.h"
#include "http_client.h"
#include "screenshot.h"

extern char *itoa(int value, char *str, int base);
extern int atoi(const char *nptr);

static void join_path(const char *base, const char *rel, char *out, size_t out_size) {
    if (rel[0] == '/' || strchr(rel, ':')) {
        strncpy(out, rel, out_size - 1);
        out[out_size - 1] = '\0';
        return;
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
    vga_puts("\n=== LiwusOS 2.0 — Comandos do Sistema ===\n\n");
    size_t count = terminal_get_command_count();
    const terminal_command_t *cmds = terminal_get_commands();
    for (size_t i = 0; i < count; i++) {
        vga_puts("  ");
        vga_puts(cmds[i].name);
        int pad = 12 - (int)strlen(cmds[i].name);
        if (pad < 2) pad = 2;
        for (int s = 0; s < pad; s++) vga_puts(" ");
        vga_puts("- ");
        vga_puts(cmds[i].desc);
        vga_puts("\n");
    }
    vga_puts("\n=== Programas Executáveis (/bin) ===\n");
    vga_puts("  doom        - Jogo DOOM Shareware em tela cheia\n");
    vga_puts("  kilo        - Editor de texto em tela cheia (Kilo)\n");
    vga_puts("  cat         - Exibir conteúdo de arquivos\n");
    vga_puts("  head        - Exibir primeiras linhas de arquivo\n");
    vga_puts("  stat        - Informações detalhadas de arquivo/pasta\n");
    vga_puts("  ls          - Listar arquivos e pastas\n");
    vga_puts("  mkdir       - Criar diretório\n");
    vga_puts("  touch       - Criar arquivo vazio\n");
    vga_puts("  rm          - Remover arquivo\n");
    vga_puts("  mv          - Mover ou renomear arquivo\n");
    vga_puts("  cp          - Copiar arquivo\n");
    vga_puts("  echo        - Imprimir texto na saída padrão\n");
    vga_puts("  c_example   - Aplicação Ring 3 (User-Space) em C\n");
    vga_puts("  native-hello- Demonstração nativa LXE de Ring 3\n\n");
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
        uint32_t write_result = sdfs_write_file(disk_path, buf, pos);
        if (write_result != pos)
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

static const char *get_terminal_badge(fs_node_t *node, const char *full_path) {
    if (!node) return "[DATA ] ";
    if (node->flags & FS_DIRECTORY) return "[DIR  ] ";
    liwus_resource_type_t t = vfs_detect_resource_type(node, full_path);
    switch (t) {
        case LIWUS_RES_TEXT: return "[TEXT ] ";
        case LIWUS_RES_EXEC: return "[EXEC ] ";
        case LIWUS_RES_IMAGE: return "[IMAGE] ";
        case LIWUS_RES_AUDIO: return "[AUDIO] ";
        case LIWUS_RES_DEVICE: return "[DEV  ] ";
        case LIWUS_RES_CHANNEL: return "[IPC  ] ";
        case LIWUS_RES_DIR: return "[DIR  ] ";
        default: return "[DATA ] ";
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
        const char *badge = get_terminal_badge(dir, path);
        vga_puts(badge);
        vga_puts(path);
        vga_puts("\n");
        vfs_node_put(dir);
        return;
    }
    
    struct dirent *node = 0;
    uint32_t i = 0;
    char entry_path[256];
    size_t plen = strlen(path);

    while ((node = readdir_fs(dir, i)) != 0) {
        if (plen + 1 + strlen(node->name) < sizeof(entry_path)) {
            strcpy(entry_path, path);
            if (plen > 0 && entry_path[plen - 1] != '/' && entry_path[plen - 1] != ':') {
                entry_path[plen] = '/';
                strcpy(entry_path + plen + 1, node->name);
            } else {
                strcpy(entry_path + plen, node->name);
            }
            fs_node_t *child = vfs_open(entry_path);
            const char *badge = child ? get_terminal_badge(child, entry_path) : "[FILE ] ";
            vga_puts(badge);
            if (child) vfs_node_put(child);
        }
        vga_puts(node->name);
        vga_puts("\n");
        i++;
    }
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
    join_path(current_task ? current_task->process->cwd : "/", argv[1], path, sizeof(path));
    
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
    
    char chunk[256];
    uint32_t offset = 0;
    for (;;) {
        uint32_t bytes_read = read_fs(file, offset, sizeof(chunk) - 1, (uint8_t *)chunk);
        if (bytes_read == 0) break;
        chunk[bytes_read] = '\0';
        vga_puts(chunk);
        serial_print(chunk);
        offset += bytes_read;
        if (file->length > 0 && offset >= file->length) break;
    }
    vfs_node_put(file);
}

void cmd_stat(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: stat <path>\n");
        return;
    }
    const char *path = argv[1];
    char full_path[256];
    join_path(current_task ? current_task->process->cwd : "/", path, full_path, sizeof(full_path));

    fs_node_t *node = vfs_open(full_path);
    if (!node) {
        vga_puts("stat: cannot inspect path: ");
        vga_puts(full_path);
        vga_puts("\n");
        return;
    }

    liwus_resource_type_t res_type = vfs_detect_resource_type(node, full_path);
    const char *res_desc = "[DATA ] Binary Data";
    if (node->flags & FS_DIRECTORY) {
        res_desc = "[DIR  ] Directory";
    } else {
        switch (res_type) {
            case LIWUS_RES_TEXT: res_desc = "[TEXT ] Text Document"; break;
            case LIWUS_RES_EXEC: res_desc = "[EXEC ] Executable Program"; break;
            case LIWUS_RES_IMAGE: res_desc = "[IMAGE] Bitmap Graphic"; break;
            case LIWUS_RES_AUDIO: res_desc = "[AUDIO] Audio Stream"; break;
            case LIWUS_RES_DEVICE: res_desc = "[DEV  ] Hardware Device"; break;
            case LIWUS_RES_CHANNEL: res_desc = "[IPC  ] Inter-Process Channel"; break;
            case LIWUS_RES_DIR: res_desc = "[DIR  ] Directory"; break;
            default: res_desc = "[DATA ] Binary Data"; break;
        }
    }

    vga_puts("Path: ");
    vga_puts(full_path);
    vga_puts("\nResource: ");
    vga_puts(res_desc);
    vga_puts("\nSize: ");
    char buf[32];
    itoa((int)node->length, buf, 10);
    vga_puts(buf);
    vga_puts(" bytes\nMode: ");
    itoa((int)(node->mask & 0777u), buf, 8);
    vga_puts(buf);
    vga_puts("\n");

    vfs_node_put(node);
}

void cmd_head(int argc, char **argv) {
    if (argc < 2) {
        vga_puts("Usage: head <file> [lines]\n");
        return;
    }
    int max_lines = 10;
    if (argc >= 3) {
        max_lines = 0;
        const char *p = argv[2];
        while (*p >= '0' && *p <= '9') {
            max_lines = max_lines * 10 + (*p - '0');
            p++;
        }
        if (max_lines <= 0) max_lines = 10;
    }
    char path[256];
    join_path(current_task ? current_task->process->cwd : "/", argv[1], path, sizeof(path));

    fs_node_t *file = vfs_open(path);
    if (!file) {
        vga_puts("head: ");
        vga_puts(argv[1]);
        vga_puts(": No such file or directory\n");
        return;
    }
    if (file->flags & FS_DIRECTORY) {
        vga_puts("head: ");
        vga_puts(argv[1]);
        vga_puts(": Is a directory\n");
        vfs_node_put(file);
        return;
    }

    uint8_t buf[256];
    uint32_t offset = 0;
    int lines = 0;
    while (lines < max_lines) {
        uint32_t bytes = read_fs(file, offset, sizeof(buf) - 1, buf);
        if (bytes == 0) break;
        for (uint32_t i = 0; i < bytes; i++) {
            char ch[2] = { (char)buf[i], '\0' };
            vga_puts(ch);
            if (buf[i] == '\n') {
                lines++;
                if (lines >= max_lines) break;
            }
        }
        offset += bytes;
    }
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
    vga_puts("LiwusOS Kernel v2.0 - [LibC & c_demo ATIVO]\n");
    serial_print("LiwusOS Kernel v2.0 - [LibC & c_demo ATIVO]\n");
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
extern int atoi(const char *nptr);
    
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
extern int atoi(const char *nptr);

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
extern int atoi(const char *nptr);
    
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
    const char *config_value = keyboard_get_layout() == KB_LAYOUT_ABNT2
        ? "ABNT2\n"
        : keyboard_get_layout() == KB_LAYOUT_US_INTL
            ? "US_INTL\n" : "US\n";
    sdfs_create_dir("/etc");
    uint32_t config_len = (uint32_t)strlen(config_value);
    if (sdfs_write_file("/etc/keyboard.conf", (uint8_t *)config_value,
                        config_len) != config_len) {
        vga_puts("Keyboard layout set, but could not save it to /etc/keyboard.conf.\n");
        return;
    }
    sdfs_flush_bitmap();
    vga_puts("Keyboard layout set to: ");
    vga_puts(keyboard_layout_name(keyboard_get_layout()));
    vga_puts(" (saved for next boot)\n");
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

void cmd_ifconfig(int argc, char **argv) {
    if (!net_is_active()) {
        vga_puts("eth0: Interface de rede nao inicializada.\n");
        return;
    }

    if (argc >= 2) {
        if (strcmp(argv[1], "dhcp") == 0) {
            vga_puts("Reiniciando negociacao DHCP em eth0...\n");
            net_dhcp_renew();
            for (int i = 0; i < 150 && !net_has_ip(); i++) {
                net_poll();
                task_sleep_ms(20);
            }
            if (net_has_ip()) {
                char ip[16], mask[16], gw[16];
                net_get_ip_strings(ip, mask, gw);
                vga_puts("DHCP sucesso! IP: ");
                vga_puts(ip);
                vga_puts(" Mask: ");
                vga_puts(mask);
                vga_puts(" GW: ");
                vga_puts(gw);
                vga_puts("\n");
            } else {
                vga_puts("DHCP timeout. Dica: use 'ifconfig <ip> [mask] [gw]' para configurar IP estatico.\n");
            }
            return;
        }

        /* Configura IP estatico: ifconfig <ip> [mask] [gw] */
        const char *new_ip = argv[1];
        const char *new_mask = (argc >= 3) ? argv[2] : "255.255.255.0";
        const char *new_gw = (argc >= 4) ? argv[3] : "0.0.0.0";

        int res = net_set_static_ip(new_ip, new_mask, new_gw);
        if (res == 0) {
            vga_puts("IP configurado com sucesso!\n      IP: ");
            vga_puts(new_ip);
            vga_puts(" Mask: ");
            vga_puts(new_mask);
            vga_puts(" GW: ");
            vga_puts(new_gw);
            vga_puts("\n");
        } else {
            vga_puts("Erro: Formato de IP invalido (ex: ifconfig 192.168.1.150 255.255.255.0 192.168.1.1)\n");
        }
        return;
    }

    /* Exibe status da interface */
    char ip[16], mask[16], gw[16];
    net_get_ip_strings(ip, mask, gw);
    uint32_t rx = 0, tx = 0;
    net_get_packet_counts(&rx, &tx);
    const uint8_t *mac = net_get_mac();
    char buf[32];
    vga_puts("eth0: Link ");
    vga_puts(net_get_link_status() ? "UP (" : "DOWN (");
    vga_puts(net_get_driver_name());
    vga_puts(")\n      mac: ");
    if (mac) {
        static const char hex[] = "0123456789abcdef";
        for (int i = 0; i < 6; i++) {
            char m[3] = { hex[(mac[i] >> 4) & 0xF], hex[mac[i] & 0xF], '\0' };
            vga_puts(m);
            if (i < 5) vga_puts(":");
        }
    }
    vga_puts("\n      inet: ");
    vga_puts(ip);
    vga_puts("  netmask: ");
    vga_puts(mask);
    vga_puts("  gateway: ");
    vga_puts(gw);
    vga_puts("\n      RX packets: ");
    itoa((int)rx, buf, 10);
    vga_puts(buf);
    vga_puts("  TX packets: ");
    itoa((int)tx, buf, 10);
    vga_puts(buf);
    vga_puts("\n");
}

void cmd_ping(int argc, char **argv) {
    if (argc < 2 || !argv[1]) {
        vga_puts("Usage: ping <ip_address>\n");
        return;
    }
    const char *target = argv[1];
    vga_puts("PING ");
    vga_puts(target);
    vga_puts(" 56(84) bytes of data.\n");
    int res = net_ping(target, 4);
    if (res == 0) {
        vga_puts("Ping finished successfully (4 packets transmitted, replies received).\n");
    } else {
        vga_puts("Ping failed: host unreachable or timeout.\n");
    }
}

void cmd_date(int argc, char **argv) {
    (void)argc; (void)argv;
    rtc_time_t t = rtc_read_time();
    char h[8], m[8], s[8];
    itoa((int)t.hour, h, 10);
    itoa((int)t.minute, m, 10);
    itoa((int)t.second, s, 10);
    vga_puts("Current Time: ");
    if (t.hour < 10) vga_puts("0");
    vga_puts(h);
    vga_puts(":");
    if (t.minute < 10) vga_puts("0");
    vga_puts(m);
    vga_puts(":");
    if (t.second < 10) vga_puts("0");
    vga_puts(s);
    vga_puts(" (RTC/NTP)\n");
}

void cmd_uptime(int argc, char **argv) {
    (void)argc; (void)argv;
    uint32_t secs = timer_ticks / 100;
    char buf[16];
    itoa((int)secs, buf, 10);
    vga_puts("System Uptime: ");
    vga_puts(buf);
    vga_puts(" seconds\n");
}

void cmd_beep(int argc, char **argv) {
    uint32_t freq = 440;
    uint32_t ms = 300;
    if (argc >= 2) {
        freq = (uint32_t)atoi(argv[1]);
        if (freq == 0) freq = 440;
    }
    if (argc >= 3) {
        ms = (uint32_t)atoi(argv[2]);
        if (ms == 0) ms = 300;
    }
    vga_puts(hda_is_available() ? "Tocando pela saida HDA: "
                                : "HDA indisponivel; tocando apenas no buzzer da placa: ");
    char b1[16], b2[16];
    itoa((int)freq, b1, 10);
    itoa((int)ms, b2, 10);
    vga_puts(b1); vga_puts(" Hz por "); vga_puts(b2); vga_puts(" ms...\n");
    hda_beep(freq, ms);
}

void cmd_sound(int argc, char **argv) {
    if (!hda_is_available()) {
        vga_puts("Status: Controlador HD Audio ausente ou aguardando detecção.\n");
    } else {
        vga_puts("Driver: Intel High Definition Audio 1.0 (Ativo)\n");
        pci_device_t *pci = hda_get_pci_device();
        if (pci) {
            vga_puts("Dispositivo PCI: ");
            char hb[16];
            itoa((int)pci->bus, hb, 16); vga_puts(hb); vga_puts(":");
            itoa((int)pci->device, hb, 16); vga_puts(hb); vga_puts(".");
            itoa((int)pci->function, hb, 10); vga_puts(hb);
            vga_puts(" (VID:DID = ");
            itoa((int)pci->vendor_id, hb, 16); vga_puts(hb); vga_puts(":");
            itoa((int)pci->device_id, hb, 16); vga_puts(hb);
            vga_puts(")\n");
        }
        vga_puts("Codec: ");
        vga_puts(hda_get_codec_name());
        vga_puts(" (VID=0x");
        char cb[16];
        itoa((int)hda_get_codec_vid(), cb, 16); vga_puts(cb);
        vga_puts(" DID=0x");
        itoa((int)hda_get_codec_did(), cb, 16); vga_puts(cb);
        vga_puts(" STATESTS=0x");
        itoa((int)hda_get_statests(), cb, 16); vga_puts(cb);
        vga_puts(")\n");
        vga_puts("Volume atual: ");
        char vb[16];
        itoa((int)hda_get_volume(), vb, 10);
        vga_puts(vb);
        vga_puts("%\n");
    }

    if (argc >= 2) {
        if (strcmp(argv[1], "test") == 0) {
            vga_puts(hda_is_available()
                         ? "Teste na saida HDA (A4 -> C5 -> E5)...\n"
                         : "Sem rota HDA; o teste so usa o buzzer interno.\n");
            hda_beep(440, 150);
            hda_beep(523, 150);
            hda_beep(659, 250);
        } else if (strcmp(argv[1], "chime") == 0) {
            vga_puts("Tocando acorde de inicialização LiwusOS...\n");
            hda_startup_sound();
        } else if (strcmp(argv[1], "vol") == 0 && argc >= 3) {
            int vol = atoi(argv[2]);
            hda_set_volume((uint8_t)vol);
            vga_puts("Volume ajustado para ");
            vga_puts(argv[2]);
            vga_puts("%\n");
        } else if (strcmp(argv[1], "diag") == 0) {
            vga_puts("--- Diagnóstico de Áudio LiwusOS ---\n");
            char buf[16];
            vga_puts("HDA inicializado: ");
            vga_puts(hda_is_available() ? "SIM\n" : "NAO\n");
            vga_puts("Stream SD0CTL: 0x");
            itoa((int)hda_get_stream_status(), buf, 16);
            vga_puts(buf);
            vga_puts(" LPIB=0x");
            itoa((int)hda_get_stream_position(), buf, 16);
            vga_puts(buf);
            vga_puts(" rotas analogicas=");
            itoa((int)hda_get_output_routes(), buf, 10);
            vga_puts(buf);
            vga_puts("\nControladores HDA no sistema: ");
            itoa(pci_get_hda_count(), buf, 10);
            vga_puts(buf);
            vga_puts("\n");
        } else {
            vga_puts("Uso: sound [test | chime | vol <0-100> | diag]\n");
        }
    } else {
        vga_puts("Uso: sound [test | chime | vol <0-100> | diag]\n");
    }
}

void cmd_screenshot(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        vga_puts("Uso: screenshot <ip-do-PC> [porta]\n");
        vga_puts("No PC: python3 tools/liwus_screenshot_receiver.py\n");
        return;
    }
    if (!net_is_active() || !net_has_ip()) {
        vga_puts("screenshot: rede sem IP; conecte a rede e tente novamente.\n");
        return;
    }

    uint32_t port = 49152;
    if (argc == 3) {
        if (!argv[2][0]) {
            vga_puts("screenshot: porta invalida.\n");
            return;
        }
        port = 0;
        for (const char *p = argv[2]; *p; p++) {
            if (*p < '0' || *p > '9' || port > 6553) {
                vga_puts("screenshot: porta deve estar entre 1 e 65535.\n");
                return;
            }
            port = port * 10 + (uint32_t)(*p - '0');
        }
        if (port == 0 || port > 65535) {
            vga_puts("screenshot: porta deve estar entre 1 e 65535.\n");
            return;
        }
    }

    uint8_t *bmp = NULL;
    uint32_t size = 0;
    int rc = vga_capture_bmp(&bmp, &size);
    if (rc != 0) {
        vga_puts("screenshot: framebuffer RGB indisponivel ou sem memoria.\n");
        return;
    }

    vga_puts("Enviando captura para ");
    vga_puts(argv[1]);
    vga_puts("...");
    rc = net_send_screenshot_bmp(argv[1], (uint16_t)port, bmp, size);
    kfree(bmp);
    if (rc == 0) {
        vga_puts(" salva no PC receptor.\n");
    } else {
        vga_puts(" falhou; confira o receptor, o IP e a porta.\n");
    }
}

void cmd_lspci(int argc, char **argv) {
    (void)argc; (void)argv;
    int count = pci_get_device_count();
    vga_puts("Dispositivos PCI (Total: ");
    char cb[16];
    itoa(count, cb, 10);
    vga_puts(cb);
    vga_puts("):\n");

    for (int i = 0; i < count; i++) {
        pci_device_t *d = pci_get_device_by_index(i);
        if (!d) continue;

        char hex_buf[16];
        vga_puts("  ");
        if (d->bus < 0x10) vga_puts("0");
        itoa((int)d->bus, hex_buf, 16);
        vga_puts(hex_buf);
        vga_puts(":");
        if (d->device < 0x10) vga_puts("0");
        itoa((int)d->device, hex_buf, 16);
        vga_puts(hex_buf);
        vga_puts(".");
        itoa((int)d->function, hex_buf, 10);
        vga_puts(hex_buf);
        vga_puts(" [");

        itoa((int)d->vendor_id, hex_buf, 16);
        vga_puts(hex_buf);
        vga_puts(":");
        itoa((int)d->device_id, hex_buf, 16);
        vga_puts(hex_buf);
        vga_puts("] ");

        if (d->class_id == 0x01 && d->subclass_id == 0x06) {
            vga_puts("SATA Controller (AHCI)\n");
        } else if (d->class_id == 0x01 && d->subclass_id == 0x08) {
            vga_puts("NVMe Controller\n");
        } else if (d->class_id == 0x02 && d->subclass_id == 0x00) {
            vga_puts("Ethernet Network Controller\n");
        } else if (d->class_id == 0x03 && d->subclass_id == 0x00) {
            vga_puts("VGA Compatible Video Controller\n");
        } else if (d->class_id == 0x04 && d->subclass_id == 0x03) {
            vga_puts("Audio Device (High Definition Audio)\n");
        } else if (d->class_id == 0x04 && d->subclass_id == 0x01) {
            vga_puts("Multimedia Audio Controller\n");
        } else if (d->class_id == 0x0C && d->subclass_id == 0x03) {
            vga_puts("USB Controller\n");
        } else if (d->class_id == 0x06 && d->subclass_id == 0x00) {
            vga_puts("Host Bridge\n");
        } else if (d->class_id == 0x06 && d->subclass_id == 0x01) {
            vga_puts("ISA Bridge\n");
        } else if (d->class_id == 0x06 && d->subclass_id == 0x04) {
            vga_puts("PCI-to-PCI Bridge\n");
        } else {
            vga_puts("Classe ");
            itoa((int)d->class_id, hex_buf, 16);
            vga_puts(hex_buf);
            vga_puts(" Subclasse ");
            itoa((int)d->subclass_id, hex_buf, 16);
            vga_puts(hex_buf);
            vga_puts("\n");
        }
    }
}

void cmd_wget(int argc, char **argv) {
    if (argc < 2 || !argv[1]) {
        vga_puts("Uso: wget <url> [caminho_destino]\n");
        vga_puts("Exemplos:\n");
        vga_puts("  wget http://192.168.0.21:8000/arquivo.txt\n");
        vga_puts("  wget https://example.com/index.html /index.html\n");
        return;
    }
    const char *url = argv[1];
    const char *dest = (argc >= 3) ? argv[2] : NULL;
    int res = http_download_file(url, dest, NULL, NULL);
    if (res < 0) {
        vga_puts("wget: Falha ao baixar arquivo.\n");
    }
}

void cmd_curl(int argc, char **argv) {
    if (argc < 2 || !argv[1]) {
        vga_puts("Uso: curl <url>\n");
        vga_puts("Exemplo:\n");
        vga_puts("  curl https://example.com/\n");
        return;
    }
    const char *url = argv[1];
    uint8_t *body = NULL;
    size_t size = 0;
    int status = 0;
    int res = http_get_mem(url, &body, &size, &status, NULL, NULL);
    if (res < 0 || !body) {
        vga_puts("curl: Falha ao obter conteudo da URL.\n");
        return;
    }
    /* Exibe conteudo no terminal em blocos/linhas */
    size_t print_len = (size > 2048) ? 2048 : size;
    char line_buf[128];
    size_t li = 0;
    for (size_t i = 0; i < print_len; i++) {
        char ch = (char)body[i];
        if (ch == '\r') continue;
        line_buf[li++] = ch;
        if (ch == '\n' || li >= sizeof(line_buf) - 1) {
            line_buf[li] = '\0';
            vga_puts(line_buf);
            li = 0;
        }
    }
    if (li > 0) {
        line_buf[li] = '\0';
        vga_puts(line_buf);
    }
    if (size > print_len) {
        vga_puts("\n[... truncado pelo terminal ...]\n");
    }
    vga_puts("\n");
    kfree(body);
}

