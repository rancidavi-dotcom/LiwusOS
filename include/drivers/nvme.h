#ifndef NVME_H
#define NVME_H

#include <stdint.h>

/*
 * Minimal polled NVMe driver (admin + single I/O queue pair).
 *
 * Sector-oriented API mirrors the AHCI driver so it can be wired into
 * SDFS with the same call shape. All transfers use 512-byte logical
 * sector addressing; the device logical block size is handled internally.
 */

void nvme_init(void);
int nvme_available(void);

/* nsid: namespace id (1-based). Returns 0 on success, non-zero on error. */
int nvme_read_sector(uint8_t nsid, uint64_t lba, uint32_t count, uint8_t *buf);
int nvme_write_sector(uint8_t nsid, uint64_t lba, uint32_t count, uint8_t *buf);

/* Picks the first usable namespace, storing its id in *nsid_out. */
int nvme_find_first(uint8_t *nsid_out);

/* Total capacity of a namespace in 512-byte sectors. */
uint64_t nvme_identify(uint8_t nsid);

/* Raw logical block size (bytes) of the active namespace. */
uint32_t nvme_sector_size(void);

#endif
