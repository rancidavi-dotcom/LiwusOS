#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"
#include "string.h"

int test_sdfs_persist_remount(void) {
    sdfs_write_file("/persist.txt", (uint8_t *)"survive", 7);

    /* Simulate remount: unmount and mount again using same ramdisk.
     * The ramdisk data survives because it's in memory; we just
     * re-read the superblock and bitmap. */
    sdfs_mount(0, 0, 0);

    uint32_t size = 0;
    void *result = sdfs_read_file("/persist.txt", &size);
    ASSERT(result != NULL, "file survives remount");
    ASSERT(size == 7, "size preserved");
    ASSERT(memcmp(result, "survive", 7) == 0, "data preserved");

    sdfs_delete("/persist.txt");
    PASS("test_sdfs_persist_remount");
}

int test_sdfs_persist_dir_structure(void) {
    sdfs_create_dir("/pdir");
    sdfs_create_file("/pdir/a.txt");
    sdfs_write_file("/pdir/a.txt", (uint8_t *)"aaa", 3);
    sdfs_create_file("/pdir/b.txt");
    sdfs_write_file("/pdir/b.txt", (uint8_t *)"bbb", 3);

    /* Remount */
    sdfs_mount(0, 0, 0);

    int is_dir = 0;
    uint32_t size = 0;
    ASSERT(sdfs_path_info("/pdir", &is_dir, &size) == 0, "/pdir exists");
    ASSERT(is_dir == 1, "/pdir is dir");

    uint32_t sz = 0;
    void *r1 = sdfs_read_file("/pdir/a.txt", &sz);
    ASSERT(r1 != NULL && sz == 3, "a.txt survives");

    void *r2 = sdfs_read_file("/pdir/b.txt", &sz);
    ASSERT(r2 != NULL && sz == 3, "b.txt survives");

    sdfs_delete("/pdir/a.txt");
    sdfs_delete("/pdir/b.txt");
    sdfs_delete("/pdir");
    PASS("test_sdfs_persist_dir_structure");
}

#endif
