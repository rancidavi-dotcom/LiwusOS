#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"

int test_sdfs_create_empty(void) {
    sdfs_create_dir("/sandbox");
    int ret = sdfs_create_file("/sandbox/empty.txt");
    ASSERT(ret == 0, "create_file returned error");

    int is_dir = 0;
    uint32_t size = 0;
    ret = sdfs_path_info("/sandbox/empty.txt", &is_dir, &size);
    ASSERT(ret == 0, "path_info succeeded");
    ASSERT(is_dir == 0, "is not directory");
    ASSERT(size == 0, "size is 0");

    sdfs_delete("/sandbox/empty.txt");
    sdfs_delete("/sandbox");
    PASS("test_sdfs_create_empty");
}

int test_sdfs_create_duplicate(void) {
    sdfs_create_dir("/sandbox");
    int ret = sdfs_create_file("/sandbox/dup.txt");
    ASSERT(ret == 0, "first create succeeds");
    ret = sdfs_create_file("/sandbox/dup.txt");
    ASSERT(ret == -1, "duplicate create returns error");
    sdfs_delete("/sandbox/dup.txt");
    sdfs_delete("/sandbox");
    PASS("test_sdfs_create_duplicate");
}

int test_sdfs_create_root_blocked(void) {
    int ret = sdfs_create_file("/");
    ASSERT(ret == -1, "create at root blocked");
    PASS("test_sdfs_create_root_blocked");
}

int test_sdfs_create_nested(void) {
    int ret = sdfs_create_dir("/ndir");
    ASSERT(ret == 0, "create /ndir");
    ret = sdfs_create_file("/ndir/file.txt");
    ASSERT(ret == 0, "create /ndir/file.txt");

    int is_dir = 0;
    uint32_t size = 0;
    ret = sdfs_path_info("/ndir/file.txt", &is_dir, &size);
    ASSERT(ret == 0, "path_info /ndir/file.txt");
    ASSERT(is_dir == 0, "is file");
    ASSERT(size == 0, "empty file");

    sdfs_delete("/ndir/file.txt");
    sdfs_delete("/ndir");
    PASS("test_sdfs_create_nested");
}

#endif
