/* CMOS RTC wall clock and Unix time conversion. */
#include "rtc.h"
#include "io.h"

typedef long time_t;

extern uint64_t ntp_get_unix_time(void);

time_t time(time_t *t) {
    /* X.509 certificate checks must use a real clock, never a guessed date. */
    time_t val = (time_t)ntp_get_unix_time();
    if (t) *t = val;
    return val;
}

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    __asm__ volatile("nop");
    return inb(0x71);
}

static int bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
}

static int rtc_is_leap_year(uint16_t year) {
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

rtc_time_t rtc_read_time(void) {
    rtc_time_t t = {0};
    uint8_t reg_b = cmos_read(0x0B);
    int binary = (reg_b & 0x04) != 0;
    uint8_t century, year, month, day, hour_raw, minute, second;

    /* Avoid combining fields from opposite sides of a clock update. */
    unsigned retries = 1000;
    do {
        if (--retries == 0) return t;
        while (cmos_read(0x0A) & 0x80) {
            if (--retries == 0) return t;
        }
        century = cmos_read(0x32);
        year = cmos_read(0x09);
        month = cmos_read(0x08);
        day = cmos_read(0x07);
        hour_raw = cmos_read(0x04);
        minute = cmos_read(0x02);
        second = cmos_read(0x00);
    } while (cmos_read(0x0A) & 0x80);

    int is_pm = (hour_raw & 0x80) != 0;
    hour_raw &= 0x7F;
    if (!binary) {
        if ((year & 0x0F) > 9 || (year >> 4) > 9 ||
            (month & 0x0F) > 9 || (month >> 4) > 9 ||
            (day & 0x0F) > 9 || (day >> 4) > 9 ||
            (hour_raw & 0x0F) > 9 || (hour_raw >> 4) > 9 ||
            (minute & 0x0F) > 9 || (minute >> 4) > 9 ||
            (second & 0x0F) > 9 || (second >> 4) > 9) {
            return t;
        }
        if ((century & 0x0F) > 9 || (century >> 4) > 9) century = 0;
        century = (uint8_t)bcd_to_bin(century);
        year = (uint8_t)bcd_to_bin(year);
        month = (uint8_t)bcd_to_bin(month);
        day = (uint8_t)bcd_to_bin(day);
        hour_raw = (uint8_t)bcd_to_bin(hour_raw);
        minute = (uint8_t)bcd_to_bin(minute);
        second = (uint8_t)bcd_to_bin(second);
    }

    if (!(reg_b & 0x02)) { /* 12-hour mode */
        if (hour_raw == 12) hour_raw = 0;
        if (is_pm) hour_raw = (uint8_t)(hour_raw + 12);
    }

    if (century >= 19 && century <= 99) {
        t.year = (uint16_t)(century * 100 + year);
    } else {
        t.year = (uint16_t)(year <= 69 ? 2000 + year : 1900 + year);
    }
    t.month = month;
    t.day = day;
    t.hour = hour_raw;
    t.minute = minute;
    t.second = second;
    return t;
}

uint64_t rtc_get_unix_time(void) {
    static const uint8_t days_per_month[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    rtc_time_t rtc = rtc_read_time();
    if (rtc.year < 1970 || rtc.month < 1 || rtc.month > 12 ||
        rtc.day < 1 || rtc.hour > 23 || rtc.minute > 59 || rtc.second > 59) {
        return 0;
    }

    uint8_t max_day = days_per_month[rtc.month - 1];
    if (rtc.month == 2 && rtc_is_leap_year(rtc.year)) max_day++;
    if (rtc.day > max_day) return 0;

    uint64_t days = 0;
    for (uint16_t year = 1970; year < rtc.year; ++year) {
        days += rtc_is_leap_year(year) ? 366 : 365;
    }
    for (uint8_t month = 1; month < rtc.month; ++month) {
        days += days_per_month[month - 1];
        if (month == 2 && rtc_is_leap_year(rtc.year)) days++;
    }
    days += (uint64_t)rtc.day - 1;
    return days * 86400 + (uint64_t)rtc.hour * 3600 +
           (uint64_t)rtc.minute * 60 + rtc.second;
}

static void cmos_write(uint8_t reg, uint8_t val) {
    outb(0x70, reg);
    __asm__ volatile("nop");
    outb(0x71, val);
}

static uint8_t bin_to_bcd(uint8_t bin) {
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

void rtc_write_time(uint8_t hour, uint8_t min, uint8_t sec) {
    uint8_t reg_b = cmos_read(0x0B);
    if (!(reg_b & 0x04)) {
        hour = bin_to_bcd(hour);
        min = bin_to_bcd(min);
        sec = bin_to_bcd(sec);
    }
    cmos_write(0x00, sec);
    cmos_write(0x02, min);
    cmos_write(0x04, hour);
}
