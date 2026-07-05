#ifndef BOOT_ANIM_H
#define BOOT_ANIM_H

#include <stdint.h>

void boot_anim_init(void);
void boot_anim_update(int percent, const char *current_file);
void boot_anim_finish(void);

#endif
