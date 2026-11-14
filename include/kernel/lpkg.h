#ifndef KERNEL_LPKG_H
#define KERNEL_LPKG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char name[32];
    char version[16];
    char category[24];
    char desc[96];
    char remote_url[160];
    char baseline_version[16];
    bool is_fullscreen_gui;
    bool is_core;
} lpkg_pkg_info_t;

typedef struct {
    char current_version[16];
    char remote_version[16];
    char release_date[24];
    char packages[96];
    char notes[512];
    char boot_image_url[256];
    uint32_t boot_image_size;
    uint32_t boot_image_crc32;
    bool boot_image_available;
    char system_archive_url[256];
    uint32_t system_archive_size;
    uint32_t system_archive_crc32;
    bool system_archive_available;
    bool update_available;
    bool checked_online;
} lpkg_sysupdate_info_t;

typedef struct {
    int overall_percent;
    size_t completed;
    size_t total;
    const char *phase;
    const char *message;
} lpkg_progress_info_t;

typedef void (*lpkg_progress_fn)(const lpkg_progress_info_t *progress, void *user_data);

int lpkg_get_catalog_count(void);
const lpkg_pkg_info_t *lpkg_get_catalog_item(int index);
const lpkg_pkg_info_t *lpkg_find_catalog(const char *name);

bool lpkg_is_installed(const char *name, uint32_t *size_out);
bool lpkg_get_installed_version(const char *name, char *ver_out, size_t max_len);
bool lpkg_has_update(const char *name);
bool lpkg_is_repo_synced(void);

int lpkg_install_pkg(const char *pkg_or_url, const char *custom_name,
                     char *msg_out, size_t msg_max);
int lpkg_remove_pkg(const char *name, char *msg_out, size_t msg_max);
int lpkg_update_repo(char *msg_out, size_t msg_max);

void lpkg_get_os_version(char *ver_out, size_t max_len);
int lpkg_check_system_update(lpkg_sysupdate_info_t *info_out, char *msg_out, size_t msg_max);
int lpkg_perform_system_update(lpkg_progress_fn progress_cb, void *user_data,
                               char *msg_out, size_t msg_max);

void lpkg_get_repo_url(char *buf, size_t buf_max);
void lpkg_set_repo_url(const char *url);

void cmd_lpkg(int argc, char **argv);

#endif /* KERNEL_LPKG_H */
