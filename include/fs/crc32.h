#ifndef SDFS_CRC32_H
#define SDFS_CRC32_H

#include <stdint.h>

uint32_t crc32_calc(const void *data, uint32_t length);
uint32_t crc32_update(uint32_t crc, const void *data, uint32_t length);

#endif
