#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_time_t;

/* Reads the wall-clock time from the CMOS/RTC (24-hour format). */
rtc_time_t rtc_read_time(void);

/* Returns Unix seconds from CMOS; returns 0 when the date is invalid. */
uint64_t rtc_get_unix_time(void);

/* Writes the wall-clock time to the CMOS/RTC (24-hour format). */
void rtc_write_time(uint8_t hour, uint8_t minute, uint8_t second);

#endif /* KERNEL_RTC_H */
