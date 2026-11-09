#ifndef LWIP_LWIPOPTS_H
#define LWIP_LWIPOPTS_H

/* Modo standalone / cooperativo no kernel */
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

/* Alinhamento de memoria para x86_64 */
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (32 * 1024)

/* Pools de buffers e controle */
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_RAW_PCB                4
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         8
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_SYS_TIMEOUT            16

/* Configuracao de pacotes de rede (PBUF) */
#define PBUF_POOL_SIZE                  16
#define PBUF_POOL_BUFSIZE               1536

/* Protocolos suportados */
#define LWIP_ARP                        1
#define ARP_QUEUEING                    1
#define LWIP_ETHERNET                   1
#define LWIP_IPV4                       1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_DHCP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_DNS                        1

/* Callbacks de status e link */
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1

/* Desativa estatisticas pesadas e debug verboso */
#define LWIP_STATS                      0
#define LWIP_DEBUG                      0

/* Checksums de rede gerenciados por software */
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1
#define CHECKSUM_CHECK_ICMP             1

#endif /* LWIP_LWIPOPTS_H */
