#ifndef R8169_H
#define R8169_H

#include <stdint.h>
#include "pci.h"

void init_r8169(pci_device_t *dev);
void r8169_handler(void);

#endif