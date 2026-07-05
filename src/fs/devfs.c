#include "vfs.h"
#include "kheap.h"
#include "string.h"
#include "serial.h"

static uint32_t devfs_read_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    // TODO: Implementar leitura da serial se necessário
    return 0;
}

static uint32_t devfs_write_serial(fs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    extern void write_serial(char a);
    for(uint32_t i = 0; i < size; i++) {
        write_serial(buffer[i]);
    }
    return size;
}

fs_node_t* devfs_init() {
    fs_node_t* root = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    memset(root, 0, sizeof(fs_node_t));
    strcpy(root->name, "dev");
    root->flags = FS_DIRECTORY;

    // Criar /dev/serial
    fs_node_t* serial_node = (fs_node_t*)kmalloc(sizeof(fs_node_t));
    memset(serial_node, 0, sizeof(fs_node_t));
    strcpy(serial_node->name, "serial");
    serial_node->flags = FS_FILE;
    serial_node->write = devfs_write_serial;
    serial_node->read = devfs_read_serial;

    return root;
}
