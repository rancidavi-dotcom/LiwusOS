#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"

int test_sdfs_delete_file(void) {
    sdfs_create_dir("/sandbox");
    int ret = sdfs_create_file("/sandbox/delme.txt");
    ASSERT(ret == 0, "create file succeeds");
    ret = sdfs_delete("/sandbox/delme.txt");
    ASSERT(ret == 0, "delete returned 0");

    int is_dir = 0;
    uint32_t size = 0;
    ret = sdfs_path_info("/sandbox/delme.txt", &is_dir, &size);
    ASSERT(ret == -1, "file no longer exists");
    sdfs_delete("/sandbox");
    PASS("test_sdfs_delete_file");
}

int test_sdfs_delete_nonexistent(void) {
    int ret = sdfs_delete("/nope.txt");
    ASSERT(ret == -1, "delete nonexistent fails");
    PASS("test_sdfs_delete_nonexistent");
}

int test_sdfs_delete_root(void) {
    int ret = sdfs_delete("/");
    ASSERT(ret == -1, "delete root blocked");
    PASS("test_sdfs_delete_root");
}

int test_sdfs_delete_file_with_data(void) {
    sdfs_write_file("/deldata.txt", (uint8_t *)"important", 9);
    int ret = sdfs_delete("/deldata.txt");
    ASSERT(ret == 0, "delete file with data");

    uint32_t size = 0;
    void *result = sdfs_read_file("/deldata.txt", &size);
    ASSERT(result == NULL, "file gone after delete");
    PASS("test_sdfs_delete_file_with_data");
}

int test_sdfs_delete_dir_empty(void) {
    sdfs_create_dir("/deldir");
    int ret = sdfs_delete("/deldir");
    ASSERT(ret == 0, "delete empty dir");

    int is_dir = 0;
    uint32_t size = 0;
    ret = sdfs_path_info("/deldir", &is_dir, &size);
    ASSERT(ret == -1, "dir gone");
    PASS("test_sdfs_delete_dir_empty");
}

#endif
