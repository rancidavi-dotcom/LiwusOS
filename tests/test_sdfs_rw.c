#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"
#include "string.h"
#include "kheap.h"

int test_sdfs_write_read_1block(void) {
    const char *data = "hello LiwusOS";
    uint32_t len = 13;

    uint32_t written = sdfs_write_file("/rw1.txt", (uint8_t *)data, len);
    ASSERT(written == len, "wrote 13 bytes");

    uint32_t size = 0;
    void *result = sdfs_read_file("/rw1.txt", &size);
    ASSERT(result != NULL, "read not null");
    ASSERT(size == len, "size matches");
    ASSERT(memcmp(result, data, len) == 0, "data matches");

    sdfs_delete("/rw1.txt");
    PASS("test_sdfs_write_read_1block");
}

int test_sdfs_write_read_exact_block(void) {
    char buf[4092];
    for (int i = 0; i < 4092; i++) buf[i] = (char)(i & 0xFF);

    uint32_t written = sdfs_write_file("/exact.txt", (uint8_t *)buf, 4092);
    ASSERT(written == 4092, "wrote 4092 bytes");

    uint32_t size = 0;
    void *result = sdfs_read_file("/exact.txt", &size);
    ASSERT(result != NULL, "read not null");
    ASSERT(size == 4092, "size is 4092");
    ASSERT(memcmp(result, buf, 4092) == 0, "data matches");

    sdfs_delete("/exact.txt");
    PASS("test_sdfs_write_read_exact_block");
}

int test_sdfs_write_read_multiblock(void) {
    uint32_t total = 20000;
    uint8_t *pattern = (uint8_t *)kmalloc(total);
    if (!pattern) FAIL("test_sdfs_write_read_multiblock", "kmalloc failed");
    for (uint32_t i = 0; i < total; i++) pattern[i] = (uint8_t)(i * 7 + 3);

    uint32_t written = sdfs_write_file("/multi.txt", pattern, total);
    ASSERT(written == total, "wrote 20000 bytes");

    uint32_t size = 0;
    void *result = sdfs_read_file("/multi.txt", &size);
    ASSERT(result != NULL, "read not null");
    ASSERT(size == total, "size is 20000");
    ASSERT(memcmp(result, pattern, total) == 0, "multiblock data matches");

    kfree(pattern);
    sdfs_delete("/multi.txt");
    PASS("test_sdfs_write_read_multiblock");
}

int test_sdfs_overwrite(void) {
    sdfs_write_file("/ov.txt", (uint8_t *)"old", 3);

    uint32_t written = sdfs_write_file("/ov.txt", (uint8_t *)"new_data_here", 13);
    ASSERT(written == 13, "overwrite wrote 13 bytes");

    uint32_t size = 0;
    void *result = sdfs_read_file("/ov.txt", &size);
    ASSERT(result != NULL, "read not null");
    ASSERT(size == 13, "size is 13 after overwrite");
    ASSERT(memcmp(result, "new_data_here", 13) == 0, "new data matches");

    sdfs_delete("/ov.txt");
    PASS("test_sdfs_overwrite");
}

int test_sdfs_empty_file(void) {
    sdfs_write_file("/empty.txt", (uint8_t *)"", 0);

    uint32_t size = 0;
    void *result = sdfs_read_file("/empty.txt", &size);
    ASSERT(result != NULL, "empty file readable");
    ASSERT(size == 0, "empty file size 0");

    sdfs_delete("/empty.txt");
    PASS("test_sdfs_empty_file");
}

#endif
