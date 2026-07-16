#ifndef EDID_H
#define EDID_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char manufacturer[4];
    uint16_t product_code;
    uint32_t serial_number;
    uint8_t week_of_manufacture;
    uint16_t year_of_manufacture;
    uint8_t edid_version;
    uint8_t edid_revision;
    
    // Limits
    uint32_t max_resolution_x;
    uint32_t max_resolution_y;
    uint32_t min_resolution_x;
    uint32_t min_resolution_y;
    uint32_t refresh_rate_hz;
    
    char monitor_name[14];
} edid_info_t;

/* Parse a 128-byte EDID block and fill the info struct */
bool edid_parse(const uint8_t *edid_data, edid_info_t *info);

/* Get the system's current monitor info (either parsed from hardware or fallback) */
bool edid_get_monitor_info(edid_info_t *info);

#endif
