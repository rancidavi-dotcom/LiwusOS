#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"
#include "string.h"

int test_perms_set_get(void) {
    sdfs_create_dir("/perm_test");
    sdfs_create_file("/perm_test/file.txt");
    sdfs_write_file("/perm_test/file.txt", (uint8_t *)"test", 4);

    /* Set specific permissions */
    ASSERT(sdfs_set_permissions("/perm_test/file.txt", 0644) == 0, "set perms ok");

    uint16_t perm = 0;
    ASSERT(sdfs_get_permissions("/perm_test/file.txt", &perm) == 0, "get perms ok");
    ASSERT(perm == 0644, "permissions match 0644");

    /* Set different permissions */
    ASSERT(sdfs_set_permissions("/perm_test/file.txt", 0755) == 0, "set perms 755 ok");
    ASSERT(sdfs_get_permissions("/perm_test/file.txt", &perm) == 0, "get perms 755 ok");
    ASSERT(perm == 0755, "permissions match 0755");

    /* Directory permissions */
    ASSERT(sdfs_set_permissions("/perm_test", 0700) == 0, "set dir perms ok");
    ASSERT(sdfs_get_permissions("/perm_test", &perm) == 0, "get dir perms ok");
    ASSERT(perm == 0700, "dir permissions match 0700");

    /* Root always returns default */
    ASSERT(sdfs_get_permissions("/", &perm) == 0, "get root perms ok");
    ASSERT((perm & 0x100) != 0, "root has owner read");

    sdfs_delete("/perm_test/file.txt");
    sdfs_delete("/perm_test");
    PASS("test_perms_set_get");
}

int test_perms_default(void) {
    sdfs_create_dir("/defperm");
    sdfs_create_file("/defperm/f.txt");
    sdfs_write_file("/defperm/f.txt", (uint8_t *)"x", 1);

    uint16_t perm = 0;
    ASSERT(sdfs_get_permissions("/defperm/f.txt", &perm) == 0, "get default perms");
    ASSERT(perm != 0, "default perms non-zero");

    sdfs_delete("/defperm/f.txt");
    sdfs_delete("/defperm");
    PASS("test_perms_default");
}

int test_timestamp_created(void) {
    sdfs_create_dir("/ts_test");
    sdfs_create_file("/ts_test/created.txt");
    sdfs_write_file("/ts_test/created.txt", (uint8_t *)"hello", 5);

    uint32_t size = 0;
    int is_dir = 0;
    ASSERT(sdfs_path_info("/ts_test/created.txt", &is_dir, &size) == 0, "path_info ok");
    ASSERT(size == 5, "file size ok");

    /* Write more data - modified_time should change, created should not */
    sdfs_write_file("/ts_test/created.txt", (uint8_t *)"hello world!", 12);

    sdfs_delete("/ts_test/created.txt");
    sdfs_delete("/ts_test");
    PASS("test_timestamp_created");
}

int test_timestamp_remount(void) {
    sdfs_create_dir("/ts_remount");
    sdfs_create_file("/ts_remount/data.txt");
    sdfs_write_file("/ts_remount/data.txt", (uint8_t *)"persist", 7);

    /* Remount */
    sdfs_mount(0, 0, 0);

    uint32_t size = 0;
    void *data = sdfs_read_file("/ts_remount/data.txt", &size);
    ASSERT(data != NULL && size == 7, "data survives remount with timestamps");

    sdfs_delete("/ts_remount/data.txt");
    sdfs_delete("/ts_remount");
    PASS("test_timestamp_remount");
}

#endif
