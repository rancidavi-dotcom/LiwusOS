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

    kfree(wbuf); kfree(rbuf);
    PASS("test_journal_multiple_entries");
}

#endif
