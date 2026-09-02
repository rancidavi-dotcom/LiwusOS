#ifdef KERNEL_TEST
#include "sdfs.h"
#include "crc32.h"
#include "framework.h"
#include "string.h"

int test_crc32_empty(void) {
    uint32_t crc = crc32_calc("", 0);
    ASSERT(crc == 0x00000000, "CRC32 of empty = 0 (for this impl)");
    PASS("test_crc32_empty");
}

int test_crc32_known(void) {
    uint32_t crc = crc32_calc("123456789", 9);
    ASSERT(crc == 0xCBF43926, "CRC32 of '123456789' = 0xCBF43926");
    PASS("test_crc32_known");
}

int test_crc32_consistency(void) {
    uint8_t data[256];
    for (int i = 0; i < 256; i++) data[i] = (uint8_t)i;
    uint32_t c1 = crc32_calc(data, 256);
    uint32_t c2 = crc32_calc(data, 256);
    ASSERT(c1 == c2, "CRC32 is deterministic");
    ASSERT(c1 != 0, "CRC32 of non-empty data is non-zero");
    PASS("test_crc32_consistency");
}

#endif
