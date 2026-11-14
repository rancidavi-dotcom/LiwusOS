#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Inicializacao do subsistema de rede e pilha lwIP */
int net_init(void);

/* Polling de pacotes recebidos e timers (chamado pelo kernel ou timer) */
void net_poll(void);

/* Consultas de status para o shell/userspace */
bool net_is_active(void);
bool net_has_ip(void);
void net_get_ip_strings(char *ip, char *netmask, char *gateway);
int net_set_static_ip(const char *ip_str, const char *mask_str, const char *gw_str);
int net_dhcp_renew(void);
const char *net_get_driver_name(void);
const uint8_t *net_get_mac(void);
bool net_get_link_status(void);
void net_get_packet_counts(uint32_t *rx, uint32_t *tx);

/* Utilitario simples de ping ICMP */
int net_ping(const char *target_ip_str, uint32_t count);

typedef void (*net_ping_output_fn)(const char *line, void *user_data);

int net_ping_custom(const char *target_ip_str, uint32_t count,
                    net_ping_output_fn output_cb, void *user_data);

/* Resolucao DNS */
int net_resolve_host(const char *hostname, char *ip_str_out, size_t max_len);
void net_get_dns_server(char *dns_ip_out, size_t max_len);

/* Sincronizacao global de acesso a pilha lwIP e drivers de rede */
uint64_t net_lock_acquire(void);
void net_lock_release(uint64_t flags);

#endif /* NET_MANAGER_H */
