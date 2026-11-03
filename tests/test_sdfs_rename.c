#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"
#include "string.h"
#include "kheap.h"

static uint32_t find_directory_child(uint32_t first_block, const char *wanted) {
    uint32_t block = first_block;
    while (block) {
        uint8_t raw[SDFS_BLOCK_SIZE];
        if (sdfs_read_raw_block(block, raw) != 0) return 0;
        sdfs_dir_block_t *dir = (sdfs_dir_block_t *)raw;
        uint8_t *pos = dir->data;
        uint32_t remaining = sizeof(dir->data);
        for (uint32_t i = 0; i < dir->entry_count; i++) {
            if (remaining < 2) return 0;
            uint8_t type = pos[0], name_len = pos[1];
            if (type == SDFS_TYPE_FREE) {
                pos += 2;
                remaining -= 2;
                continue;
            }
            uint32_t size = 2u + name_len + 24u;
            if (size > remaining) return 0;
            if (type != SDFS_TYPE_DEL &&
                strlen(wanted) == name_len &&
                memcmp(pos + 2, wanted, name_len) == 0) {
                uint32_t child;
                memcpy(&child, pos + 2 + name_len, sizeof(child));
                return child;
            }
            pos += size;
            remaining -= size;
        }
        block = dir->next_block;
    }
    return 0;
}

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

int test_sdfs_rename_journal_atomic(void) {
    ASSERT(sdfs_create_dir("/rename-src") == 0, "create source directory");
    ASSERT(sdfs_create_dir("/rename-dst") == 0, "create destination directory");
    ASSERT(sdfs_write_file("/rename-src/item", (uint8_t *)"atomic", 6) == 6,
                          "create rename source file");

    sdfs_super_t *super = (sdfs_super_t *)kmalloc(SDFS_BLOCK_SIZE);
    ASSERT(super != NULL, "allocate superblock scratch");
    ASSERT(sdfs_read_raw_block(0, (uint8_t *)super) == 0,
           "read journal location");
    uint32_t root = super->root_dir_block;
    uint32_t source_dir = find_directory_child(root, "rename-src");
    uint32_t dest_dir = find_directory_child(root, "rename-dst");
    ASSERT(source_dir != 0 && dest_dir != 0 && source_dir != dest_dir,
           "resolve distinct directory blocks");

    /* Fail staging the second metadata snapshot, before the journal commit
     * record. The old namespace must remain intact after remount. */
    sdfs_test_fail_next_write(super->journal_start + 4);
    ASSERT(sdfs_rename("/rename-src/item", "/rename-dst/item") < 0,
           "pre-commit journal staging failure is reported");
    ASSERT(sdfs_journal_dirty() == 0,
           "failed staging leaves no committed journal");
    ASSERT(sdfs_mount(0, 0, 0) != NULL,
           "pre-commit failure remounts without replay");
    int is_dir = 0;
    uint32_t size = 0;
    ASSERT(sdfs_path_info("/rename-src/item", &is_dir, &size) == 0 && size == 6,
           "pre-commit failure retains old name and file");
    ASSERT(sdfs_path_info("/rename-dst/item", &is_dir, &size) < 0,
           "pre-commit failure does not publish destination");

    /* Fail applying the first target after the commit record. Recovery must
     * replay both snapshots, never a half-renamed directory pair. */
    sdfs_test_fail_next_write(source_dir);
    ASSERT(sdfs_rename("/rename-src/item", "/rename-dst/item") < 0,
           "post-commit target failure is reported");
    ASSERT(sdfs_journal_dirty() == 1,
           "post-commit failure retains replayable transaction");
    ASSERT(sdfs_mount(0, 0, 0) != NULL,
           "post-commit failure remount replays transaction");
    ASSERT(sdfs_journal_dirty() == 0,
           "successful replay cleans committed rename");
    ASSERT(sdfs_path_info("/rename-src/item", &is_dir, &size) < 0,
           "replay removes source name");
    ASSERT(sdfs_path_info("/rename-dst/item", &is_dir, &size) == 0 && size == 6,
           "replay publishes destination name");
    uint32_t data_size = 0;
    void *data = sdfs_read_file("/rename-dst/item", &data_size);
    ASSERT(data != NULL && data_size == 6 && memcmp(data, "atomic", 6) == 0,
           "replayed rename retains exact file contents");
    kfree(data);
    kfree(super);
    ASSERT(sdfs_delete("/rename-dst/item") == 0, "remove renamed test file");
    ASSERT(sdfs_delete("/rename-src") == 0, "remove empty source directory");
    ASSERT(sdfs_delete("/rename-dst") == 0, "remove empty destination directory");
    PASS("test_sdfs_rename_journal_atomic");
}

#endif
