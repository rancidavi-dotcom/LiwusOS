#ifndef NET_TELNET_SERVER_H
#define NET_TELNET_SERVER_H

#include <stdint.h>
#include <stdbool.h>

/* Inicializa o servidor Telnet na porta especificada (padrao 23).
 * Retorna 0 em sucesso ou codigo de erro negativo. */
int telnet_server_init(uint16_t port);

/* Para o servidor Telnet e encerra conexoes ativas */
void telnet_server_stop(void);

/* Retorna se o servidor Telnet esta escutando conexoes */
bool telnet_server_is_active(void);

#endif /* NET_TELNET_SERVER_H */
