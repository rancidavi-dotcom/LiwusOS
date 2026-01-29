#ifndef WIFI_H
#define WIFI_H

#include <stdint.h>
#include "pci.h"
#include "net.h"

typedef struct {
    char ssid[33];
    int signal_strength;
    int is_secure;
} wifi_network_t;

void wifi_init(pci_device_t* dev);
int wifi_scan(wifi_network_t* networks, int max_networks);
void wifi_connect(const char* ssid, const char* password);
const char* wifi_get_current_ssid();
int wifi_is_available();

#endif
