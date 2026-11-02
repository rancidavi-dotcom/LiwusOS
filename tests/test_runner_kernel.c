#ifdef KERNEL_TEST
#define KERNEL_TEST_MAIN
#include "sdfs.h"
#include "framework.h"
#include "serial.h"
#include "string.h"
#include "vfs.h"

/* ---- Test declarations from separate files ---- */
int test_terminal_files(void);
int test_sdfs_create_empty(void);
int test_sdfs_create_duplicate(void);
int test_sdfs_create_root_blocked(void);
int test_sdfs_create_nested(void);

int test_sdfs_write_read_1block(void);
int test_sdfs_write_read_exact_block(void);
int test_sdfs_write_read_multiblock(void);
int test_sdfs_overwrite(void);
int test_sdfs_empty_file(void);

int test_sdfs_dir_create(void);
int test_sdfs_dir_list(void);
int test_sdfs_dir_not_empty(void);
int test_sdfs_nested_dirs(void);

int test_sdfs_rename_file(void);
int test_sdfs_rename_dir(void);
int test_sdfs_rename_not_found(void);
int test_sdfs_rename_overwrite(void);

int test_sdfs_delete_file(void);
int test_sdfs_delete_nonexistent(void);
int test_sdfs_delete_root(void);
int test_sdfs_delete_file_with_data(void);
int test_sdfs_delete_dir_empty(void);

int test_sdfs_persist_remount(void);
int test_sdfs_persist_dir_structure(void);

int test_sdfs_diskfull(void);

/* ---- V2: CRC32 tests ---- */
int test_crc32_empty(void);
int test_crc32_known(void);
int test_crc32_consistency(void);

/* ---- V2: Journal tests ---- */
int test_journal_replay_basic(void);
int test_journal_clean_noop(void);
int test_journal_multiple_entries(void);

/* ---- V2: Permissions & timestamps ---- */
int test_perms_set_get(void);
int test_perms_default(void);
int test_timestamp_created(void);
int test_timestamp_remount(void);

/* ---- V2: Superblock & fsck ---- */
int test_superblock_crc(void);
int test_superblock_v2_fields(void);
int test_fsck_clean(void);
int test_v1_compat_read(void);

/* ---- Scheduler tests ---- */
int test_sched_priority(void);
int test_sched_sleep(void);
int test_sched_wait_queue(void);
int test_ring3_processes(void);
int test_vma_intervals(void);
int test_kernel_stack_guard(void);
int test_pmm_high_memory(void);
int test_pmm_contract(void);
int test_kheap_contract(void);
int test_process_thread_ownership(void);

/* ---- Global flag so kernel.c knows tests finished ---- */
volatile int test_runner_done = 0;

static int test_pass = 0;
static int test_fail = 0;

/* QEMU's isa-debug-exit device turns this into a reliable process result.
 * It is reached only in the KERNEL_TEST image, never in production builds. */
static void qemu_test_exit(int failed) {
    uint8_t code = failed ? 1 : 0;
    serial_print("[TEST HARNESS] QEMU debug-exit ");
    serial_print(failed ? "FAIL\n" : "PASS\n");
    asm volatile("outb %0, $0xf4" : : "a"(code));
}

static void run_test(int (*fn)(void), const char *name) {
    TEST_BEGIN(name);
    if (fn() == 0) {
        test_pass++;
    } else {
        test_fail++;
    }
}

/*
 * Main test runner task.
 * Called from kernel.c when /test_mode is detected in initrd.
 * Sets up an isolated ramdisk and runs all SDFS tests.
 */
void test_runner_task(void) {
    TEST_RUNNER_BEGIN;

    serial_print("[runner] Setting up isolated 4MB ramdisk...\n");
    sdfs_enable_ramdisk(4);

    serial_print("[runner] Formatting SDFS...\n");
    if (sdfs_format() != 0) {
        serial_print("[runner] FATAL: sdfs_format failed\n");
        TEST_RESULT(0, 1);
        TEST_RUNNER_END;
        test_runner_done = 1;
        qemu_test_exit(1);
        return;
    }

    serial_print("[runner] Mounting SDFS...\n");
    fs_node_t *root = sdfs_mount(0, 0, 0);
    if (!root) {
        serial_print("[runner] FATAL: sdfs_mount failed\n");
        TEST_RESULT(0, 1);
        TEST_RUNNER_END;
        test_runner_done = 1;
        qemu_test_exit(1);
        return;
    }

    vfs_mount("/", root);
    vfs_mount("/house/localhost", root);
    run_test(test_terminal_files, "terminal_files");

    serial_print("[runner] Running SDFS tests...\n\n");

    /* ---- V2: CRC32 tests ---- */
    run_test(test_crc32_known,       "crc32_known");
    run_test(test_crc32_consistency,  "crc32_consistency");

    /* ---- V2: Superblock & fsck ---- */
    run_test(test_superblock_crc,     "superblock_crc");
    run_test(test_superblock_v2_fields, "superblock_v2_fields");
    run_test(test_fsck_clean,         "fsck_clean");

    /* ---- V2: Journal tests ---- */
    run_test(test_journal_replay_basic,    "journal_replay_basic");
    run_test(test_journal_clean_noop,      "journal_clean_noop");
    run_test(test_journal_multiple_entries, "journal_multiple_entries");

    /* ---- Create tests ---- */
    run_test(test_sdfs_create_empty,      "sdfs_create_empty");
    run_test(test_sdfs_create_duplicate,   "sdfs_create_duplicate");
    run_test(test_sdfs_create_root_blocked,"sdfs_create_root_blocked");
    run_test(test_sdfs_create_nested,      "sdfs_create_nested");

    /* ---- Read/Write tests ---- */
    run_test(test_sdfs_write_read_1block,   "sdfs_write_read_1block");
    run_test(test_sdfs_write_read_exact_block, "sdfs_write_read_exact_block");
    run_test(test_sdfs_write_read_multiblock,  "sdfs_write_read_multiblock");
    run_test(test_sdfs_overwrite,           "sdfs_overwrite");
    run_test(test_sdfs_empty_file,          "sdfs_empty_file");

    /* ---- Directory tests ---- */
    run_test(test_sdfs_dir_create,    "sdfs_dir_create");
    run_test(test_sdfs_dir_list,      "sdfs_dir_list");
    run_test(test_sdfs_dir_not_empty, "sdfs_dir_not_empty");
    run_test(test_sdfs_nested_dirs,   "sdfs_nested_dirs");

    /* ---- Rename tests ---- */
    run_test(test_sdfs_rename_file,         "sdfs_rename_file");
    run_test(test_sdfs_rename_dir,          "sdfs_rename_dir");
    run_test(test_sdfs_rename_not_found,    "sdfs_rename_not_found");
    run_test(test_sdfs_rename_overwrite,    "sdfs_rename_overwrite");

    /* ---- Delete tests ---- */
    run_test(test_sdfs_delete_file,            "sdfs_delete_file");
    run_test(test_sdfs_delete_nonexistent,     "sdfs_delete_nonexistent");
    run_test(test_sdfs_delete_root,            "sdfs_delete_root");
    run_test(test_sdfs_delete_file_with_data,  "sdfs_delete_file_with_data");
    run_test(test_sdfs_delete_dir_empty,       "sdfs_delete_dir_empty");

    /* ---- Persistence tests ---- */
    run_test(test_sdfs_persist_remount,       "sdfs_persist_remount");
    run_test(test_sdfs_persist_dir_structure, "sdfs_persist_dir_structure");

    /* ---- V2: Permissions & timestamps ---- */
    run_test(test_perms_set_get,       "perms_set_get");
    run_test(test_perms_default,        "perms_default");
    run_test(test_timestamp_created,    "timestamp_created");
    run_test(test_timestamp_remount,    "timestamp_remount");

    /* ---- V2: Compat ---- */
    run_test(test_v1_compat_read,       "v1_compat_read");

    /* ---- Disk full test ---- */
    run_test(test_sdfs_diskfull, "sdfs_diskfull");

    serial_print("\n[runner] Scheduler tests...\n");
    run_test(test_vma_intervals, "vma_intervals");
    run_test(test_kernel_stack_guard, "kernel_stack_guard");
    run_test(test_pmm_high_memory, "pmm_high_memory");
    run_test(test_pmm_contract, "pmm_contract");
    run_test(test_kheap_contract, "kheap_contract");
    run_test(test_process_thread_ownership, "process_thread_ownership");
    run_test(test_sched_sleep,    "sched_sleep");
    run_test(test_sched_priority, "sched_priority");
    run_test(test_sched_wait_queue, "sched_wait_queue");

    serial_print("\n[runner] Ring-3 integration tests...\n");
    run_test(test_ring3_processes, "ring3_processes");

    serial_print("\n");
    TEST_RESULT(test_pass, test_fail);
    TEST_RUNNER_END;

    test_runner_done = 1;
    qemu_test_exit(test_fail != 0);
    while (1) { asm volatile("hlt"); }
}

#endif /* KERNEL_TEST */
