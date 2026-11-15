#ifndef LIWUS_CRAFT_PLATFORM_H
#define LIWUS_CRAFT_PLATFORM_H
#include <stdint.h>

uint64_t craft_platform_ticks(void);
void craft_platform_present(const uint32_t *pixels, int width, int height);

#endif
