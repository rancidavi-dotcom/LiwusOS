#ifndef INSTALLER_H
#define INSTALLER_H

#include <stdint.h>
#include "gui.h"

typedef enum {
    STEP_WELCOME_DISK,
    STEP_USERNAME,
    STEP_CONFIRM,
    STEP_FORMATTING,
    STEP_COPYING,
    STEP_DONE
} install_step_t;

typedef struct {
    char username[32];
    // Outras configs
} install_config_t;

void open_installer();
void draw_installer_full();

#endif
