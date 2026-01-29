#include "initrd.h"
#include <stddef.h>
#include "string.h"

static uint32_t initrd_location;

static uint32_t get_size(const char *in) {
    uint32_t size = 0;
    int count = 1;
    for (int j = 10; j >= 0; j--) {
        size += (in[j] - '0') * count;
        count *= 8;
    }
    return size;
}

void init_initrd(uint32_t location) {
    initrd_location = location;
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
        address += ((filesize / 512) + 1) * 512;
        if (filesize % 512) address += 512;
        current++;
    }
    return NULL;
}

void* initrd_get_file(const char* name, uint32_t* size) {
    uint32_t address = initrd_location;
    while (1) {
        struct tar_header* header = (struct tar_header*)address;
        if (header->filename[0] == '\0') break;

        uint32_t filesize = get_size(header->size);
        if (strcmp(header->filename, name) == 0) {
            *size = filesize;
            return (void*)(address + 512);
        }

        address += ((filesize / 512) + 1) * 512;
        if (filesize % 512) address += 512;
    }
    return NULL;
}