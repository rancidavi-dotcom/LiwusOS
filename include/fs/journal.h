#ifndef SDFS_JOURNAL_H
#define SDFS_JOURNAL_H

#include <stdint.h>

/*
 * SDFS metadata write-ahead log. A transaction stages full-block snapshots;
 * the journal header's dirty/count fields are the commit record and are not
 * written until all entries have reached the device. Replay is idempotent.
 *
 * Each logical entry consumes TWO physical blocks so a full 4096-byte
 * block snapshot can be stored. Max entries = (journal_blocks - 1)/2.
 */

#define SDFS_JOURNAL_MAGIC    0x4A464453  /* "SDFJ" little-endian */
#define SDFS_JOURNAL_HDR_MAGIC 0x48444A53 /* "SJDH" little-endian */

#define JOURNAL_OP_NONE          0
#define JOURNAL_OP_WRITE_BLOCK   1   /* Write full block to target */

#define JOURNAL_STATUS_FREE      0
#define JOURNAL_STATUS_COMMITTED 1
#define JOURNAL_STATUS_REPLAYED  2

#define SDFS_JOURNAL_MAX_ENTRIES 31

typedef struct {
    uint32_t magic;         /* SDFS_JOURNAL_HDR_MAGIC */
    uint32_t dirty;         /* 1 = has committed entries to replay */
    uint32_t sequence;      /* monotonically increasing */
    uint32_t head;          /* next entry index to write (circular) */
    uint32_t count;         /* number of committed entries pending replay */
    uint32_t header_crc;
    uint8_t  pad[4072];
} __attribute__((packed)) sdfs_journal_header_t;

typedef struct {
    uint32_t magic;         /* SDFS_JOURNAL_MAGIC */
    uint32_t sequence;
    uint8_t  op;            /* JOURNAL_OP_* */
    uint8_t  status;        /* JOURNAL_STATUS_* */
    uint16_t reserved;
    uint32_t target_block;  /* disk block to write */
    uint32_t entry_crc;
    uint32_t header_crc;    /* integrity of fields through entry_crc */
    uint8_t  pad[4072];     /* data lives in the following journal block */
} __attribute__((packed)) sdfs_journal_entry_hdr_t;

/*
 * Initialize journal subsystem.
 * journal_start: first block number of journal area
 * journal_blocks: total blocks reserved for journal (>=3; 1 header + even data)
 */
typedef int (*journal_read_fn)(uint32_t block, uint8_t *buffer);
typedef int (*journal_write_fn)(uint32_t block, uint8_t *buffer);
typedef int (*journal_flush_fn)(void);

void sdfs_journal_init(uint32_t journal_start, uint32_t journal_blocks,
                       uint32_t total_blocks, uint32_t bitmap_start,
                       uint32_t bitmap_blocks, journal_read_fn read_fn,
                       journal_write_fn write_fn, journal_flush_fn flush_fn);

/* Begin, stage, and commit a bounded transaction. */
int sdfs_journal_begin(void);

/* Write a block snapshot to the journal */
int sdfs_journal_write_block(uint32_t target_block, const uint8_t *data);

/* Mark the journal dirty so replay will happen on next mount if not cleaned */
int sdfs_journal_commit(void);

/* Abandon an uncommitted set of staged journal blocks. */
void sdfs_journal_abort(void);

/* Clean: mark dirty=0 after successful write to actual target blocks */
int sdfs_journal_clean(void);

/* Replay: on mount, if dirty, restore all journal entries */
int sdfs_journal_replay(void);

/* Check if journal is dirty (needs replay) */
int sdfs_journal_is_dirty(void);

#endif
