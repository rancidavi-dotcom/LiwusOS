#ifndef KERNEL_RTC_H
#define KERNEL_RTC_H

#include <stdint.h>

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} rtc_time_t;

/* Reads the wall-clock time from the CMOS/RTC (24-hour format). */
rtc_time_t rtc_read_time(void);

#endif /* KERNEL_RTC_H */
