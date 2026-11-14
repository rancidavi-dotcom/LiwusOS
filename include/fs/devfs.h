#ifndef LIWUS_DEVFS_H
#define LIWUS_DEVFS_H

#include "fs/vfs.h"

/* Inicializa o sistema de dispositivos virtuais dev: */
fs_node_t *devfs_init(void);

#endif
