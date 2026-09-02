#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"
#include "string.h"

int test_sdfs_rename_file(void) {
    sdfs_write_file("/rename1.txt", (uint8_t *)"data", 4);

    int ret = sdfs_rename("/rename1.txt", "/rename2.txt");
    ASSERT(ret == 0, "rename returned 0");

    uint32_t size = 0;
    void *result = sdfs_read_file("/rename2.txt", &size);
    ASSERT(result != NULL, "renamed file readable");
    ASSERT(size == 4, "size preserved");
    ASSERT(memcmp(result, "data", 4) == 0, "data preserved");

    int is_dir = 0;
    ret = sdfs_path_info("/rename1.txt", &is_dir, &size);
    ASSERT(ret == -1, "old name gone");

    sdfs_delete("/rename2.txt");
    PASS("test_sdfs_rename_file");
}

int test_sdfs_rename_dir(void) {
    sdfs_create_dir("/olddir");
    sdfs_create_file("/olddir/f.txt");

    int ret = sdfs_rename("/olddir", "/newdir");
    ASSERT(ret == 0, "rename dir");

    int is_dir = 0;
    uint32_t size = 0;
    ret = sdfs_path_info("/newdir", &is_dir, &size);
    ASSERT(ret == 0, "new dir exists");
    ASSERT(is_dir == 1, "is directory");

    ret = sdfs_path_info("/newdir/f.txt", &is_dir, &size);
    ASSERT(ret == 0, "file in renamed dir exists");

    sdfs_delete("/newdir/f.txt");
    sdfs_delete("/newdir");
    PASS("test_sdfs_rename_dir");
}

int test_sdfs_rename_not_found(void) {
    int ret = sdfs_rename("/ghost.txt", "/also_ghost.txt");
    ASSERT(ret == -1, "rename nonexistent fails");
    PASS("test_sdfs_rename_not_found");
}

int test_sdfs_rename_overwrite(void) {
    sdfs_write_file("/ow_a.txt", (uint8_t *)"aaa", 3);
    sdfs_write_file("/ow_b.txt", (uint8_t *)"bbb", 3);

    int ret = sdfs_rename("/ow_a.txt", "/ow_b.txt");
    ASSERT(ret == -1, "rename over existing fails");

    sdfs_delete("/ow_a.txt");
    sdfs_delete("/ow_b.txt");
    PASS("test_sdfs_rename_overwrite");
}

#endif
