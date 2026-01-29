#include "crc32.h"

static uint32_t crc32_table[256];
static int crc32_table_ready = 0;

static void crc32_build_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1)
                crc = (crc >> 1) ^ 0xEDB88320;
            else
                crc >>= 1;
        }
        crc32_table[i] = crc;
    }
    crc32_table_ready = 1;
}

uint32_t crc32_update(uint32_t crc, const void *data, uint32_t length) {
    if (!crc32_table_ready) crc32_build_table();
    const uint8_t *p = (const uint8_t *)data;
    crc = crc ^ 0xFFFFFFFF;
    for (uint32_t i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}

uint32_t crc32_calc(const void *data, uint32_t length) {
    return crc32_update(0, data, length);
}
