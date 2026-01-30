#include "wifi.h"
#include "net.h"
#include "string.h"
#include "kheap.h"

static net_interface_t wifi_netif;
static char current_ssid[33] = "Desconectado";
static int has_wifi_hardware = 0;

void wifi_send_packet(net_interface_t* self, void* data, uint32_t len) {
    (void)self; (void)data; (void)len;
}

void wifi_init(pci_device_t* dev) {
    if (!dev) {
        has_wifi_hardware = 0;
        return;
    }
    
    has_wifi_hardware = 1;
    strcpy(wifi_netif.name, "wlan0");
    wifi_netif.type = NET_TYPE_WIFI;
    wifi_netif.send_packet = wifi_send_packet;
    
    // MAC real viria do hardware, aqui usamos um placeholder para a interface ativa
    wifi_netif.mac[0] = 0x00; wifi_netif.mac[1] = 0x16;
    wifi_netif.mac[2] = 0xEA; wifi_netif.mac[3] = 0xAE;
    wifi_netif.mac[4] = 0x12; wifi_netif.mac[5] = 0x34;

    net_register_interface(&wifi_netif);
}

int wifi_scan(wifi_network_t* networks, int max_networks) {
    (void)networks; (void)max_networks;
    // Retorna 0 redes se for apenas um stub sem driver de hardware real
    return 0; 
}

void wifi_connect(const char* ssid, const char* password) {
    if (!has_wifi_hardware) return;
    (void)password;
    strncpy(current_ssid, ssid, 32);
}

const char* wifi_get_current_ssid() {
    return current_ssid;
}

int wifi_is_available() {
    return has_wifi_hardware;
}