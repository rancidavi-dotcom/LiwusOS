#include "initrd.h"
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
    for (int j = 0; j < 11; j++) {
        if (in[j] < '0' || in[j] > '7') continue;
        size = size * 8 + (in[j] - '0');
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
        localhost->readdir = initrd_readdir;
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
