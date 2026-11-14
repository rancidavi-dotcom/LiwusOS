#include "lpkg.h"
#include "terminal.h"
#include "vga.h"
#include "serial.h"
#include "string.h"
#include "kheap.h"
#include "fs/sdfs.h"
#include "initrd.h"
#include "http_client.h"
#include "fs/crc32.h"
#include "initrd.h"

extern char *itoa(int value, char *str, int base);
extern volatile uint32_t timer_ticks;
extern int launch_initrd_program_argv(const char *filename, char *const argv[]);
extern int sys_waitpid(int pid, int *status, int options);

#define LPKG_MAX_CATALOG 16
#define LPKG_REPO_FILE   "/etc/lpkg.repo"
#define LPKG_DB_FILE     "/etc/lpkg.db"
#define LPKG_OS_RELEASE  "/etc/os-release"
#define LPKG_BUILD_VERSION "2.2.14"
#define LPKG_DEFAULT_REPO "https://raw.githubusercontent.com/rancidavi-dotcom/LiwusOS/pkg-repo"
#define LPKG_SYSTEM_MANIFEST "system-v2.txt"

static bool g_repo_synced = false;
static int g_sysupdate_last_bucket = -1;
static size_t g_sysupdate_last_bytes = 0;
static char g_sysupdate_last_phase[48];
static void copy_str_safe(char *dst, const char *src, size_t max_len);

static void lpkg_terminal_update_progress(const lpkg_progress_info_t *p,
                                         void *user_data) {
    (void)user_data;
    if (!p) return;
    int phase_percent = p->total
        ? (int)((p->completed * 100U) / p->total) : p->overall_percent;
    if (phase_percent > 100) phase_percent = 100;
    int bucket = phase_percent / 5;
    bool phase_changed = strcmp(g_sysupdate_last_phase, p->phase ? p->phase : "") != 0;
    if (!phase_changed && p->total && bucket == g_sysupdate_last_bucket && p->overall_percent != 100) return;
    if (!phase_changed && !p->total && p->completed >= g_sysupdate_last_bytes &&
        p->completed - g_sysupdate_last_bytes < 16384U && p->completed != 0 &&
        p->overall_percent != 100) return;

    if (p->phase) {
        copy_str_safe(g_sysupdate_last_phase, p->phase, sizeof(g_sysupdate_last_phase));
    }
    g_sysupdate_last_bucket = bucket;
    g_sysupdate_last_bytes = p->completed;
    char num[16];
    vga_puts("[update ");
    itoa(p->overall_percent, num, 10); vga_puts(num); vga_puts("%] ");
    if (p->phase) { vga_puts(p->phase); vga_puts(": "); }
    if (p->total) {
        itoa((int)p->completed, num, 10); vga_puts(num); vga_puts("/");
        itoa((int)p->total, num, 10); vga_puts(num); vga_puts(" bytes");
        vga_puts(" ("); itoa(phase_percent, num, 10); vga_puts(num); vga_puts("%)");
    } else if (p->completed) {
        itoa((int)p->completed, num, 10); vga_puts(num);
        vga_puts(" bytes recebidos; total indisponivel");
    } else if (p->message) {
        vga_puts(p->message);
    }
    vga_puts("\n");
}

static lpkg_pkg_info_t g_catalog[LPKG_MAX_CATALOG] = {
    {
        "neofetch", "1.2.0", "Sistema",
        "Logotipo ASCII e resumo de hardware, memoria e rede",
        LPKG_DEFAULT_REPO "/neofetch.lxe", "1.2.0", false, false
    },
    {
        "snake", "1.0.0", "Jogo",
        "Jogo Snake grafico em tela cheia (320x200 Breeze Dark)",
        LPKG_DEFAULT_REPO "/snake.lxe", "1.0.0", true, false
    },
    {
        "matrix", "1.1.0", "Grafico",
        "Protetor de tela Matrix Digital Rain (320x200)",
        LPKG_DEFAULT_REPO "/matrix.lxe", "1.1.0", true, false
    },
    {
        "sysbench", "1.0.0", "Benchmark",
        "Benchmark de CPU (primos/hash) e largura de banda RAM",
        LPKG_DEFAULT_REPO "/sysbench.lxe", "1.0.0", false, false
    },
    {
        "hello_web", "1.0.0", "Fonte C",
        "Codigo C baixado do GitHub e compilado via TinyCC (tcc)",
        LPKG_DEFAULT_REPO "/hello_web.c", "1.0.0", false, false
    },
    {
        "doom", "1.9.0", "Jogo",
        "DOOM Shareware (id Software, 1993) em tela cheia",
        "", "1.9.0", true, true
    },
    {
        "kilo", "0.0.1", "Editor",
        "Editor de texto de terminal com realce de sintaxe C",
        LPKG_DEFAULT_REPO "/kilo.lxe", "0.0.1", false, true
    },
    {
        "tcc", "0.9.27", "Dev",
        "Compilador C nativo TinyCC x86_64 para gerar .lxe",
        "", "0.9.27", false, true
    },
    {
        "c_demo", "2.0.0", "Sistema",
        "Suite de demonstracao e verificacao da LibC em Ring 3",
        LPKG_DEFAULT_REPO "/c_demo.lxe", "2.0.0", false, true
    }
};

static int g_catalog_count = 9;

static uint32_t lpkg_parse_u32(const char *s, int base) {
    uint32_t value = 0;
    if (!s || !*s) return 0;
    for (; *s; s++) {
        uint32_t digit;
        if (*s >= '0' && *s <= '9') digit = (uint32_t)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f') digit = (uint32_t)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F') digit = (uint32_t)(*s - 'A' + 10);
        else break;
        if (digit >= (uint32_t)base || value > (UINT32_MAX - digit) / (uint32_t)base) return 0;
        value = value * (uint32_t)base + digit;
    }
    return value;
}

static int lpkg_tar_octal(const char *field, size_t width, uint32_t *out) {
    uint32_t value = 0;
    size_t i = 0;
    while (i < width && (field[i] == ' ' || field[i] == '\0')) i++;
    int found = 0;
    for (; i < width && field[i] >= '0' && field[i] <= '7'; i++) {
        uint32_t digit = (uint32_t)(field[i] - '0');
        if (value > (UINT32_MAX - digit) / 8U) return -1;
        value = value * 8U + digit;
        found = 1;
    }
    *out = found ? value : 0;
    return 0;
}

static int lpkg_tar_path(const struct tar_header *header, char *out, size_t out_size) {
    size_t used = 0;
    if (header->prefix[0]) {
        while (used < sizeof(header->prefix) && header->prefix[used]) used++;
        if (used + 1 >= out_size) return -1;
        memcpy(out, header->prefix, used);
        out[used++] = '/';
    }
    size_t name_len = 0;
    while (name_len < sizeof(header->filename) && header->filename[name_len]) name_len++;
    if (!name_len || used + name_len >= out_size) return -1;
    memcpy(out + used, header->filename, name_len);
    used += name_len;
    out[used] = '\0';
    while (out[0] == '.' && out[1] == '/') {
        memmove(out, out + 2, strlen(out + 2) + 1);
    }
    if (!out[0] || out[0] == '/' || strstr(out, "../") || strcmp(out, "..") == 0) return -1;
    for (size_t i = 0; out[i]; i++) {
        if (out[i] == '/' && out[i + 1] == '/') return -1;
        if ((i == 0 || out[i - 1] == '/') && out[i] == '.' && out[i + 1] == '.' &&
            (out[i + 2] == '/' || out[i + 2] == '\0')) return -1;
    }
    return 0;
}

static int lpkg_tar_validate(const uint8_t *archive, size_t size) {
    size_t off = 0;
    unsigned entries = 0;
    while (off + 512 <= size) {
        const struct tar_header *h = (const struct tar_header *)(archive + off);
        if (!h->filename[0]) return entries > 0 ? 0 : -1;
        uint32_t file_size = 0;
        if (lpkg_tar_octal(h->size, sizeof(h->size), &file_size) < 0) return -1;
        size_t padded = ((size_t)file_size + 511U) & ~(size_t)511U;
        if (padded < file_size || off + 512U > size || padded > size - (off + 512U)) return -1;
        if (h->typeflag == '5') {
            off += 512U + padded;
            entries++;
            continue;
        }
        if (h->typeflag != '\0' && h->typeflag != '0') return -1;
        char path[256];
        if (lpkg_tar_path(h, path, sizeof(path)) < 0) return -1;
        off += 512U + padded;
        entries++;
    }
    return -1;
}

static int lpkg_tar_should_preserve(const char *path) {
    return strcmp(path, "boot/hd_boot.img") == 0 || strcmp(path, ".system_installed") == 0 ||
           strncmp(path, "etc/", 4) == 0 || strncmp(path, "home/", 5) == 0 ||
           strncmp(path, "var/", 4) == 0;
}

static void lpkg_report_progress(lpkg_progress_fn cb, void *user_data,
                                 int overall, size_t completed, size_t total,
                                 const char *phase, const char *message) {
    if (!cb) return;
    lpkg_progress_info_t p = {
        .overall_percent = overall,
        .completed = completed,
        .total = total,
        .phase = phase,
        .message = message
    };
    cb(&p, user_data);
}

typedef struct {
    lpkg_progress_fn callback;
    void *user_data;
    const char *phase;
    const char *message;
    uint32_t expected_size;
    int overall_start;
    int overall_span;
} lpkg_download_progress_t;

static void lpkg_download_progress(size_t received, size_t total, void *user_data) {
    lpkg_download_progress_t *ctx = (lpkg_download_progress_t *)user_data;
    if (!ctx || !ctx->callback) return;
    if (!total) total = ctx->expected_size;
    size_t bounded = received;
    if (total && bounded > total) bounded = total;
    int local = total ? (int)((bounded * 100U) / total) : 0;
    if (local > 100) local = 100;
    int overall = ctx->overall_start + (ctx->overall_span * local) / 100;
    lpkg_report_progress(ctx->callback, ctx->user_data, overall, received,
                         total, ctx->phase, ctx->message);
}

static int lpkg_install_system_archive(const uint8_t *archive, size_t size,
                                       lpkg_progress_fn progress_cb, void *user_data) {
    size_t off = 0;
    unsigned file_count = 0;
    for (size_t scan = 0; scan + 512 <= size;) {
        const struct tar_header *h = (const struct tar_header *)(archive + scan);
        if (!h->filename[0]) break;
        uint32_t len = 0;
        if (lpkg_tar_octal(h->size, sizeof(h->size), &len) < 0) return -1;
        if (h->typeflag == '5') {
            scan += 512U;
            continue;
        }
        char path[256];
        if (lpkg_tar_path(h, path, sizeof(path)) < 0) return -1;
        if (!lpkg_tar_should_preserve(path)) file_count++;
        scan += 512U + (((size_t)len + 511U) & ~(size_t)511U);
    }
    if (!file_count) return -1;

    unsigned done = 0;
    while (off + 512 <= size) {
        const struct tar_header *h = (const struct tar_header *)(archive + off);
        if (!h->filename[0]) break;
        uint32_t len = 0;
        if (lpkg_tar_octal(h->size, sizeof(h->size), &len) < 0) return -1;
        if (h->typeflag == '5') {
            off += 512U;
            continue;
        }
        char path[256];
        if (lpkg_tar_path(h, path, sizeof(path)) < 0) return -1;
        const uint8_t *data = archive + off + 512;
        if (!lpkg_tar_should_preserve(path)) {
            uint32_t mode = 0644;
            if (lpkg_tar_octal(h->mode, sizeof(h->mode), &mode) < 0) return -1;
            char target[258];
            target[0] = '/';
            strncpy(target + 1, path, sizeof(target) - 2);
            target[sizeof(target) - 1] = '\0';
            for (char *p = target + 1; *p; p++) {
                if (*p == '/') {
                    char saved = *p;
                    *p = '\0';
                    sdfs_create_dir(target);
                    *p = saved;
                }
            }
            sdfs_create_file(target);
            if (len && sdfs_write_file(target, (uint8_t *)data, len) != len) return -1;
            sdfs_set_permissions(target, (uint16_t)(mode & 0777U));
            done++;
            if (progress_cb) {
                char msg[112];
                strcpy(msg, "Instalando arquivo do sistema: ");
                strcat(msg, path);
                int overall = 80 + (int)((done * 14U) / file_count);
                lpkg_report_progress(progress_cb, user_data, overall, done,
                                     file_count, "Instalando arquivos", msg);
            }
        }
        off += 512U + (((size_t)len + 511U) & ~(size_t)511U);
    }
    sdfs_flush_bitmap();
    return 0;
}

bool lpkg_is_repo_synced(void) {
    return g_repo_synced;
}

static void copy_str_safe(char *dst, const char *src, size_t max_len) {
    if (!dst || max_len == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, max_len - 1);
    dst[max_len - 1] = '\0';
}

static bool str_contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle || !needle[0]) return true;
    size_t nlen = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < nlen && p[i]) {
            char a = p[i];
            char b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
            if (a != b) break;
            i++;
        }
        if (i == nlen) return true;
    }
    return false;
}

int lpkg_get_catalog_count(void) {
    return g_catalog_count;
}

const lpkg_pkg_info_t *lpkg_get_catalog_item(int index) {
    if (index < 0 || index >= g_catalog_count) return NULL;
    return &g_catalog[index];
}

const lpkg_pkg_info_t *lpkg_find_catalog(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < g_catalog_count; i++) {
        if (strcmp(g_catalog[i].name, name) == 0) {
            return &g_catalog[i];
        }
    }
    return NULL;
}

static void lpkg_register_dynamic(const char *name, const char *version,
                                  const char *cat, const char *desc,
                                  const char *url) {
    if (!name || !name[0]) return;
    for (int i = 0; i < g_catalog_count; i++) {
        if (strcmp(g_catalog[i].name, name) == 0) {
            if (version && version[0]) copy_str_safe(g_catalog[i].version, version, sizeof(g_catalog[i].version));
            if (cat && cat[0]) copy_str_safe(g_catalog[i].category, cat, sizeof(g_catalog[i].category));
            if (desc && desc[0]) copy_str_safe(g_catalog[i].desc, desc, sizeof(g_catalog[i].desc));
            if (url && url[0]) copy_str_safe(g_catalog[i].remote_url, url, sizeof(g_catalog[i].remote_url));
            return;
        }
    }
    if (g_catalog_count >= LPKG_MAX_CATALOG) return;
    lpkg_pkg_info_t *it = &g_catalog[g_catalog_count++];
    memset(it, 0, sizeof(*it));
    copy_str_safe(it->name, name, sizeof(it->name));
    copy_str_safe(it->version, (version && version[0]) ? version : "1.0.0", sizeof(it->version));
    copy_str_safe(it->baseline_version, it->version, sizeof(it->baseline_version));
    copy_str_safe(it->category, (cat && cat[0]) ? cat : "Extra", sizeof(it->category));
    copy_str_safe(it->desc, (desc && desc[0]) ? desc : "Pacote instalado via URL/Repositorio", sizeof(it->desc));
    if (url && url[0]) copy_str_safe(it->remote_url, url, sizeof(it->remote_url));
    it->is_fullscreen_gui = (cat && (strcmp(cat, "Jogo") == 0 || strcmp(cat, "Grafico") == 0));
    it->is_core = false;
}

void lpkg_get_repo_url(char *buf, size_t buf_max) {
    if (!buf || buf_max == 0) return;
    uint32_t sz = 0;
    char *data = (char *)sdfs_read_file(LPKG_REPO_FILE, &sz);
    if (data && sz > 0) {
        size_t len = (sz < buf_max - 1) ? sz : (buf_max - 1);
        memcpy(buf, data, len);
        buf[len] = '\0';
        kfree(data);
        /* Remove quebras de linha no final */
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r' || buf[len - 1] == ' ')) {
            buf[--len] = '\0';
        }
        if (buf[0] != '\0') return;
    } else if (data) {
        kfree(data);
    }
    copy_str_safe(buf, LPKG_DEFAULT_REPO, buf_max);
}

void lpkg_set_repo_url(const char *url) {
    if (!url || !url[0]) return;
    sdfs_create_dir("/etc");
    char clean[160];
    copy_str_safe(clean, url, sizeof(clean));
    size_t len = strlen(clean);
    while (len > 1 && clean[len - 1] == '/') {
        clean[--len] = '\0';
    }
    sdfs_write_file(LPKG_REPO_FILE, (uint8_t *)clean, (uint32_t)len);
    sdfs_flush_bitmap();
}

bool lpkg_is_installed(const char *name, uint32_t *size_out) {
    if (!name || !name[0]) return false;
    char path[128];
    int is_dir = 0;
    uint32_t sz = 0;

    strcpy(path, "/bin/");
    strcat(path, name);
    if (strlen(name) <= 4 || strcmp(name + strlen(name) - 4, ".lxe") != 0) {
        strcat(path, ".lxe");
    }
    if (sdfs_path_info(path, &is_dir, &sz) == 0 && !is_dir && sz > 0) {
        if (size_out) *size_out = sz;
        return true;
    }

    strcpy(path, "/bin/");
    strcat(path, name);
    if (sdfs_path_info(path, &is_dir, &sz) == 0 && !is_dir && sz > 0) {
        if (size_out) *size_out = sz;
        return true;
    }

    return false;
}

static bool is_elf64_binary(const uint8_t *buf, size_t size) {
    if (!buf || size < 64) return false;
    return (buf[0] == 0x7F && buf[1] == 'E' && buf[2] == 'L' && buf[3] == 'F');
}

static void extract_pkg_basename(const char *url_or_path, char *out_name, size_t max_len) {
    const char *slash = strrchr(url_or_path, '/');
    const char *base = slash ? (slash + 1) : url_or_path;
    if (!base[0]) base = "app";
    copy_str_safe(out_name, base, max_len);
    /* Remove query string */
    char *q = strchr(out_name, '?');
    if (q) *q = '\0';
    /* Remove extensao .lxe ou .c */
    size_t len = strlen(out_name);
    if (len > 4 && strcmp(out_name + len - 4, ".lxe") == 0) {
        out_name[len - 4] = '\0';
    } else if (len > 2 && strcmp(out_name + len - 2, ".c") == 0) {
        out_name[len - 2] = '\0';
    }
    if (!out_name[0]) copy_str_safe(out_name, "app", max_len);
}

static void record_installed_db(const char *name, const char *version, const char *source, uint32_t size) {
    sdfs_create_dir("/etc");
    uint32_t old_sz = 0;
    char *old_db = (char *)sdfs_read_file(LPKG_DB_FILE, &old_sz);
    char new_db[2048];
    new_db[0] = '\0';

    if (old_db && old_sz > 0) {
        /* Mantem linhas de outros pacotes */
        size_t pos = 0;
        while (pos < old_sz) {
            char line[160];
            size_t li = 0;
            while (pos < old_sz && old_db[pos] != '\n' && li < sizeof(line) - 1) {
                if (old_db[pos] != '\r') line[li++] = old_db[pos];
                pos++;
            }
            if (pos < old_sz && old_db[pos] == '\n') pos++;
            line[li] = '\0';
            if (li == 0) continue;

            size_t nlen = strlen(name);
            if (strncmp(line, name, nlen) == 0 && line[nlen] == '|') {
                continue;
            }
            if (strlen(new_db) + li + 2 < sizeof(new_db)) {
                strcat(new_db, line);
                strcat(new_db, "\n");
            }
        }
        kfree(old_db);
    } else if (old_db) {
        kfree(old_db);
    }

    const lpkg_pkg_info_t *cat = lpkg_find_catalog(name);
    const char *ver = (version && version[0]) ? version : (cat ? cat->version : "1.0.0");

    char entry[160];
    char sbuf[16];
    itoa((int)size, sbuf, 10);
    strcpy(entry, name);
    strcat(entry, "|");
    strcat(entry, ver);
    strcat(entry, "|");
    strcat(entry, sbuf);
    strcat(entry, "|");
    strcat(entry, source ? source : "local");
    strcat(entry, "\n");
    if (strlen(new_db) + strlen(entry) + 1 < sizeof(new_db)) {
        strcat(new_db, entry);
    }
    sdfs_write_file(LPKG_DB_FILE, (uint8_t *)new_db, (uint32_t)strlen(new_db));
}

bool lpkg_get_installed_version(const char *name, char *ver_out, size_t max_len) {
    if (!name || !name[0]) return false;
    if (!lpkg_is_installed(name, NULL)) return false;

    uint32_t db_sz = 0;
    char *db = (char *)sdfs_read_file(LPKG_DB_FILE, &db_sz);
    if (db && db_sz > 0) {
        size_t pos = 0;
        size_t nlen = strlen(name);
        while (pos < db_sz) {
            char line[160];
            size_t li = 0;
            while (pos < db_sz && db[pos] != '\n' && li < sizeof(line) - 1) {
                if (db[pos] != '\r') line[li++] = db[pos];
                pos++;
            }
            if (pos < db_sz && db[pos] == '\n') pos++;
            line[li] = '\0';
            if (strncmp(line, name, nlen) == 0 && line[nlen] == '|') {
                char *vstart = line + nlen + 1;
                char *vend = strchr(vstart, '|');
                if (vend) {
                    *vend = '\0';
                    /* Verifica se o segundo campo e uma versao (contem ponto) */
                    if (strchr(vstart, '.') != NULL) {
                        if (ver_out && max_len > 0) copy_str_safe(ver_out, vstart, max_len);
                        kfree(db);
                        return true;
                    }
                }
            }
        }
        kfree(db);
    } else if (db) {
        kfree(db);
    }

    const lpkg_pkg_info_t *cat = lpkg_find_catalog(name);
    if (ver_out && max_len > 0) {
        copy_str_safe(ver_out, (cat && cat->baseline_version[0]) ? cat->baseline_version : "1.0.0", max_len);
    }
    return true;
}

bool lpkg_has_update(const char *name) {
    if (!name || !name[0]) return false;
    const lpkg_pkg_info_t *cat = lpkg_find_catalog(name);
    if (!cat) return false;
    char inst_ver[16];
    if (!lpkg_get_installed_version(name, inst_ver, sizeof(inst_ver))) return false;
    return (strcmp(inst_ver, cat->version) != 0);
}

static int install_elf_buffer(const char *pkg_name, const uint8_t *buf, uint32_t size,
                              const char *source_label, char *msg_out, size_t msg_max) {
    if (!is_elf64_binary(buf, size)) {
        copy_str_safe(msg_out, "Erro: binario invalido (assinatura ELF64 ausente).", msg_max);
        return -1;
    }

    sdfs_create_dir("/bin");
    char dest[128];
    strcpy(dest, "/bin/");
    strcat(dest, pkg_name);
    strcat(dest, ".lxe");

    uint32_t written = sdfs_write_file(dest, (uint8_t *)buf, size);
    if (written != size) {
        copy_str_safe(msg_out, "Erro: falha ao gravar pacote em /bin no SDFS.", msg_max);
        return -2;
    }

    const lpkg_pkg_info_t *cat = lpkg_find_catalog(pkg_name);
    sdfs_set_permissions(dest, 0755);
    record_installed_db(pkg_name, cat ? cat->version : "1.0.0", source_label, size);
    sdfs_flush_bitmap();

    if (msg_out && msg_max > 0) {
        char sbuf[16];
        itoa((int)(size / 1024U > 0 ? size / 1024U : 1U), sbuf, 10);
        char tmp[192];
        strcpy(tmp, "Instalado: ");
        strcat(tmp, dest);
        strcat(tmp, " (");
        strcat(tmp, sbuf);
        strcat(tmp, " KB via ");
        strcat(tmp, source_label);
        strcat(tmp, ")");
        copy_str_safe(msg_out, tmp, msg_max);
    }
    return 0;
}

static int compile_and_install_c_source(const char *pkg_name, const uint8_t *src_buf,
                                        uint32_t src_size, const char *source_label,
                                        char *msg_out, size_t msg_max) {
    sdfs_create_dir("/src");
    sdfs_create_dir("/bin");

    char src_path[128];
    strcpy(src_path, "/src/");
    strcat(src_path, pkg_name);
    strcat(src_path, ".c");

    if (sdfs_write_file(src_path, (uint8_t *)src_buf, src_size) != src_size) {
        copy_str_safe(msg_out, "Erro ao salvar codigo-fonte C em /src.", msg_max);
        return -1;
    }
    sdfs_set_permissions(src_path, 0644);

    char bin_path[128];
    strcpy(bin_path, "/bin/");
    strcat(bin_path, pkg_name);
    strcat(bin_path, ".lxe");

    char *tcc_argv[6];
    tcc_argv[0] = "tcc";
    tcc_argv[1] = "-o";
    tcc_argv[2] = bin_path;
    tcc_argv[3] = src_path;
    tcc_argv[4] = NULL;

    int pid = launch_initrd_program_argv("/bin/tcc.lxe", tcc_argv);
    if (pid <= 0) pid = launch_initrd_program_argv("bin/tcc.lxe", tcc_argv);
    if (pid <= 0) {
        copy_str_safe(msg_out, "Erro: compilador TinyCC (/bin/tcc.lxe) nao encontrado.", msg_max);
        return -2;
    }

    int status = 0;
    sys_waitpid(pid, &status, 0);

    uint32_t bin_sz = 0;
    if (!lpkg_is_installed(pkg_name, &bin_sz)) {
        copy_str_safe(msg_out, "Erro: falha na compilacao C com TinyCC.", msg_max);
        return -3;
    }

    sdfs_set_permissions(bin_path, 0755);
    record_installed_db(pkg_name, "1.0.0", source_label, bin_sz);
    sdfs_flush_bitmap();

    lpkg_register_dynamic(pkg_name, "1.0.0", "Compilado",
                          "Compilado em dispositivo via TinyCC (tcc)", source_label);

    if (msg_out && msg_max > 0) {
        char tmp[192];
        strcpy(tmp, "Compilado com TCC e instalado em ");
        strcat(tmp, bin_path);
        copy_str_safe(msg_out, tmp, msg_max);
    }
    return 0;
}

int lpkg_install_pkg(const char *pkg_or_url, const char *custom_name,
                     char *msg_out, size_t msg_max) {
    if (!pkg_or_url || !pkg_or_url[0]) {
        copy_str_safe(msg_out, "Especifique o nome do pacote ou URL HTTP(S).", msg_max);
        return -1;
    }

    bool is_url = (strncmp(pkg_or_url, "http://", 7) == 0 ||
                   strncmp(pkg_or_url, "https://", 8) == 0);

    char pkg_name[32];
    if (custom_name && custom_name[0]) {
        extract_pkg_basename(custom_name, pkg_name, sizeof(pkg_name));
    } else {
        extract_pkg_basename(pkg_or_url, pkg_name, sizeof(pkg_name));
    }

    /* 1. Instalacao direta via URL HTTP/HTTPS */
    if (is_url) {
        uint8_t *body = NULL;
        size_t size = 0;
        int status = 0;
        int rc = http_get_mem(pkg_or_url, &body, &size, &status, NULL, NULL);
        if (rc < 0 || !body || size == 0 || (status != 200 && status != 0)) {
            if (body) kfree(body);
            copy_str_safe(msg_out, "Falha ao baixar pacote da URL especificada.", msg_max);
            return -2;
        }

        int res;
        size_t ulen = strlen(pkg_or_url);
        bool looks_like_c = (ulen > 2 && strcmp(pkg_or_url + ulen - 2, ".c") == 0) ||
                            (!is_elf64_binary(body, size) && strstr((const char *)body, "main") != NULL);

        if (is_elf64_binary(body, size)) {
            res = install_elf_buffer(pkg_name, body, (uint32_t)size, pkg_or_url, msg_out, msg_max);
            if (res == 0) {
                lpkg_register_dynamic(pkg_name, "1.0.0", "Web",
                                      "Pacote baixado diretamente via HTTP/HTTPS", pkg_or_url);
            }
        } else if (looks_like_c) {
            res = compile_and_install_c_source(pkg_name, body, (uint32_t)size, pkg_or_url, msg_out, msg_max);
        } else {
            copy_str_safe(msg_out, "Arquivo baixado nao e um executavel .lxe nem codigo C.", msg_max);
            res = -3;
        }

        kfree(body);
        return res;
    }

    /* 2. Verifica se o pacote no catalogo possui remote_url especifica (ex: GitHub HTTPS) */
    const lpkg_pkg_info_t *cat = lpkg_find_catalog(pkg_name);
    if (cat && cat->remote_url[0] != '\0') {
        uint8_t *body = NULL;
        size_t size = 0;
        int status = 0;
        if (http_get_mem(cat->remote_url, &body, &size, &status, NULL, NULL) >= 0 &&
            body && size > 0 && status == 200) {
            size_t rlen = strlen(cat->remote_url);
            bool is_c = (rlen > 2 && strcmp(cat->remote_url + rlen - 2, ".c") == 0);
            int res;
            if (is_c && !is_elf64_binary(body, size)) {
                res = compile_and_install_c_source(pkg_name, body, (uint32_t)size, "GitHub HTTPS", msg_out, msg_max);
            } else {
                res = install_elf_buffer(pkg_name, body, (uint32_t)size, "GitHub HTTPS", msg_out, msg_max);
            }
            kfree(body);
            if (res == 0) return 0;
        } else if (body) {
            kfree(body);
        }
    }

    /* 3. Procura no mirror local SDFS (/pkg/<nome>.lxe) ou initrd (pkg/<nome>.lxe) */
    char sdfs_pkg_path[128];
    strcpy(sdfs_pkg_path, "/pkg/");
    strcat(sdfs_pkg_path, pkg_name);
    strcat(sdfs_pkg_path, ".lxe");

    uint32_t pkg_sz = 0;
    uint8_t *pkg_data = (uint8_t *)sdfs_read_file(sdfs_pkg_path, &pkg_sz);
    if (pkg_data && pkg_sz > 0) {
        int res = install_elf_buffer(pkg_name, pkg_data, pkg_sz, "sdfs:/pkg", msg_out, msg_max);
        kfree(pkg_data);
        return res;
    }
    if (pkg_data) kfree(pkg_data);

    char initrd_pkg_path[128];
    strcpy(initrd_pkg_path, "pkg/");
    strcat(initrd_pkg_path, pkg_name);
    strcat(initrd_pkg_path, ".lxe");

    uint32_t initrd_sz = 0;
    uint8_t *initrd_data = (uint8_t *)initrd_get_file(initrd_pkg_path, &initrd_sz);
    if (!initrd_data) {
        strcpy(initrd_pkg_path, "bin/");
        strcat(initrd_pkg_path, pkg_name);
        strcat(initrd_pkg_path, ".lxe");
        initrd_data = (uint8_t *)initrd_get_file(initrd_pkg_path, &initrd_sz);
    }
    if (initrd_data && initrd_sz > 0) {
        return install_elf_buffer(pkg_name, initrd_data, initrd_sz, "sys:pkg", msg_out, msg_max);
    }

    /* 4. Tenta baixar do repositorio HTTP configurado em /etc/lpkg.repo */
    char repo_base[160];
    lpkg_get_repo_url(repo_base, sizeof(repo_base));
    if (repo_base[0] != '\0') {
        char url[256];
        strcpy(url, repo_base);
        strcat(url, "/");
        strcat(url, pkg_name);
        strcat(url, ".lxe");

        uint8_t *body = NULL;
        size_t size = 0;
        int status = 0;
        if (http_get_mem(url, &body, &size, &status, NULL, NULL) >= 0 &&
            body && size > 0 && status == 200) {
            int res = install_elf_buffer(pkg_name, body, (uint32_t)size, repo_base, msg_out, msg_max);
            kfree(body);
            if (res == 0) {
                lpkg_register_dynamic(pkg_name, "1.0.0", "Repo",
                                      "Baixado do repositorio HTTP remoto", url);
                return 0;
            }
        } else if (body) {
            kfree(body);
        }
    }

    copy_str_safe(msg_out, "Pacote nao encontrado no catalogo local nem no repositorio HTTP.", msg_max);
    return -4;
}

int lpkg_remove_pkg(const char *name, char *msg_out, size_t msg_max) {
    if (!name || !name[0]) {
        copy_str_safe(msg_out, "Informe o nome do pacote a remover.", msg_max);
        return -1;
    }

    char pkg_name[32];
    extract_pkg_basename(name, pkg_name, sizeof(pkg_name));

    const lpkg_pkg_info_t *cat = lpkg_find_catalog(pkg_name);
    if (cat && cat->is_core) {
        copy_str_safe(msg_out, "Pacote essencial do sistema nao pode ser removido.", msg_max);
        return -2;
    }

    char path[128];
    strcpy(path, "/bin/");
    strcat(path, pkg_name);
    strcat(path, ".lxe");

    int is_dir = 0;
    uint32_t sz = 0;
    if (sdfs_path_info(path, &is_dir, &sz) != 0) {
        copy_str_safe(msg_out, "Pacote nao esta instalado em /bin.", msg_max);
        return -3;
    }

    if (sdfs_delete(path) != 0) {
        copy_str_safe(msg_out, "Erro ao remover executavel do disco SDFS.", msg_max);
        return -4;
    }
    sdfs_flush_bitmap();

    if (msg_out && msg_max > 0) {
        char tmp[128];
        strcpy(tmp, "Pacote '");
        strcat(tmp, pkg_name);
        strcat(tmp, "' removido com sucesso.");
        copy_str_safe(msg_out, tmp, msg_max);
    }
    return 0;
}

static int lpkg_update_repo_progress(char *msg_out, size_t msg_max,
                                    lpkg_progress_fn progress_cb, void *user_data,
                                    int overall_start, int overall_span) {
    char repo_base[160];
    lpkg_get_repo_url(repo_base, sizeof(repo_base));

    char index_url[224];
    strcpy(index_url, repo_base);
    strcat(index_url, "/repo.txt");

    uint8_t *body = NULL;
    size_t size = 0;
    int status = 0;
    int added = 0;
    lpkg_download_progress_t progress = {
        .callback = progress_cb,
        .user_data = user_data,
        .phase = "Catalogo de pacotes",
        .message = "Recebidos",
        .expected_size = 0,
        .overall_start = overall_start,
        .overall_span = overall_span
    };
    lpkg_report_progress(progress_cb, user_data, overall_start, 0, 0,
                         "Catalogo de pacotes", "Baixando o indice do repositorio.");

    if (http_get_mem_progress(index_url, &body, &size, &status, NULL, NULL,
                              lpkg_download_progress, &progress) >= 0 &&
        body && size > 0 && status == 200) {
        /* Formato por linha: nome|versao|categoria|descricao */
        size_t pos = 0;
        while (pos < size) {
            char line[224];
            size_t li = 0;
            while (pos < size && body[pos] != '\n' && li < sizeof(line) - 1) {
                if (body[pos] != '\r') line[li++] = (char)body[pos];
                pos++;
            }
            if (pos < size && body[pos] == '\n') pos++;
            line[li] = '\0';
            if (li == 0 || line[0] == '#') continue;

            char *p1 = strchr(line, '|');
            if (!p1) continue;
            *p1++ = '\0';
            char *p2 = strchr(p1, '|');
            if (!p2) continue;
            *p2++ = '\0';
            char *p3 = strchr(p2, '|');
            if (!p3) continue;
            *p3++ = '\0';

            char pkg_url[224];
            strcpy(pkg_url, repo_base);
            strcat(pkg_url, "/");
            strcat(pkg_url, line);
            strcat(pkg_url, ".lxe");

            lpkg_register_dynamic(line, p1, p2, p3, pkg_url);
            added++;
        }
        kfree(body);
        g_repo_synced = true;

        char tmp[160];
        char nbuf[16];
        itoa(g_catalog_count, nbuf, 10);
        strcpy(tmp, "GitHub HTTPS sincronizado (");
        strcat(tmp, nbuf);
        strcat(tmp, " pacotes atualizados).");
        copy_str_safe(msg_out, tmp, msg_max);
        (void)added;
        return 0;
    }
    if (body) kfree(body);

    char tmp[160];
    char nbuf[16];
    itoa(g_catalog_count, nbuf, 10);
    strcpy(tmp, "Catalogo local SDFS sincronizado (");
    strcat(tmp, nbuf);
    strcat(tmp, " pacotes prontos).");
    copy_str_safe(msg_out, tmp, msg_max);
    return 0;
}

int lpkg_update_repo(char *msg_out, size_t msg_max) {
    return lpkg_update_repo_progress(msg_out, msg_max, NULL, NULL, 0, 0);
}

void lpkg_get_os_version(char *ver_out, size_t max_len) {
    if (!ver_out || max_len == 0) return;
    copy_str_safe(ver_out, LPKG_BUILD_VERSION, max_len);

    uint32_t sz = 0;
    char *data = (char *)sdfs_read_file(LPKG_OS_RELEASE, &sz);
    if (data && sz > 0) {
        size_t len = (sz < 255) ? sz : 255;
        char buf[256];
        memcpy(buf, data, len);
        buf[len] = '\0';
        kfree(data);

        char *v = strstr(buf, "VERSION=");
        if (v) {
            v += 8;
            size_t i = 0;
            while (v[i] && v[i] != '\n' && v[i] != '\r' && i < max_len - 1) {
                ver_out[i] = v[i];
                i++;
            }
            ver_out[i] = '\0';
        }
    } else if (data) {
        kfree(data);
    }
}

static int lpkg_check_system_update_progress(lpkg_sysupdate_info_t *info_out,
                                             char *msg_out, size_t msg_max,
                                             lpkg_progress_fn progress_cb,
                                             void *user_data,
                                             int overall_start,
                                             int overall_span) {
    if (!info_out) return -1;
    memset(info_out, 0, sizeof(*info_out));
    lpkg_get_os_version(info_out->current_version, sizeof(info_out->current_version));
    copy_str_safe(info_out->remote_version, info_out->current_version, sizeof(info_out->remote_version));

    char repo_base[160];
    lpkg_get_repo_url(repo_base, sizeof(repo_base));

    char sys_url[224];
    strcpy(sys_url, repo_base);
    strcat(sys_url, "/" LPKG_SYSTEM_MANIFEST);
    char cache_key[16];
    itoa((int)timer_ticks, cache_key, 10);
    strcat(sys_url, "?t=");
    strcat(sys_url, cache_key);

    uint8_t *body = NULL;
    size_t size = 0;
    int status = 0;
    lpkg_download_progress_t progress = {
        .callback = progress_cb,
        .user_data = user_data,
        .phase = "Manifesto de sistema",
        .message = "Recebidos",
        .expected_size = 0,
        .overall_start = overall_start,
        .overall_span = overall_span
    };
    lpkg_report_progress(progress_cb, user_data, overall_start, 0, 0,
                         "Manifesto de sistema", "Baixando e lendo system-v2.txt.");
    if (http_get_mem_progress(sys_url, &body, &size, &status, NULL, NULL,
                              lpkg_download_progress, &progress) < 0 ||
        !body || size == 0 || status != 200) {
        if (body) kfree(body);
        copy_str_safe(msg_out, "Nao foi possivel consultar system.txt no GitHub HTTPS.", msg_max);
        return -2;
    }

    size_t pos = 0;
    while (pos < size) {
        char line[224];
        size_t li = 0;
        while (pos < size && body[pos] != '\n' && li < sizeof(line) - 1) {
            if (body[pos] != '\r') line[li++] = (char)body[pos];
            pos++;
        }
        if (pos < size && body[pos] == '\n') pos++;
        line[li] = '\0';
        if (li == 0 || line[0] == '#') continue;

        if (strncmp(line, "VERSION=", 8) == 0) {
            copy_str_safe(info_out->remote_version, line + 8, sizeof(info_out->remote_version));
        } else if (strncmp(line, "DATE=", 5) == 0) {
            copy_str_safe(info_out->release_date, line + 5, sizeof(info_out->release_date));
        } else if (strncmp(line, "PACKAGES=", 9) == 0) {
            copy_str_safe(info_out->packages, line + 9, sizeof(info_out->packages));
        } else if (strncmp(line, "NOTES=", 6) == 0) {
            if (strlen(info_out->notes) + strlen(line + 6) + 2 < sizeof(info_out->notes)) {
                if (info_out->notes[0] != '\0') strcat(info_out->notes, "\n");
                strcat(info_out->notes, line + 6);
            }
        } else if (strncmp(line, "BOOT_IMAGE_URL=", 15) == 0) {
            copy_str_safe(info_out->boot_image_url, line + 15, sizeof(info_out->boot_image_url));
        } else if (strncmp(line, "BOOT_IMAGE_SIZE=", 16) == 0) {
            info_out->boot_image_size = lpkg_parse_u32(line + 16, 10);
        } else if (strncmp(line, "BOOT_IMAGE_CRC32=", 17) == 0) {
            const char *value = line + 17;
            if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) value += 2;
            info_out->boot_image_crc32 = lpkg_parse_u32(value, 16);
        } else if (strncmp(line, "SYSTEM_ARCHIVE_URL=", 19) == 0) {
            copy_str_safe(info_out->system_archive_url, line + 19, sizeof(info_out->system_archive_url));
        } else if (strncmp(line, "SYSTEM_ARCHIVE_SIZE=", 20) == 0) {
            info_out->system_archive_size = lpkg_parse_u32(line + 20, 10);
        } else if (strncmp(line, "SYSTEM_ARCHIVE_CRC32=", 21) == 0) {
            const char *value = line + 21;
            if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) value += 2;
            info_out->system_archive_crc32 = lpkg_parse_u32(value, 16);
        }
    }
    kfree(body);

    info_out->boot_image_available = info_out->boot_image_url[0] &&
        info_out->boot_image_size > (16U * 2048U) &&
        info_out->boot_image_size < (SDFS_HD_PARTITION_LBA * 512U);
    info_out->system_archive_available = info_out->system_archive_url[0] &&
        info_out->system_archive_size >= 1024U && info_out->system_archive_size <= (64U * 1024U * 1024U);

    info_out->checked_online = true;
    info_out->update_available = (strcmp(info_out->current_version, info_out->remote_version) != 0);

    if (info_out->update_available) {
        char tmp[160];
        if (info_out->boot_image_available && info_out->system_archive_available) {
            strcpy(tmp, "Atualizacao completa disponivel no GitHub: LiwusOS v");
            strcat(tmp, info_out->remote_version);
        } else {
            strcpy(tmp, "Feed incompleto para atualizacao do kernel e sistema de arquivos (v");
            strcat(tmp, info_out->remote_version);
            strcat(tmp, ").");
        }
        copy_str_safe(msg_out, tmp, msg_max);
    } else {
        char tmp[160];
        strcpy(tmp, "O sistema ja esta na versao mais recente (v");
        strcat(tmp, info_out->current_version);
        strcat(tmp, ").");
        copy_str_safe(msg_out, tmp, msg_max);
    }
    return 0;
}

int lpkg_check_system_update(lpkg_sysupdate_info_t *info_out, char *msg_out, size_t msg_max) {
    return lpkg_check_system_update_progress(info_out, msg_out, msg_max,
                                             NULL, NULL, 0, 0);
}

static int lpkg_fetch_checked(const char *url, uint32_t expected_size, uint32_t expected_crc,
                              uint8_t **out, char *error, size_t error_max,
                              lpkg_progress_fn progress_cb, void *user_data,
                              const char *phase, const char *message,
                              int overall_start, int overall_span) {
    uint8_t *data = NULL;
    size_t size = 0;
    int status = 0;
    lpkg_download_progress_t progress = {
        .callback = progress_cb,
        .user_data = user_data,
        .phase = phase,
        .message = message,
        .expected_size = expected_size,
        .overall_start = overall_start,
        .overall_span = overall_span
    };
    lpkg_report_progress(progress_cb, user_data, overall_start, 0,
                         expected_size, phase, message);
    if (!url || strncmp(url, "https://", 8) != 0 || !expected_size ||
        http_get_mem_progress(url, &data, &size, &status, NULL, NULL,
                              lpkg_download_progress, &progress) < 0 || !data || status != 200) {
        if (data) kfree(data);
        copy_str_safe(error, "Falha ao baixar um artefato HTTPS da atualizacao.", error_max);
        return -1;
    }
    if (size != expected_size || crc32_calc(data, (uint32_t)size) != expected_crc) {
        kfree(data);
        copy_str_safe(error, "Tamanho ou CRC32 do artefato nao confere; nada foi instalado.", error_max);
        return -1;
    }
    lpkg_report_progress(progress_cb, user_data, overall_start + overall_span,
                         size, expected_size, phase, "Download concluido; tamanho e CRC32 conferidos.");
    *out = data;
    return 0;
}

typedef struct {
    lpkg_progress_fn callback;
    void *user_data;
    const char *phase;
    const char *message;
    int overall_start;
    int overall_span;
} lpkg_region_progress_t;

static void lpkg_region_progress(uint32_t completed, uint32_t total, void *user_data) {
    lpkg_region_progress_t *ctx = (lpkg_region_progress_t *)user_data;
    if (!ctx || !ctx->callback) return;
    int local = total ? (int)(((uint64_t)completed * 100U) / total) : 0;
    int overall = ctx->overall_start + (ctx->overall_span * local) / 100;
    lpkg_report_progress(ctx->callback, ctx->user_data, overall, completed,
                         total, ctx->phase, ctx->message);
}

static int lpkg_apply_boot_image(const uint8_t *image, uint32_t image_size,
                                 char *msg, size_t msg_max,
                                 lpkg_progress_fn progress_cb, void *user_data) {
    if (!image || image_size <= (16U * 2048U) || image_size >= (SDFS_HD_PARTITION_LBA * 512U) ||
        memcmp(image + (16U * 2048U) + 1, "CD001", 5) != 0 ||
        sdfs_get_partition_lba() != SDFS_HD_PARTITION_LBA) {
        copy_str_safe(msg, "Imagem de boot invalida ou sistema nao iniciado de uma instalacao HD.", msg_max);
        return -1;
    }

    uint32_t sectors = (image_size + 511U) / 512U;
    uint32_t bytes = sectors * 512U;
    uint8_t *new_data = (uint8_t *)kmalloc_a(bytes);
    uint8_t *old_data = (uint8_t *)kmalloc_a(bytes);
    uint8_t *verify = (uint8_t *)kmalloc_a(bytes);
    if (!new_data || !old_data || !verify) {
        if (new_data) kfree(new_data);
        if (old_data) kfree(old_data);
        if (verify) kfree(verify);
        copy_str_safe(msg, "Memoria insuficiente para preparar atualizacao e rollback do boot.", msg_max);
        return -1;
    }
    memcpy(new_data, image, image_size);
    if (bytes > image_size) memset(new_data + image_size, 0, bytes - image_size);
    lpkg_region_progress_t region_progress = {
        .callback = progress_cb,
        .user_data = user_data,
        .phase = "Protegendo boot atual",
        .message = "Lendo a area atual para rollback.",
        .overall_start = 94,
        .overall_span = 1
    };
    lpkg_report_progress(progress_cb, user_data, 94, 0, sectors,
                         "Protegendo boot atual", "Lendo a area atual para rollback.");
    if (sdfs_read_hd_boot_region_progress(0, sectors, old_data,
                                          lpkg_region_progress, &region_progress) < 0) {
        copy_str_safe(msg, "Nao foi possivel guardar o boot atual; disco nao alterado.", msg_max);
        kfree(new_data); kfree(old_data); kfree(verify);
        return -1;
    }

    lpkg_report_progress(progress_cb, user_data, 95, 0, sectors,
                         "Gravando boot", "Escrevendo a imagem de boot no disco.");
    region_progress.phase = "Gravando boot";
    region_progress.message = "Setores gravados";
    region_progress.overall_start = 95;
    region_progress.overall_span = 2;
    int write_result = sdfs_write_hd_boot_region_progress(0, sectors, new_data,
                                                          lpkg_region_progress,
                                                          &region_progress);
    if (write_result == 0) {
        lpkg_report_progress(progress_cb, user_data, 97, sectors, sectors,
                             "Verificando boot", "Relendo a imagem gravada.");
    }
    region_progress.phase = "Verificando boot";
    region_progress.message = "Setores relidos";
    region_progress.overall_start = 97;
    region_progress.overall_span = 1;
    int read_result = write_result == 0
        ? sdfs_read_hd_boot_region_progress(0, sectors, verify,
                                            lpkg_region_progress, &region_progress) : -1;
    int write_ok = write_result == 0 && read_result == 0 &&
                   memcmp(new_data, verify, bytes) == 0;
    if (!write_ok) {
        int rollback_ok = sdfs_write_hd_boot_region(0, sectors, old_data) == 0;
        copy_str_safe(msg, rollback_ok
            ? "Falha ao verificar boot novo; imagem anterior restaurada."
            : "Falha de boot e de rollback; use a ISO de recuperacao.", msg_max);
        kfree(new_data); kfree(old_data); kfree(verify);
        return -1;
    }
    lpkg_report_progress(progress_cb, user_data, 98, sectors, sectors,
                         "Verificando boot", "Leitura de retorno conferida byte a byte.");
    kfree(new_data); kfree(old_data); kfree(verify);
    return 0;
}

int lpkg_perform_system_update(lpkg_progress_fn progress_cb, void *user_data,
                               char *msg_out, size_t msg_max) {
    char sync_msg[128];
    lpkg_update_repo_progress(sync_msg, sizeof(sync_msg), progress_cb,
                              user_data, 0, 3);

    lpkg_sysupdate_info_t info;
    if (lpkg_check_system_update_progress(&info, sync_msg, sizeof(sync_msg),
                                          progress_cb, user_data, 3, 7) < 0) {
        copy_str_safe(msg_out, sync_msg, msg_max);
        return -1;
    }

    if (!info.boot_image_available || !info.system_archive_available) {
        copy_str_safe(msg_out, "O feed nao publicou a imagem de boot e o arquivo completo do sistema.", msg_max);
        return -1;
    }
    if (sdfs_get_partition_lba() != SDFS_HD_PARTITION_LBA) {
        copy_str_safe(msg_out, "Atualizacao completa requer LiwusOS instalado no HD; dados nao foram alterados.", msg_max);
        return -1;
    }

    uint8_t *boot_image = NULL;
    uint8_t *system_archive = NULL;
    if (lpkg_fetch_checked(info.boot_image_url, info.boot_image_size, info.boot_image_crc32,
                           &boot_image, sync_msg, sizeof(sync_msg), progress_cb,
                           user_data, "Download da imagem de boot",
                           "Recebidos", 10, 25) < 0) {
        copy_str_safe(msg_out, sync_msg, msg_max);
        return -1;
    }
    if (lpkg_fetch_checked(info.system_archive_url, info.system_archive_size, info.system_archive_crc32,
                           &system_archive, sync_msg, sizeof(sync_msg), progress_cb,
                           user_data, "Download dos arquivos do sistema",
                           "Recebidos", 35, 40) < 0) {
        kfree(boot_image);
        copy_str_safe(msg_out, sync_msg, msg_max);
        return -1;
    }
    lpkg_report_progress(progress_cb, user_data, 75, 0, 0,
                         "Validacao", "Conferindo formato ISO, TAR, tamanhos e CRC32.");
    if (info.boot_image_size <= (16U * 2048U) ||
        memcmp(boot_image + (16U * 2048U) + 1, "CD001", 5) != 0 ||
        lpkg_tar_validate(system_archive, info.system_archive_size) < 0) {
        kfree(boot_image);
        kfree(system_archive);
        copy_str_safe(msg_out, "Artefato de sistema invalido; instalacao atual preservada.", msg_max);
        return -1;
    }

    lpkg_report_progress(progress_cb, user_data, 80, 0, 0,
                         "Instalando arquivos", "Aplicando arquivos do sistema preservando dados do usuario.");
    if (lpkg_install_system_archive(system_archive, info.system_archive_size, progress_cb, user_data) < 0) {
        kfree(boot_image);
        kfree(system_archive);
        copy_str_safe(msg_out, "Falha ao instalar arquivos do sistema; a versao nao foi marcada como atualizada.", msg_max);
        return -1;
    }
    kfree(system_archive);

    if (lpkg_apply_boot_image(boot_image, info.boot_image_size, sync_msg,
                              sizeof(sync_msg), progress_cb, user_data) < 0) {
        kfree(boot_image);
        copy_str_safe(msg_out, sync_msg, msg_max);
        return -1;
    }
    kfree(boot_image);

    /* Grava /etc/os-release no SDFS */
    lpkg_report_progress(progress_cb, user_data, 99, 0, 0,
                         "Finalizando", "Gravando a versao instalada e sincronizando o sistema de arquivos.");
    sdfs_create_dir("/etc");
    char os_rel[192];
    strcpy(os_rel, "NAME=LiwusOS Plasma Edition\nVERSION=");
    strcat(os_rel, info.remote_version);
    strcat(os_rel, "\nDATE=");
    strcat(os_rel, info.release_date[0] ? info.release_date : "2026-11-11");
    strcat(os_rel, "\nCHANNEL=GitHub-OTA\n");
    sdfs_write_file(LPKG_OS_RELEASE, (uint8_t *)os_rel, (uint32_t)strlen(os_rel));
    sdfs_flush_bitmap();

    lpkg_report_progress(progress_cb, user_data, 100, 1, 1,
                         "Concluido", "Atualizacao concluida. Reinicie para carregar o kernel novo.");

    char tmp[192];
    strcpy(tmp, "Sistema completo atualizado para LiwusOS v");
    strcat(tmp, info.remote_version);
    strcat(tmp, ". Reinicie para aplicar o kernel novo.");
    copy_str_safe(msg_out, tmp, msg_max);
    return 0;
}

static void print_pkg_row(const lpkg_pkg_info_t *it) {
    uint32_t sz = 0;
    bool inst = lpkg_is_installed(it->name, &sz);
    bool has_upd = inst && lpkg_has_update(it->name);

    vga_puts("  ");
    vga_puts(it->name);
    size_t nlen = strlen(it->name);
    for (size_t s = nlen; s < 11; s++) vga_putc(' ');

    vga_puts("v");
    vga_puts(it->version);
    size_t vlen = strlen(it->version) + 1;
    for (size_t s = vlen; s < 8; s++) vga_putc(' ');

    if (has_upd) {
        vga_puts("[atualizar]  ");
    } else if (inst) {
        vga_puts("[instalado]  ");
    } else {
        vga_puts("[disponivel] ");
    }

    vga_puts("[");
    vga_puts(it->category);
    vga_puts("] ");
    vga_puts(it->desc);
    vga_puts("\n");
}

void cmd_lpkg(int argc, char **argv) {
    if (argc < 2 || !argv[1] || strcmp(argv[1], "help") == 0) {
        vga_puts("LiwusOS Package Manager (lpkg) v2.1\n");
        vga_puts("Uso:\n");
        vga_puts("  lpkg list                    Sincroniza e lista pacotes do catalogo\n");
        vga_puts("  lpkg search <termo>          Busca pacotes por nome ou descricao\n");
        vga_puts("  lpkg install <nome | url>    Instala/atualiza pacote do GitHub ou URL\n");
        vga_puts("  lpkg remove <nome>           Remove um pacote instalado de /bin\n");
        vga_puts("  lpkg installed               Lista apenas os pacotes instalados\n");
        vga_puts("  lpkg info <nome>             Exibe detalhes de um pacote\n");
        vga_puts("  lpkg repo [url]              Exibe ou define a URL do repositorio HTTP\n");
        vga_puts("  lpkg update                  Sincroniza indice de pacotes com GitHub\n");
        vga_puts("  lpkg sysupdate               Verifica e atualiza o sistema LiwusOS inteiro\n");
        return;
    }

    const char *sub = argv[1];

    if (strcmp(sub, "list") == 0) {
        if (!g_repo_synced) {
            char sync_msg[128];
            lpkg_update_repo(sync_msg, sizeof(sync_msg));
        }
        vga_puts("=== Catalogo de Pacotes LiwusOS (lpkg) ===\n");
        for (int i = 0; i < g_catalog_count; i++) {
            print_pkg_row(&g_catalog[i]);
        }
        vga_puts("\nDica: use 'lpkg install <nome>' ou 'lpkg sysupdate'\n");
        return;
    }

    if (strcmp(sub, "installed") == 0) {
        vga_puts("=== Pacotes Instalados em /bin ===\n");
        int found = 0;
        for (int i = 0; i < g_catalog_count; i++) {
            uint32_t sz = 0;
            if (lpkg_is_installed(g_catalog[i].name, &sz)) {
                print_pkg_row(&g_catalog[i]);
                found++;
            }
        }
        if (found == 0) {
            vga_puts("  (nenhum pacote encontrado)\n");
        }
        return;
    }

    if (strcmp(sub, "search") == 0) {
        const char *q = (argc >= 3) ? argv[2] : "";
        vga_puts("=== Resultado da Busca ===\n");
        int matches = 0;
        for (int i = 0; i < g_catalog_count; i++) {
            const lpkg_pkg_info_t *it = &g_catalog[i];
            if (str_contains_ci(it->name, q) ||
                str_contains_ci(it->desc, q) ||
                str_contains_ci(it->category, q)) {
                print_pkg_row(it);
                matches++;
            }
        }
        if (matches == 0) {
            vga_puts("  Nenhum pacote correspondente.\n");
        }
        return;
    }

    if (strcmp(sub, "info") == 0) {
        if (argc < 3 || !argv[2]) {
            vga_puts("Uso: lpkg info <nome>\n");
            return;
        }
        const lpkg_pkg_info_t *it = lpkg_find_catalog(argv[2]);
        if (!it) {
            vga_puts("lpkg: pacote nao encontrado no catalogo.\n");
            return;
        }
        uint32_t sz = 0;
        bool inst = lpkg_is_installed(it->name, &sz);
        vga_puts("Pacote    : "); vga_puts(it->name); vga_puts("\n");
        vga_puts("Versao    : "); vga_puts(it->version); vga_puts("\n");
        vga_puts("Categoria : "); vga_puts(it->category); vga_puts("\n");
        vga_puts("Descricao : "); vga_puts(it->desc); vga_puts("\n");
        vga_puts("Estado    : "); vga_puts(inst ? "Instalado (/bin/" : "Disponivel (/pkg/");
        vga_puts(it->name); vga_puts(".lxe)\n");
        if (inst) {
            char sbuf[16];
            itoa((int)sz, sbuf, 10);
            vga_puts("Tamanho   : "); vga_puts(sbuf); vga_puts(" bytes\n");
        }
        return;
    }

    if (strcmp(sub, "install") == 0 || strcmp(sub, "add") == 0) {
        if (argc < 3 || !argv[2]) {
            vga_puts("Uso: lpkg install <nome | url> [nome_destino]\n");
            return;
        }
        const char *target = argv[2];
        const char *custom_name = (argc >= 4) ? argv[3] : NULL;
        char msg[192];
        vga_puts("[lpkg] Instalando '");
        vga_puts(target);
        vga_puts("'...\n");
        int rc = lpkg_install_pkg(target, custom_name, msg, sizeof(msg));
        vga_puts("[lpkg] ");
        vga_puts(msg);
        vga_puts("\n");
        if (rc == 0) {
            char pkg_name[32];
            extract_pkg_basename(custom_name ? custom_name : target, pkg_name, sizeof(pkg_name));
            vga_puts("[lpkg] Pronto! Digite '");
            vga_puts(pkg_name);
            vga_puts("' para executar.\n");
        }
        return;
    }

    if (strcmp(sub, "remove") == 0 || strcmp(sub, "uninstall") == 0 || strcmp(sub, "rm") == 0) {
        if (argc < 3 || !argv[2]) {
            vga_puts("Uso: lpkg remove <nome>\n");
            return;
        }
        char msg[160];
        lpkg_remove_pkg(argv[2], msg, sizeof(msg));
        vga_puts("[lpkg] ");
        vga_puts(msg);
        vga_puts("\n");
        return;
    }

    if (strcmp(sub, "repo") == 0) {
        if (argc >= 3 && argv[2]) {
            lpkg_set_repo_url(argv[2]);
            vga_puts("[lpkg] Repositorio HTTP definido para: ");
            vga_puts(argv[2]);
            vga_puts("\n");
        } else {
            char repo[160];
            lpkg_get_repo_url(repo, sizeof(repo));
            vga_puts("Repositorio HTTP atual: ");
            vga_puts(repo);
            vga_puts("\n");
        }
        return;
    }

    if (strcmp(sub, "update") == 0) {
        char msg[160];
        vga_puts("[lpkg] Sincronizando catalogo de pacotes com GitHub...\n");
        lpkg_update_repo(msg, sizeof(msg));
        vga_puts("[lpkg] ");
        vga_puts(msg);
        vga_puts("\n");
        return;
    }

    if (strcmp(sub, "sysupdate") == 0 || strcmp(sub, "upgrade") == 0) {
        char msg[192];
        g_sysupdate_last_bucket = -1;
        g_sysupdate_last_bytes = 0;
        g_sysupdate_last_phase[0] = '\0';
        vga_puts("[lpkg] Atualizacao do sistema: fases e bytes reais\n");
        int rc = lpkg_perform_system_update(lpkg_terminal_update_progress, NULL,
                                            msg, sizeof(msg));
        vga_puts("[lpkg] ");
        vga_puts(msg);
        vga_puts("\n");
        if (rc < 0) vga_puts("[lpkg] A atualizacao parou; consulte a fase acima para localizar o erro.\n");
        return;
    }

    vga_puts("lpkg: subcomando desconhecido '");
    vga_puts(sub);
    vga_puts("'. Use 'lpkg help'.\n");
}
