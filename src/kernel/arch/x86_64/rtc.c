/*
 * rtc.c — Leitura da hora real (CMOS/RTC) para o relógio do desktop.
 *
 * Acessa a porta 0x70 (índice) / 0x71 (dado). O RTC pode estar em modo BCD
 * ou binário; detectamos via Register B (0x0B, bit 2). Retorna hora 24h.
 */
#include "rtc.h"
#include "io.h"

static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    __asm__ volatile("nop");
    return inb(0x71);
}

static int bcd_to_bin(uint8_t bcd) {
    return ((bcd >> 4) & 0x0F) * 10 + (bcd & 0x0F);
}

rtc_time_t rtc_read_time(void) {
    rtc_time_t t;
    t.hour   = 0;
    t.minute = 0;
    t.second = 0;

    uint8_t reg_b = cmos_read(0x0B);
    int binary = (reg_b & 0x04) != 0;

    /* Leia primeiro os "update in progress" para evitar leituras inconsistentes
     * (valores podem mudar exatamente durante o update do RTC). */
    uint8_t h, m, s;
    do {
        h = cmos_read(0x04); /* hours   */
        m = cmos_read(0x02); /* minutes */
        s = cmos_read(0x00); /* seconds */
    } while ((cmos_read(0x0A) & 0x80) != 0); /* UIP */

    if (!binary) {
        h = (uint8_t)bcd_to_bin(h);
        m = (uint8_t)bcd_to_bin(m);
        s = (uint8_t)bcd_to_bin(s);
    }

    /* Detecta 12h (bit 0x80 de horas) e converte para 24h usando bit "PM"
     * (0x80 do register B status, aqui representado por hora & 0x80). */
    int hour = (h & 0x7F);
    if (!binary) {
        if ((h & 0x80) && hour < 12) hour += 12;
    } else if ((h & 0x80) && hour < 12) {
        hour += 12;
    }
    if (hour >= 24) hour -= 24;

    t.hour   = (uint8_t)hour;
    t.minute = m;
    t.second = s;
    return t;
}
