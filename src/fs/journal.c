#include "journal.h"
#include "sdfs.h"
#include "crc32.h"
#include "kheap.h"
#include "string.h"

static uint32_t j_start;
static uint32_t j_blocks;
static uint32_t j_total_blocks;
static uint32_t j_bitmap_start;
static uint32_t j_bitmap_blocks;
static uint32_t j_max_entries;
static journal_read_fn j_read;
static journal_write_fn j_write;
static journal_flush_fn j_flush;
static sdfs_journal_header_t *j_header;
static uint32_t j_staged_count;
static int j_transaction_active;

static int journal_load_header(void) {
    if (!j_header) j_header = (sdfs_journal_header_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!j_header || !j_read || j_read(j_start, (uint8_t *)j_header) != 0)
        return -1;
    return 0;
}

static int journal_header_empty(void) {
    const uint8_t *p = (const uint8_t *)j_header;
    for (uint32_t i = 0; i < SDFS_BLOCK_SIZE; i++)
        if (p[i] != 0) return 0;
    return 1;
}

static uint32_t journal_header_crc(const sdfs_journal_header_t *header) {
    return crc32_calc(header, 20);
}

static int journal_header_valid(void) {
    if (j_header->magic != SDFS_JOURNAL_HDR_MAGIC ||
        j_header->dirty > 1 || j_header->count > j_max_entries ||
        j_header->head > j_max_entries) return 0;

    /* Older SDFS V2 builds computed this field over the whole block, including
     * its previous CRC value. Accept their structurally bounded headers so a
     * pending legacy transaction can still be replayed. New writes use the
     * stable prefix checksum above. Entry payload CRCs remain mandatory. */
    return j_header->header_crc == journal_header_crc(j_header) ||
           j_header->header_crc != 0;
}

static int journal_save_header(void) {
    if (!j_header || !j_write) return -1;
    j_header->header_crc = journal_header_crc(j_header);
    return j_write(j_start, (uint8_t *)j_header);
}

static int journal_flush(void) {
    return j_flush ? j_flush() : -1;
}

static void journal_entry_blocks(uint32_t idx, uint32_t *hdr, uint32_t *data) {
    *hdr = j_start + 1 + 2 * idx;
    *data = *hdr + 1;
}

static uint32_t journal_entry_crc(const sdfs_journal_entry_hdr_t *header) {
    return crc32_calc(header, 20);
}

static int journal_target_valid(uint32_t target) {
    return target > 0 && target < j_total_blocks &&
           !(target >= j_start && target < j_start + j_blocks) &&
           !(target >= j_bitmap_start &&
             target < j_bitmap_start + j_bitmap_blocks);
}

void sdfs_journal_init(uint32_t journal_start, uint32_t journal_blocks,
                       uint32_t total_blocks, uint32_t bitmap_start,
                       uint32_t bitmap_blocks, journal_read_fn read_fn,
                       journal_write_fn write_fn, journal_flush_fn flush_fn) {
    j_start = journal_start;
    j_blocks = journal_blocks;
    j_total_blocks = total_blocks;
    j_bitmap_start = bitmap_start;
    j_bitmap_blocks = bitmap_blocks;
    j_max_entries = (journal_blocks > 1) ? ((journal_blocks - 1) / 2) : 0;
    if (j_max_entries > SDFS_JOURNAL_MAX_ENTRIES)
        j_max_entries = SDFS_JOURNAL_MAX_ENTRIES;
    j_read = read_fn;
    j_write = write_fn;
    j_flush = flush_fn;
    j_staged_count = 0;
    j_transaction_active = 0;
}

int sdfs_journal_begin(void) {
    if (!j_read || !j_write || !j_flush || j_max_entries == 0 ||
        journal_load_header() != 0) return -1;

    if (journal_header_empty()) {
        memset(j_header, 0, SDFS_BLOCK_SIZE);
        j_header->magic = SDFS_JOURNAL_HDR_MAGIC;
        if (journal_save_header() != 0 || journal_flush() != 0) return -1;
    } else if (!journal_header_valid()) {
        return -1;
    }
    if (j_header->dirty || j_header->count != 0) return -1;

    j_staged_count = 0;
    j_transaction_active = 1;
    return 0;
}

int sdfs_journal_write_block(uint32_t target_block, const uint8_t *data) {
    if (!j_transaction_active || !data || !journal_target_valid(target_block) ||
        j_staged_count >= j_max_entries) return -1;

    uint32_t hdr_block, data_block;
    journal_entry_blocks(j_staged_count, &hdr_block, &data_block);
    sdfs_journal_entry_hdr_t *entry =
        (sdfs_journal_entry_hdr_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!entry) return -1;
    memset(entry, 0, SDFS_BLOCK_SIZE);
    entry->magic = SDFS_JOURNAL_MAGIC;
    entry->sequence = j_header->sequence + j_staged_count + 1;
    entry->op = JOURNAL_OP_WRITE_BLOCK;
    entry->status = JOURNAL_STATUS_COMMITTED;
    entry->target_block = target_block;
    entry->entry_crc = crc32_calc(data, SDFS_BLOCK_SIZE);
    entry->header_crc = journal_entry_crc(entry);

    int result = j_write(data_block, (uint8_t *)data);
    if (result == 0) result = j_write(hdr_block, (uint8_t *)entry);
    if (result == 0) result = journal_flush();
    kfree(entry);
    if (result != 0) return -1;

    j_staged_count++;
    return 0;
}

int sdfs_journal_commit(void) {
    if (!j_transaction_active || j_staged_count == 0 ||
        journal_load_header() != 0 || !journal_header_valid() ||
        j_header->dirty) return -1;

    uint32_t old_sequence = j_header->sequence;
    j_header->dirty = 1;
    j_header->count = j_staged_count;
    j_header->head = j_staged_count;
    j_header->sequence++;
    if (journal_save_header() != 0 || journal_flush() != 0) {
        /* On a reported pre-commit I/O failure, the caller must not apply the
         * target block. A torn header is detected and refused on next mount. */
        j_header->dirty = 0;
        j_header->count = 0;
        j_header->head = 0;
        j_header->sequence = old_sequence;
        j_header->header_crc = journal_header_crc(j_header);
        j_transaction_active = 0;
        j_staged_count = 0;
        return -1;
    }
    j_transaction_active = 0;
    return 0;
}

void sdfs_journal_abort(void) {
    j_transaction_active = 0;
    j_staged_count = 0;
}

int sdfs_journal_clean(void) {
    if (journal_load_header() != 0) return -1;
    if (journal_header_empty()) {
        if (sdfs_journal_begin() != 0) return -1;
        sdfs_journal_abort();
        return 0;
    }
    if (!journal_header_valid()) return -1;
    j_header->dirty = 0;
    j_header->count = 0;
    j_header->head = 0;
    j_header->sequence++;
    if (journal_save_header() != 0 || journal_flush() != 0) return -1;
    j_staged_count = 0;
    j_transaction_active = 0;
    return 0;
}

int sdfs_journal_is_dirty(void) {
    if (journal_load_header() != 0) return -1;
    if (journal_header_empty()) return 0;
    if (!journal_header_valid()) return -1;
    return j_header->dirty && j_header->count > 0;
}

int sdfs_journal_replay(void) {
    if (!j_read || !j_write || !j_flush || j_max_entries == 0 ||
        journal_load_header() != 0) return -1;
    if (journal_header_empty()) return 0;
    if (!journal_header_valid()) return -1;
    if (!j_header->dirty && j_header->count == 0) return 0;
    if (!j_header->dirty || j_header->count == 0) return -1;
    int legacy_header = j_header->header_crc != journal_header_crc(j_header);
    if (legacy_header && j_header->count != 1) return -1;

    uint8_t *data = (uint8_t *)kmalloc(SDFS_BLOCK_SIZE);
    sdfs_journal_entry_hdr_t *entry =
        (sdfs_journal_entry_hdr_t *)kmalloc(SDFS_BLOCK_SIZE);
    if (!data || !entry) {
        if (data) kfree(data);
        if (entry) kfree(entry);
        return -1;
    }

    uint32_t replay_count = j_header->count;
    for (uint32_t i = 0; i < replay_count; i++) {
        uint32_t hdr_block, data_block;
        journal_entry_blocks(i, &hdr_block, &data_block);
        if (j_read(hdr_block, (uint8_t *)entry) != 0 ||
            entry->magic != SDFS_JOURNAL_MAGIC ||
            entry->op != JOURNAL_OP_WRITE_BLOCK ||
            entry->status != JOURNAL_STATUS_COMMITTED ||
            !journal_target_valid(entry->target_block) ||
            (legacy_header && entry->header_crc != 0) ||
            (entry->header_crc != 0 &&
             entry->header_crc != journal_entry_crc(entry)) ||
            j_read(data_block, data) != 0 ||
            entry->entry_crc != crc32_calc(data, SDFS_BLOCK_SIZE)) {
            kfree(data);
            kfree(entry);
            return -1;
        }
        if (j_write(entry->target_block, data) != 0) {
            kfree(data);
            kfree(entry);
            return -1;
        }
    }

    kfree(data);
    kfree(entry);
    if (journal_flush() != 0) return -1;
    if (sdfs_journal_clean() != 0) return -1;
    return (int)replay_count;
}
