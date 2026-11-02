#ifdef KERNEL_TEST
#include "sdfs.h"
#include "journal.h"
#include "framework.h"
#include "serial.h"
#include "string.h"
#include "kheap.h"

/* journal_replay_basic: write journal entry, then call replay */
int test_journal_replay_basic(void) {
    uint8_t *original = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    uint8_t *journaled = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    uint8_t *result = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    ASSERT(original && journaled && result, "alloc ok");

    uint32_t test_block = 900;

    for (int i = 0; i < 256; i++) original[i] = 0xAA;
    sdfs_write_raw_block(test_block, original);

    for (int i = 0; i < 256; i++) journaled[i] = 0xBB;
    sdfs_journal_begin();
    sdfs_journal_write_block(test_block, journaled);

    for (int i = 0; i < 256; i++) original[i] = 0xCC;
    sdfs_write_raw_block(test_block, original);
    ASSERT(sdfs_journal_commit() == 0, "commit staged entry before replay");

    int replayed = sdfs_journal_replay();
    ASSERT(replayed > 0, "replay should replay entries");

    sdfs_read_raw_block(test_block, result);
    for (int i = 0; i < 256; i++) {
        if (result[i] != 0xBB) {
            kfree(original); kfree(journaled); kfree(result);
            FAIL("test_journal_replay_basic", "data mismatch after replay");
        }
    }

    kfree(original); kfree(journaled); kfree(result);
    PASS("test_journal_replay_basic");
}

/* journal_clean_noop: clean journal should not crash */
int test_journal_clean_noop(void) {
    sdfs_journal_begin();
    sdfs_journal_clean();
    ASSERT(!sdfs_journal_is_dirty(), "journal clean after clean");
    PASS("test_journal_clean_noop");
}

/* journal_multiple_entries: journal several blocks, replay all */
int test_journal_multiple_entries(void) {
    uint32_t test_base = 910;
    uint8_t *wbuf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    uint8_t *rbuf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    ASSERT(wbuf && rbuf, "alloc ok");

    sdfs_journal_begin();

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 256; j++) wbuf[j] = (uint8_t)(0x10 + i);
        sdfs_journal_write_block(test_base + i, wbuf);
    }

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 256; j++) wbuf[j] = 0xDE;
        sdfs_write_raw_block(test_base + i, wbuf);
    }

    sdfs_journal_commit();
    int replayed = sdfs_journal_replay();
    ASSERT(replayed >= 3, "replayed >= 3 entries");

    for (int i = 0; i < 3; i++) {
        sdfs_read_raw_block(test_base + i, rbuf);
        for (int j = 0; j < 256; j++) {
            if (rbuf[j] != (uint8_t)(0x10 + i)) {
                kfree(wbuf); kfree(rbuf);
                FAIL("test_journal_multiple_entries", "block data mismatch");
            }
        }
    }

    /* A corrupt committed payload must not be skipped and then silently
     * cleared. Preserve the journal blocks, verify mount refuses the damaged
     * log, restore the bytes, and then prove normal replay still succeeds. */
    sdfs_super_t *sb = (sdfs_super_t *)kmalloc(SDFS_BLOCK_SIZE);
    uint8_t *saved_journal = (uint8_t *)kmalloc(3 * SDFS_BLOCK_SIZE);
    ASSERT(sb && saved_journal, "journal corruption scratch allocated");
    ASSERT(sdfs_read_raw_block(0, (uint8_t *)sb) == 0, "superblock read");
    uint32_t target = 930;
    sdfs_journal_begin();
    for (int j = 0; j < SDFS_BLOCK_SIZE; j++) wbuf[j] = 0x6D;
    ASSERT(sdfs_journal_write_block(target, wbuf) == 0, "stage corruption probe");
    ASSERT(sdfs_journal_commit() == 0, "commit corruption probe");
    for (uint32_t i = 0; i < 3; i++)
        ASSERT(sdfs_read_raw_block(sb->journal_start + i,
                                   saved_journal + i * SDFS_BLOCK_SIZE) == 0,
               "save journal header and entry");
    wbuf[0] = 0x00;
    ASSERT(sdfs_write_raw_block(sb->journal_start + 2, wbuf) == 0,
           "corrupt staged payload block");
    ASSERT(sdfs_journal_replay() < 0, "replay rejects payload CRC mismatch");
    ASSERT(sdfs_journal_dirty() == 1, "corrupt journal remains dirty");
    ASSERT(sdfs_mount(0, 0, 0) == NULL && sdfs_mount_failed_corrupt(),
           "mount refuses unrecoverable journal without formatting");
    for (uint32_t i = 0; i < 3; i++)
        ASSERT(sdfs_write_raw_block(sb->journal_start + i,
                                    saved_journal + i * SDFS_BLOCK_SIZE) == 0,
               "restore journal after corruption probe");
    ASSERT(sdfs_mount(0, 0, 0) != NULL, "restored journal remounts");
    ASSERT(sdfs_read_raw_block(target, rbuf) == 0 && rbuf[0] == 0x6D,
           "restored journal replays complete block");
    kfree(sb);
    kfree(saved_journal);

    kfree(wbuf); kfree(rbuf);
    PASS("test_journal_multiple_entries");
}

#endif
