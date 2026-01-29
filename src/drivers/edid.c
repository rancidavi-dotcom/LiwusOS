#include "edid.h"
#include "string.h"

// A realistic dummy EDID block for a 1920x1080 60Hz monitor ("Generic 1080p")
// Used as fallback when real hardware I2C EDID reading is unavailable.
static const uint8_t s_fallback_edid[128] = {
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, // Header
    0x1E, 0x6D, // Manufacturer ID (LGD - LG Display)
    0x01, 0x00, // Product Code
    0x01, 0x00, 0x00, 0x00, // Serial Number
    0x01, 0x14, // Week 1, Year 2010
    0x01, 0x03, // EDID 1.3
    0x80, 0x35, 0x1E, 0x78, // Basic display parameters
    0xEA, 0xAE, 0xC5, 0xA6, 0x54, 0x4C, 0x99, 0x26, 0x0F, 0x50, 0x54, // Color characteristics
    0x21, 0x08, 0x00, // Timings
    0x81, 0x80, 0x95, 0x00, 0xA9, 0x40, 0xB3, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, // Standard timings (e.g. 1280x1024, 1440x900)
    
    // Detailed Timing Descriptor 1 (Preferred - 1920x1080 @ 60Hz)
    0x02, 0x3A, 0x80, 0x18, 0x71, 0x38, 0x2D, 0x40, 0x58, 0x2C, 0x45, 0x00, 0x13, 0x2A, 0x21, 0x00, 0x00, 0x1E,
    
    // Detailed Timing Descriptor 2 (Monitor Name)
    0x00, 0x00, 0x00, 0xFC, 0x00, 'G', 'e', 'n', 'e', 'r', 'i', 'c', ' ', '1', '0', '8', '0', 'p',
    
    // Detailed Timing Descriptor 3 (Unused)
    0x00, 0x00, 0x00, 0xFD, 0x00, 0x38, 0x4C, 0x1E, 0x53, 0x11, 0x00, 0x0A, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20,
    
    // Detailed Timing Descriptor 4 (Unused)
    0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    
    0x00, 0x00 // Extension flag and Checksum (mocked)
};

bool edid_parse(const uint8_t *edid_data, edid_info_t *info) {
    if (!edid_data || !info) return false;
    
    // Check header 00 FF FF FF FF FF FF 00
    if (edid_data[0] != 0x00 || edid_data[7] != 0x00 || edid_data[1] != 0xFF) {
        return false;
    }
    
    memset(info, 0, sizeof(edid_info_t));
    
    // Manufacturer ID (Big Endian 16-bit compressed ASCII)
    uint16_t mfg = (edid_data[8] << 8) | edid_data[9];
    info->manufacturer[0] = ((mfg >> 10) & 0x1F) + '@';
    info->manufacturer[1] = ((mfg >> 5) & 0x1F) + '@';
    info->manufacturer[2] = (mfg & 0x1F) + '@';
    info->manufacturer[3] = '\0';
    
    info->product_code = edid_data[10] | (edid_data[11] << 8);
    info->serial_number = edid_data[12] | (edid_data[13] << 8) | (edid_data[14] << 16) | (edid_data[15] << 24);
    info->week_of_manufacture = edid_data[16];
    info->year_of_manufacture = edid_data[17] + 1990;
    
    info->edid_version = edid_data[18];
    info->edid_revision = edid_data[19];
    
    // We assume 640x480 is the minimum safe standard for modern EDID parsing
    info->min_resolution_x = 640;
    info->min_resolution_y = 480;
    
    // Parse detailed timing blocks for preferred resolution
    for (int i = 0; i < 4; i++) {
        int offset = 54 + (i * 18);
        const uint8_t *desc = &edid_data[offset];
        
        // If it's a display descriptor
        if (desc[0] == 0x00 && desc[1] == 0x00) {
            if (desc[3] == 0xFC) { // Monitor name
                int k = 0;
                for (int j = 0; j < 13; j++) {
                    if (desc[5 + j] == '\n') break;
                    info->monitor_name[k++] = desc[5 + j];
                }
                info->monitor_name[k] = '\0';
            }
        } else {
            // Detailed Timing Descriptor (Pixel Clock > 0)
            uint16_t pixel_clock_10khz = desc[0] | (desc[1] << 8);
            if (pixel_clock_10khz > 0) {
                uint32_t ha = desc[2] | ((desc[4] & 0xF0) << 4);
                uint32_t va = desc[5] | ((desc[7] & 0xF0) << 4);
                uint32_t hblank = desc[3] | ((desc[4] & 0x0F) << 8);
                uint32_t vblank = desc[6] | ((desc[7] & 0x0F) << 8);
                
                if (ha > info->max_resolution_x) {
                    info->max_resolution_x = ha;
                    info->max_resolution_y = va;
                    
                    // Calculate refresh rate: Pixel Clock / (H_Total * V_Total)
                    uint32_t htotal = ha + hblank;
                    uint32_t vtotal = va + vblank;
                    if (htotal > 0 && vtotal > 0) {
                        info->refresh_rate_hz = (pixel_clock_10khz * 10000) / (htotal * vtotal);
                    }
                }
            }
        }
    }
    
    if (info->monitor_name[0] == '\0') {
        strcpy(info->monitor_name, "Generic Monitor");
    }
    
    return true;
}

bool edid_get_monitor_info(edid_info_t *info) {
    // TODO: In the future, read the 128-byte block via GPU I2C (or VM86 INT 10h AX=4F15h)
    // For now, parse the fallback mock EDID.
    return edid_parse(s_fallback_edid, info);
}
