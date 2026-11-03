#ifdef KERNEL_TEST
#include "sdfs.h"
#include "framework.h"
#include "string.h"
#include "task.h"
#include "kheap.h"

#define SDFS_CONCURRENT_WORKERS 4
#define SDFS_CONCURRENT_FILES 8
static volatile uint32_t sdfs_concurrent_done;
static volatile uint32_t sdfs_concurrent_errors;

static void sdfs_concurrent_worker(int worker) {
    uint8_t payload[513];
    for (uint32_t n = 0; n < SDFS_CONCURRENT_FILES; n++) {
        char path[64];
        char number[12];
        strcpy(path, "/sdfs-race-");
        itoa(worker, number, 10);
        strcat(path, number);
        strcat(path, "-");
        itoa((int)n, number, 10);
        strcat(path, number);
        for (uint32_t i = 0; i < sizeof(payload); i++)
            payload[i] = (uint8_t)(worker * 37 + n * 11 + i);

        if (sdfs_create_file(path) != 0 ||
            sdfs_write_file(path, payload, sizeof(payload)) != sizeof(payload)) {
            __sync_fetch_and_add(&sdfs_concurrent_errors, 1);
            continue;
        }
        uint32_t size = 0;
        uint8_t *readback = (uint8_t *)sdfs_read_file(path, &size);
        if (!readback || size != sizeof(payload) ||
            memcmp(readback, payload, sizeof(payload)) != 0)
            __sync_fetch_and_add(&sdfs_concurrent_errors, 1);
        if (readback) kfree(readback);
        if (sdfs_delete(path) != 0)
            __sync_fetch_and_add(&sdfs_concurrent_errors, 1);
    }
    __sync_fetch_and_add(&sdfs_concurrent_done, 1);
    for (;;) task_sleep_ms(1000);
}

static void sdfs_concurrent_worker0(void) { sdfs_concurrent_worker(0); }
static void sdfs_concurrent_worker1(void) { sdfs_concurrent_worker(1); }
static void sdfs_concurrent_worker2(void) { sdfs_concurrent_worker(2); }
static void sdfs_concurrent_worker3(void) { sdfs_concurrent_worker(3); }

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
    ASSERT(count == 3, "3 entries in /listdir before delete");
    sdfs_delete("/listdir/a.txt");

    /* A tombstone must occupy the full serialized entry, not just its two
     * byte header. Reuse the directory afterward and verify enumeration. */
    ASSERT(sdfs_create_file("/listdir/replacement.txt") == 0,
           "create after directory deletion");
    count = 0;
    int saw_b = 0, saw_sub = 0, saw_replacement = 0, saw_deleted = 0;
    for (int i = 0; i < 10; i++) {
        if (sdfs_list_dir_entry("/listdir", i, name, &is_dir, &size) != 0) break;
        count++;
        if (strcmp(name, "b.txt") == 0) saw_b++;
        else if (strcmp(name, "sub") == 0) saw_sub++;
        else if (strcmp(name, "replacement.txt") == 0) saw_replacement++;
        else if (strcmp(name, "a.txt") == 0) saw_deleted++;
    }
    ASSERT(count == 3, "3 entries after delete and create");
    ASSERT(saw_b == 1 && saw_sub == 1 && saw_replacement == 1 && saw_deleted == 0,
           "enumeration skips deleted record and lists live entries once");

    sdfs_delete("/listdir/b.txt");
    sdfs_delete("/listdir/sub");
    sdfs_delete("/listdir/replacement.txt");
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

int test_sdfs_concurrent_mutations(void) {
    void (*workers[SDFS_CONCURRENT_WORKERS])(void) = {
        sdfs_concurrent_worker0, sdfs_concurrent_worker1,
        sdfs_concurrent_worker2, sdfs_concurrent_worker3
    };
    sdfs_concurrent_done = 0;
    sdfs_concurrent_errors = 0;
    for (int i = 0; i < SDFS_CONCURRENT_WORKERS; i++)
        ASSERT(create_task_named(workers[i], "sdfs-mutator") >= 0,
               "create concurrent SDFS worker");
    for (uint32_t tick = 0; tick < 1000 &&
         sdfs_concurrent_done != SDFS_CONCURRENT_WORKERS; tick++)
        task_sleep_ms(10);
    ASSERT(sdfs_concurrent_done == SDFS_CONCURRENT_WORKERS,
           "all SDFS mutation workers complete within deadline");
    ASSERT(sdfs_concurrent_errors == 0,
           "concurrent create/write/read/delete preserves files");
    PASS("test_sdfs_concurrent_mutations");
}

#endif
