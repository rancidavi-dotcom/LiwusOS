#ifndef LIWUS_IPCFS_H
#define LIWUS_IPCFS_H

#include "fs/vfs.h"

/* Inicializa o sistema de canais nomeados de comunicacao ipc: */
fs_node_t *ipcfs_init(void);

#endif
