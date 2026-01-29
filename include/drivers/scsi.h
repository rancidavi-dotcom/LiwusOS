#ifndef SCSCI_H
#define SCSI_H 1

#include <stdint.h>

/* Initialize the AMD Am53C974 (PCscsi / ESP) driver. Looks the controller up
 * on the PCI bus and enables I/O space + bus mastering. */
void scsi_init(void);

/* Returns 1 if a SCSI target answered an INQUIRY on the bus, 0 if absent. */
int scsi_present(void);

/* Read `count` 512-byte blocks starting at `lba` into `buf` (count <= 16).
 * Returns 0 on success, -1 on I/O error, -2 if no device on the bus. */
int scsi_read_blocks(uint32_t lba, uint32_t count, uint8_t *buf);

#endif /* SCSI_H */