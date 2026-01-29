#ifndef NET_H
#define NET_H

#include <stdint.h>

typedef enum {
    NET_TYPE_ETHERNET,
    NET_TYPE_WIFI
} net_type_t;

typedef struct net_interface {
    char name[16];
    net_type_t type;
    uint8_t mac[6];
    void (*send_packet)(struct net_interface* self, void* data, uint32_t len);
    void* driver_data;
    struct net_interface* next;
} net_interface_t;

void net_init();
void net_register_interface(net_interface_t* netif);
net_interface_t* net_get_list();

#endif
