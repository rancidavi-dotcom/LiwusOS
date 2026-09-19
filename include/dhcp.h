#ifndef DHCP_H
#define DHCP_H

#include <stdint.h>

void dhcp_init(void);
void dhcp_discover(void);
int dhcp_has_bound(void);
void create_dhcp_config_task(void);

#endif
