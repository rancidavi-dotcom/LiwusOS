#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"
#include "kheap.h"

int test_sdfs_diskfull(void) {
    /* Fill the disk by creating files until it fails */
    char name[32];
    int count = 0;
    uint8_t *block = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!block) FAIL("test_sdfs_diskfull", "kmalloc failed");
    for (int i = 0; i < SDFS_BLOCK_SIZE; i++) block[i] = (uint8_t)i;

    for (int i = 0; i < 10000; i++) {
        /* Build filename /fill_NNN */
        name[0] = '/';
        name[1] = 'f';
        name[2] = 'i';
        name[3] = 'l';
        name[4] = 'l';
        name[5] = '_';
        int n = i;
        int pos = 6;
        if (n >= 1000) { name[pos++] = '0' + (n / 1000) % 10; }
        if (n >= 100)  { name[pos++] = '0' + (n / 100) % 10; }
        if (n >= 10)   { name[pos++] = '0' + (n / 10) % 10; }
        name[pos++] = '0' + n % 10;
        name[pos++] = '\0';

        uint32_t written = sdfs_write_file(name, block, SDFS_BLOCK_SIZE);
        if (written == 0) break;
        count++;
    }

    ASSERT(count > 0, "at least one file written");
    ASSERT(count < 10000, "disk filled before 10000 files");

    kfree(block);

    /* Clean up */
    for (int i = 0; i < count; i++) {
        name[0] = '/';
        name[1] = 'f';
        name[2] = 'i';
        name[3] = 'l';
        name[4] = 'l';
        name[5] = '_';
        int n = i;
        int pos = 6;
        if (n >= 1000) { name[pos++] = '0' + (n / 1000) % 10; }
        if (n >= 100)  { name[pos++] = '0' + (n / 100) % 10; }
        if (n >= 10)   { name[pos++] = '0' + (n / 10) % 10; }
        name[pos++] = '0' + n % 10;
        name[pos++] = '\0';
        sdfs_delete(name);
    }

    PASS("test_sdfs_diskfull");
}

#endif
