#ifndef NET_RTL8168_H
#define NET_RTL8168_H

#include <stdint.h>
#include <stdbool.h>

#define RTL8168_NUM_RX_DESC 128
#define RTL8168_NUM_TX_DESC 32
#define RTL8168_BUFFER_SIZE 2048

/* Informacoes e estatisticas da interface RTL8168 */
typedef struct {
    uint8_t mac_addr[6];
    bool link_up;
    uint32_t packets_rx;
    uint32_t packets_tx;
    uint32_t bytes_rx;
    uint32_t bytes_tx;
} rtl8168_stats_t;

/* Inicializacao do driver Realtek RTL8168 / RTL8111 */
int rtl8168_init(void);

/* Envio e recepcao de pacotes Ethernet brutos */
int rtl8168_send_packet(const void *data, uint16_t len);
int rtl8168_poll_packet(void *out_buffer, uint16_t max_len);

/* Metadados e estado */
bool rtl8168_is_available(void);
const uint8_t *rtl8168_get_mac(void);
bool rtl8168_is_link_up(void);
void rtl8168_get_stats(rtl8168_stats_t *out_stats);

#endif /* NET_RTL8168_H */
