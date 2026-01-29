#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"

int test_sdfs_dir_create(void) {
    int ret = sdfs_create_dir("/mydir");
    ASSERT(ret == 0, "create_dir returned 0");

    int is_dir = 0;
    uint32_t size = 0;
    ret = sdfs_path_info("/mydir", &is_dir, &size);
    ASSERT(ret == 0, "path_info /mydir");
    ASSERT(is_dir == 1, "is directory");

    sdfs_delete("/mydir");
    PASS("test_sdfs_dir_create");
}

int test_sdfs_dir_list(void) {
    sdfs_create_dir("/listdir");
    sdfs_create_file("/listdir/a.txt");
    sdfs_create_file("/listdir/b.txt");
    sdfs_create_dir("/listdir/sub");

    char name[256];
    int is_dir = 0;
    uint32_t size = 0;
    int count = 0;

    for (int i = 0; i < 10; i++) {
        if (sdfs_list_dir_entry("/listdir", i, name, &is_dir, &size) != 0) break;
        count++;
    }
    ASSERT(count == 3, "3 entries in /listdir");

    sdfs_delete("/listdir/a.txt");
    sdfs_delete("/listdir/b.txt");
    sdfs_delete("/listdir/sub");
    sdfs_delete("/listdir");
    PASS("test_sdfs_dir_list");
}

int test_sdfs_dir_not_empty(void) {
    sdfs_create_dir("/notempty");
    sdfs_create_file("/notempty/keep.txt");

    int ret = sdfs_delete("/notempty");
    ASSERT(ret == -1, "delete non-empty dir fails");

    sdfs_delete("/notempty/keep.txt");
    sdfs_delete("/notempty");
    PASS("test_sdfs_dir_not_empty");
}

int test_sdfs_nested_dirs(void) {
    sdfs_create_dir("/a");
    sdfs_create_dir("/a/b");
    sdfs_create_dir("/a/b/c");
    sdfs_create_file("/a/b/c/file.txt");

    int is_dir = 0;
    uint32_t size = 0;
    int ret = sdfs_path_info("/a/b/c/file.txt", &is_dir, &size);
    ASSERT(ret == 0, "deep nested path resolves");
    ASSERT(is_dir == 0, "is file");

    sdfs_delete("/a/b/c/file.txt");
    sdfs_delete("/a/b/c");
    sdfs_delete("/a/b");
    sdfs_delete("/a");
    PASS("test_sdfs_nested_dirs");
}

#endif
