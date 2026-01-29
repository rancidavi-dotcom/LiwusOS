#include "journal.h"
#include "sdfs.h"
#include "crc32.h"
#include "serial.h"
#include "string.h"
#include "kheap.h"

static uint32_t j_start = 0;        /* first block of journal area */
static uint32_t j_blocks = 0;       /* total blocks in journal area */
static uint32_t j_max_entries = 0;  /* (j_blocks - 1) / 2 */
static journal_read_fn  j_read  = 0;
static journal_write_fn j_write = 0;

static sdfs_journal_header_t *j_header = NULL;

void sdfs_journal_init(uint32_t journal_start, uint32_t journal_blocks,
                       journal_read_fn read_fn, journal_write_fn write_fn) {
    j_start = journal_start;
    j_blocks = journal_blocks;
    j_max_entries = (journal_blocks > 1) ? ((journal_blocks - 1) / 2) : 0;
    if (j_max_entries > SDFS_JOURNAL_MAX_ENTRIES)
        j_max_entries = SDFS_JOURNAL_MAX_ENTRIES;
    j_read  = read_fn;
    j_write = write_fn;
}

static void journal_load_header(void) {
    if (!j_header) {
        j_header = (sdfs_journal_header_t *)kmalloc(SDFS_BLOCK_SIZE);
    }
    if (!j_header) return;
    j_read(j_start, (uint8_t *)j_header);
}

static void journal_save_header(void) {
    if (!j_header) return;
    j_header->header_crc = crc32_calc((uint8_t *)j_header + 4,
                                       SDFS_BLOCK_SIZE - 4);
    j_write(j_start, (uint8_t *)j_header);
}

void sdfs_journal_begin(void) {
    journal_load_header();
    if (!j_header) return;

    if (j_header->magic != SDFS_JOURNAL_HDR_MAGIC) {
        memset(j_header, 0, SDFS_BLOCK_SIZE);
        j_header->magic = SDFS_JOURNAL_HDR_MAGIC;
        j_header->dirty = 0;
        j_header->sequence = 0;
        j_header->head = 0;
        j_header->count = 0;
    }
    j_header->dirty = 0;
    j_header->count = 0;
    journal_save_header();
}

/* Physical blocks for a given entry index i:
 *   header block = j_start + 1 + 2*i
 *   data block   = j_start + 2 + 2*i
 */
static void journal_entry_blocks(uint32_t idx, uint32_t *hdr, uint32_t *data) {
    *hdr = j_start + 1 + 2 * idx;
    *data = *hdr + 1;
}

int sdfs_journal_write_block(uint32_t target_block, const uint8_t *data) {
    if (!j_header || !j_write) return -1;

    uint32_t idx = j_header->head;
    if (idx >= j_max_entries) {
        j_header->head = 0;
        idx = 0;
    }

    uint32_t hdr_block, data_block;
    journal_entry_blocks(idx, &hdr_block, &data_block);

    sdfs_journal_entry_hdr_t *hdr =
        (sdfs_journal_entry_hdr_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!hdr) return -1;

    memset(hdr, 0, SDFS_BLOCK_SIZE);
    hdr->magic = SDFS_JOURNAL_MAGIC;
    hdr->sequence = j_header->sequence + 1;
    hdr->op = JOURNAL_OP_WRITE_BLOCK;
    hdr->status = JOURNAL_STATUS_COMMITTED;
    hdr->target_block = target_block;

    hdr->entry_crc = crc32_calc(data, SDFS_BLOCK_SIZE);

    if (j_write(hdr_block, (uint8_t *)hdr) != 0) {
        kfree(hdr);
        return -1;
    }
    kfree(hdr);

    if (j_write(data_block, (uint8_t *)data) != 0) {
        return -1;
    }

    j_header->head = (idx + 1) % j_max_entries;
    j_header->count++;
    j_header->sequence++;
    j_header->dirty = 1;
    journal_save_header();

    return 0;
}

int sdfs_journal_commit(void) {
    if (!j_header) return -1;
    j_header->dirty = 1;
    journal_save_header();
    return 0;
}

void sdfs_journal_clean(void) {
    if (!j_header) return;
    j_header->dirty = 0;
    j_header->count = 0;
    j_header->head = 0;
    j_header->sequence++;
    journal_save_header();
}

int sdfs_journal_is_dirty(void) {
    journal_load_header();
    if (!j_header) return 0;
    if (j_header->magic != SDFS_JOURNAL_HDR_MAGIC) return 0;
    return j_header->dirty && j_header->count > 0;
}

int sdfs_journal_replay(void) {
    if (!j_header || !j_read || !j_write) return 0;

    journal_load_header();
    if (j_header->magic != SDFS_JOURNAL_HDR_MAGIC) return 0;
    if (!j_header->dirty || j_header->count == 0) return 0;

    uint32_t replayed = 0;
    uint8_t *data_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!data_buf) return 0;
    uint8_t *block_buf = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!block_buf) { kfree(data_buf); return 0; }

    for (uint32_t i = 0; i < j_max_entries; i++) {
        uint32_t hdr_block, data_block;
        journal_entry_blocks(i, &hdr_block, &data_block);

        sdfs_journal_entry_hdr_t *hdr =
            (sdfs_journal_entry_hdr_t *)kmalloc(SDFS_BLOCK_SIZE);
        if (!hdr) break;

        j_read(hdr_block, (uint8_t *)hdr);

        if (hdr->magic != SDFS_JOURNAL_MAGIC ||
            hdr->status != JOURNAL_STATUS_COMMITTED) {
            kfree(hdr);
            continue;
        }

        j_read(data_block, data_buf);

        uint32_t expected_crc = hdr->entry_crc;
        uint32_t actual_crc = crc32_calc(data_buf, SDFS_BLOCK_SIZE);

        if (expected_crc != actual_crc) {
            kfree(hdr);
            continue;
        }

        j_write(hdr->target_block, data_buf);
        memset(block_buf, 0, SDFS_BLOCK_SIZE);
        block_buf[0] = JOURNAL_STATUS_REPLAYED;
        j_write(hdr_block, block_buf);
        replayed++;
        kfree(hdr);
    }

    j_header->dirty = 0;
    j_header->count = 0;
    j_header->head = 0;
    journal_save_header();

    kfree(data_buf);
    kfree(block_buf);

    return (int)replayed;
}
