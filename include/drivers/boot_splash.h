#ifndef BOOT_SPLASH_H
#define BOOT_SPLASH_H

#include <stdint.h>

void boot_splash_init(void);
void boot_splash_set_progress(int progress, const char *status);
void boot_splash_set_max_progress(int max_progress);
void boot_splash_done(void);

#endif