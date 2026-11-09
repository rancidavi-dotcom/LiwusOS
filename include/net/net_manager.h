#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

/* Inicializacao do subsistema de rede e pilha lwIP */
int net_init(void);

/* Polling de pacotes recebidos e timers (chamado pelo kernel ou timer) */
void net_poll(void);

/* Consultas de status para o shell/userspace */
bool net_is_active(void);
bool net_has_ip(void);
void net_get_ip_strings(char *ip, char *netmask, char *gateway);
const char *net_get_driver_name(void);
const uint8_t *net_get_mac(void);
bool net_get_link_status(void);
void net_get_packet_counts(uint32_t *rx, uint32_t *tx);

/* Utilitario simples de ping ICMP */
int net_ping(const char *target_ip_str, uint32_t count);

#endif /* NET_MANAGER_H */
