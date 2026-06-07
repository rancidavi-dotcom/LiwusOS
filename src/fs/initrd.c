#include "initrd.h"
#include "sdfs.h"
#include <stddef.h>
#include "kheap.h"
#include "serial.h"
#include "string.h"

static uint32_t initrd_location;
static uint32_t initrd_size;

static const char *normalize_initrd_name(const char *name) {
    if (!name) return name;
    if (name[0] == '.' && name[1] == '/') {
        return name + 2;
    }
    if (name[0] == '/') {
        return name + 1;
    }
    return name;
}

static int initrd_name_equals(const char *a, const char *b) {
    const char *na = normalize_initrd_name(a);
    const char *nb = normalize_initrd_name(b);
    return strcmp(na, nb) == 0;
}

static uint32_t get_size(const char *in) {
    uint32_t size = 0;
    const char *p = in;
    // Pula espaços iniciais
    while (*p == ' ') p++;
    // Converte octal até encontrar espaço ou nulo (máximo 11 caracteres)
    for (int j = 0; j < 11; j++) {
        if (p[j] < '0' || p[j] > '7') break;
        size = size * 8 + (p[j] - '0');
    }
    return size;
}

static struct dirent dirent;

static uint32_t initrd_read(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    uint32_t file_size = 0;
    void *file_data = initrd_get_file(node->name, &file_size);
    if (!file_data) return 0;

    if (offset > file_size) return 0;
    if (offset + size > file_size) size = file_size - offset;

    memcpy(buffer, (uint8_t *)file_data + offset, size);
    return size;
}

// readdir vazio para o diretorio virtual "localhost" em LIVECD mode.
// Quando o SDFS for montado, vfs_mount substitui este node pelo
// root do SDFS, entao este callback nunca e usado em modo persistente.
static struct dirent *initrd_readdir_empty(fs_node_t *node, uint32_t index) {
    (void)node;
    (void)index;
    return NULL;
}

static struct dirent *initrd_readdir(fs_node_t *node, uint32_t index) {
    if (!(node->flags & FS_DIRECTORY)) return NULL;

    uint32_t address = initrd_location;
    uint32_t i = 0;
    while (1) {
        struct tar_header *header = (struct tar_header *)address;
        if (header->filename[0] == '\0') break;

        if (i == index) {
            strcpy(dirent.name, normalize_initrd_name(header->filename));
            dirent.ino = i;
            return &dirent;
        }

        uint32_t filesize = get_size(header->size);
        address += 512 + ((filesize + 511) & ~511);
        i++;
    }

    // Adiciona entrada virtual 'house' para o ponto de montagem se estivermos na raiz
    if (index == i && strcmp(node->name, "initrd") == 0) {
        strcpy(dirent.name, "house");
        dirent.ino = i;
        return &dirent;
    }

    return NULL;
}

static fs_node_t *initrd_finddir(fs_node_t *node, const char *name) {
    if (!(node->flags & FS_DIRECTORY)) return NULL;

    // Caso especial para a pasta virtual house
    if (strcmp(name, "house") == 0 && strcmp(node->name, "initrd") == 0) {
        fs_node_t *house = (fs_node_t *)kmalloc(sizeof(fs_node_t));
        memset(house, 0, sizeof(fs_node_t));
        strcpy(house->name, "house");
        house->flags = FS_DIRECTORY;
        house->readdir = initrd_readdir;
        house->finddir = initrd_finddir;
        return house;
    }
    
    if (strcmp(name, "localhost") == 0 && strcmp(node->name, "house") == 0) {
        fs_node_t *localhost = (fs_node_t *)kmalloc(sizeof(fs_node_t));
        memset(localhost, 0, sizeof(fs_node_t));
        strcpy(localhost->name, "localhost");
        localhost->flags = FS_DIRECTORY;
        // Em LIVECD mode: readdir vazio (initrd_readdir_empty).
        // Em modo persistente, ensure_disk_ready() faz vfs_mount()
        // do SDFS sobre este node, substituindo os callbacks.
        localhost->readdir = initrd_readdir_empty;
        localhost->finddir = initrd_finddir;
        return localhost;
    }

    uint32_t size = 0;
    void *data = initrd_get_file(name, &size);
    if (data) {
        fs_node_t *res = (fs_node_t *)kmalloc(sizeof(fs_node_t));
        memset(res, 0, sizeof(fs_node_t));
        strcpy(res->name, name);
        res->length = size;
        res->flags = FS_FILE;
        res->read = initrd_read;
        return res;
    }

    return NULL;
}

fs_node_t* init_initrd(uint32_t location, uint32_t size) {
    void *copy = kmalloc(size);
    memcpy(copy, (const void *)location, size);
    initrd_location = (uint32_t)copy;
    initrd_size = size;
    serial_print("initrd: copiado para heap do kernel\n");

    fs_node_t* root = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    memset(root, 0, sizeof(fs_node_t));
    strcpy(root->name, "initrd");
    root->flags = FS_DIRECTORY;
    root->readdir = initrd_readdir;
    root->finddir = initrd_finddir;
    fs_root = root; // Define como raiz global
    return root;
}

/* Retorna o nome do n-ésimo arquivo no disco */
char* initrd_list_files(int index) {
    uint32_t address = initrd_location;
    int current = 0;
    while (1) {
        struct tar_header* header = (struct tar_header*)address;
        if (header->filename[0] == '\0') break;

        if (current == index) return header->filename;

        uint32_t filesize = get_size(header->size);
        address += 512 + ((filesize + 511) & ~511);
        current++;
    }
    return NULL;
}

void* initrd_get_file(const char* name, uint32_t* size) {
    uint32_t address = initrd_location;
    serial_print("initrd: procurando ");
    serial_print(name);
    serial_print("\n");
    while (1) {
        struct tar_header* header = (struct tar_header*)address;
        if (header->filename[0] == '\0') break;

        uint32_t filesize = get_size(header->size);
        serial_print("initrd: achou entrada ");
        serial_print(header->filename);
        serial_print("\n");
        if (initrd_name_equals(header->filename, name)) {
            *size = filesize;
            serial_print("initrd: match encontrado\n");
            return (void*)(address + 512);
        }

        address += 512 + ((filesize + 511) & ~511);
    }
    serial_print("initrd: nenhum match encontrado\n");
    return NULL;
}

/*
 * initrd_copy_to_sdfs: Copia todos os arquivos do initrd (tar) para o SDFS.
 *
 * Esta função é chamada apenas no primeiro boot (quando /.system_installed
 * não existe no disco). Ela faz duas passadas no arquivo tar:
 *
 * Passada 1: Conta quantos arquivos existem (para calcular progresso %)
 * Passada 2: Para cada arquivo, cria no SDFS via sdfs_create_file() e
 *            escreve o conteúdo via sdfs_write_file(). Chama o callback
 *            cb(pct, nome) após cada cópia para atualizar a animação.
 *
 * O callback é do tipo copy_progress_cb (definido em initrd.h):
 *   void (*cb)(int percent, const char *filename);
 * Pode ser NULL se não quiser progresso (ex: comando sysupdate).
 *
 * Diretórios (typeflag '5') são ignorados — o SDFS não precisa de
 * criação explícita de diretórios; ele resolve caminhos como "/foo/bar"
 * navegando a partir da raiz.
 *
 * A normalização de nomes (normalize_initrd_name) remove "./" prefixado
 * e "/" inicial, já que no initrd os paths vêm como "./foo/bar" ou
 * "/foo/bar" e no SDFS usamos "/foo/bar".
 */
void initrd_copy_to_sdfs(copy_progress_cb cb) {
    uint32_t address;
    int total_files = 0;
    int copied = 0;

    serial_print("initrd: copying system files to SDFS...\n");

    // --- Primeira passada: contar total de arquivos ---
    // Itera o tar do início ao fim, somando 1 para cada entrada que
    // não seja diretório. O total é usado para calcular a porcentagem
    // do progresso na segunda passada.
    address = initrd_location;
    while (1) {
        struct tar_header *header = (struct tar_header *)address;
        if (header->filename[0] == '\0') break;

        uint32_t filesize = get_size(header->size);

        if (header->typeflag != '5') {
            const char *name = normalize_initrd_name(header->filename);
            if (name && name[0] != '\0') total_files++;
        }

        address += 512 + ((filesize + 511) & ~511);
    }

    // --- Segunda passada: copiar arquivos com callback de progresso ---
    // Reitera o tar. Para cada arquivo:
    //   1. Cria o arquivo no SDFS (sdfs_create_file)
    //   2. Se tiver conteúdo > 0, escreve os bytes (sdfs_write_file)
    //   3. Chama o callback de progresso (se fornecido)
    address = initrd_location;
    while (1) {
        struct tar_header *header = (struct tar_header *)address;
        if (header->filename[0] == '\0') break;

        uint32_t filesize = get_size(header->size);
        void *data = (void *)(address + 512);

        // Pula diretorios (typeflag '5') — SDFS os cria implicitamente
        // na navegação de caminhos
        if (header->typeflag == '5') {
            address += 512 + ((filesize + 511) & ~511);
            continue;
        }

        const char *name = normalize_initrd_name(header->filename);
        if (!name || name[0] == '\0') {
            address += 512 + ((filesize + 511) & ~511);
            continue;
        }

        // Monta caminho absoluto no SDFS (ex: "bin/lua" -> "/bin/lua")
        char sdfs_path[256];
        sdfs_path[0] = '/';
        strncpy(sdfs_path + 1, name, 254);
        sdfs_path[255] = '\0';

        serial_print("  copiando ");
        serial_print(sdfs_path);
        serial_print("\n");

        // Cria o arquivo (start block + entrada no diretório pai)
        if (sdfs_create_file(sdfs_path) == 0) {
            // Escreve o conteúdo binário no SDFS via block chain
            if (filesize > 0) {
                sdfs_write_file(sdfs_path, (uint8_t *)data, filesize);
            }
            copied++;
        }

        // Callback de progresso (usado pela boot animation)
        if (cb && total_files > 0) {
            int pct = (copied * 100) / total_files;
            cb(pct, sdfs_path);
        }

        address += 512 + ((filesize + 511) & ~511);
    }

    // 100% — callback final para completar a barra
    if (cb) cb(100, NULL);

    serial_print("initrd: copy complete\n");
}
