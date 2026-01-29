#ifndef INSTALLER_H
#define INSTALLER_H

#include "gui.h"

typedef struct {
    char language[32];
    char keyboard_layout[32];
    char timezone[64];
    char hostname[64];
    char username[32];
    char password[32];
    char disk_target[64];
    char partition_scheme[32]; /* GPT/MBR */
    char filesystem[16];       /* ext4, btrfs */
    bool encrypt_disk;
    bool automatic_login;
    int install_profile;       /* 0: Minimal, 1: Desktop, 2: Server */
} install_config_t;

typedef enum {
    INT_INIT, DIAGNOSTIC, LOCALIZATION, IDENTITY, NETWORK, 
    STORAGE, BOOTLOADER, SECURITY, PROFILE, SERVICES, 
    REVIEW, EXECUTION, FINALIZE
} install_step_t;

widget_t* init_installer();
void draw_installer_full();

#endif
