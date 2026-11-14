#ifndef LIWUS_SCREENSHOT_H
#define LIWUS_SCREENSHOT_H

#include <stdint.h>

/* Send one complete BMP over TCP; the receiver replies with "OK" on success. */
int net_send_screenshot_bmp(const char *host_ip, uint16_t port,
                            const uint8_t *data, uint32_t size);

#endif
