#ifndef NET_E1000_H
#define NET_E1000_H

#include <stdint.h>
#include <stdbool.h>

#define E1000_NUM_RX_DESC 32
#define E1000_NUM_TX_DESC 32
#define E1000_BUFFER_SIZE 2048

/* Informacoes da interface e1000 */
typedef struct {
    uint8_t mac_addr[6];
    bool link_up;
    uint32_t packets_rx;
    uint32_t packets_tx;
    uint32_t bytes_rx;
    uint32_t bytes_tx;
} e1000_stats_t;

/* Inicializacao do driver */
int e1000_init(void);

/* Envio e recepcao de pacotes Ethernet brutos */
int e1000_send_packet(const void *data, uint16_t len);
int e1000_poll_packet(void *out_buffer, uint16_t max_len);

/* Metadados e estado */
bool e1000_is_available(void);
const uint8_t *e1000_get_mac(void);
void e1000_get_stats(e1000_stats_t *out_stats);

#endif /* NET_E1000_H */
