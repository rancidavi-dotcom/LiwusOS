#include "net.h"
#include "kheap.h"
#include <stddef.h>

static net_interface_t* interfaces = NULL;

void net_init() {
    interfaces = NULL;
}

void net_register_interface(net_interface_t* netif) {
    if (interfaces == NULL) {
        interfaces = netif;
    } else {
        net_interface_t* curr = interfaces;
        while (curr->next != NULL) curr = curr->next;
        curr->next = netif;
    }
}

net_interface_t* net_get_list() {
    return interfaces;
}
