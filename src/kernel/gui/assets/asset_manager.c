/*
 * gui/assets/asset_manager.c
 */
#include "asset_manager.h"
#include "kheap.h"
#include "string.h"

extern char _binary_src_drivers_font_psf_start[];

static glyph_t s_default_font[256];
static bool s_font_loaded = false;

void asset_manager_init(void) {
    if (s_font_loaded) return;

    uint8_t *font_hdr = (uint8_t*)&_binary_src_drivers_font_psf_start;
    if (font_hdr[0] == 0x36 && font_hdr[1] == 0x04) {
        int bpg = font_hdr[3];
        uint8_t *font_data = font_hdr + 4;

        for (int i = 0; i < 256; i++) {
            s_default_font[i].cell_w = 8;
            s_default_font[i].cell_h = 16;
            
            /* Allocate and copy bitmap to heap instead of pointing to ROM? 
             * No, pointing to ROM is fine since it's static in the kernel. */
            s_default_font[i].bitmap = font_data + (i * bpg);
        }
    } else {
        /* Fallback empty font */
        memset(s_default_font, 0, sizeof(s_default_font));
    }
    s_font_loaded = true;
}

void asset_manager_destroy(void) {
    /* currently nothing allocated dynamically */
    s_font_loaded = false;
}

const glyph_t *asset_manager_get_font(const char *name) {
    (void)name; /* For now we only have one font */
    if (!s_font_loaded) {
        asset_manager_init();
    }
    return s_default_font;
}
