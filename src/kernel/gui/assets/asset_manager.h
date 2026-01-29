/*
 * gui/assets/asset_manager.h
 *
 * Centralized loading and caching of GUI assets (fonts, cursors, images, etc.)
 */
#ifndef GUI_ASSET_MANAGER_H
#define GUI_ASSET_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "../render/renderer.h"

/* Initialize the asset manager and its internal caches */
void asset_manager_init(void);

/* Clean up */
void asset_manager_destroy(void);

/* Get a font by name. Returns a system default font if name is NULL or not found. */
const glyph_t *asset_manager_get_font(const char *name);

#endif
