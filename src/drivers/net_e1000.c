#include "net_e1000.h"
#include "pci.h"
#include "kheap.h"
#include "vmm.h"
#include "kernel_layout.h"
#include "serial.h"
#include "string.h"

/* Registradores MMIO da Intel 82540EM / e1000 */
#define REG_CTRL       0x0000
#define REG_STATUS     0x0008
#define REG_EECD       0x0010
#define REG_EERD       0x0014
#define REG_ICR        0x00C0
#define REG_ITR        0x00C4
#define REG_ICS        0x00C8
#define REG_IMS        0x00D0
#define REG_IMC        0x00D8
#define REG_RCTL       0x0100
#define REG_TCTL       0x0400
#define REG_TIPG       0x0410
#define REG_RDBAL      0x2800
#define REG_RDBAH      0x2804
#define REG_RDLEN      0x2808
#define REG_RDH        0x2810
#define REG_RDT        0x2818
#define REG_TDBAL      0x3800
#define REG_TDBAH      0x3804
#define REG_TDLEN      0x3808
#define REG_TDH        0x3810
#define REG_TDT        0x3818
#define REG_RAL        0x5400
#define REG_RAH        0x5404

/* Bits de Device Control (REG_CTRL) */
#define E1000_CTRL_SLU  (1 << 6)   /* Set Link Up */
#define E1000_CTRL_RST  (1 << 26)  /* Software Reset */

/* Bits de Receive Control (REG_RCTL) */
#define E1000_RCTL_EN             (1 << 1)   /* Receiver Enable */
#define E1000_RCTL_SBP            (1 << 2)   /* Store Bad Packets */
#define E1000_RCTL_UPE            (1 << 3)   /* Unicast Promiscuous Enabled */
#define E1000_RCTL_MPE            (1 << 4)   /* Multicast Promiscuous Enabled */
#define E1000_RCTL_LPE            (1 << 5)   /* Long Packet Reception Enable */
#define E1000_RCTL_BAM            (1 << 15)  /* Broadcast Accept Mode */
#define E1000_RCTL_BSIZE_2048     (0 << 16)  /* Buffer size: 2048 bytes */
#define E1000_RCTL_SECRC          (1 << 26)  /* Strip Ethernet CRC */

/* Bits de Transmit Control (REG_TCTL) */
#define E1000_TCTL_EN             (1 << 1)   /* Transmit Enable */
#define E1000_TCTL_PSP            (1 << 3)   /* Pad Short Packets */
#define E1000_TCTL_CT_SHIFT       4          /* Collision Threshold */
#define E1000_TCTL_COLD_SHIFT     12         /* Collision Distance */

/* Descritores de RX (16 bytes) */
struct e1000_rx_desc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint16_t checksum;
    volatile uint8_t  status;
    volatile uint8_t  errors;
    volatile uint16_t special;
} __attribute__((packed));

#define E1000_RXD_STAT_DD   (1 << 0)  /* Descriptor Done */
#define E1000_RXD_STAT_EOP  (1 << 1)  /* End of Packet */

/* Descritores de TX (16 bytes) */
struct e1000_tx_desc {
    volatile uint64_t addr;
    volatile uint16_t length;
    volatile uint8_t  cso;
    volatile uint8_t  cmd;
    volatile uint8_t  status;
    volatile uint8_t  css;
    volatile uint16_t special;
} __attribute__((packed));

#define E1000_TXD_CMD_EOP   (1 << 0)  /* End of Packet */
#define E1000_TXD_CMD_IFCS  (1 << 1)  /* Insert FCS/CRC */
#define E1000_TXD_CMD_RS    (1 << 3)  /* Report Status */
#define E1000_TXD_STAT_DD   (1 << 0)  /* Descriptor Done */

/* Estado interno do driver */
static volatile uint8_t *e1000_mmio = 0;
static bool e1000_initialized = false;
static uint8_t mac_address[6] = {0};
static e1000_stats_t e1000_stats = {0};

/* Anéis de descritores e buffers em memória de DMA contígua */
static struct e1000_rx_desc *rx_descs = 0;
static uint64_t rx_descs_phys = 0;
static void *rx_buffers[E1000_NUM_RX_DESC] = {0};
static uint32_t rx_cur = 0;

static struct e1000_tx_desc *tx_descs = 0;
static uint64_t tx_descs_phys = 0;
static void *tx_buffers[E1000_NUM_TX_DESC] = {0};
static uint32_t tx_cur = 0;

/* Leitura e escrita MMIO */
static inline void e1000_write(uint32_t reg, uint32_t val) {
    *((volatile uint32_t *)(e1000_mmio + reg)) = val;
}

static inline uint32_t e1000_read(uint32_t reg) {
    return *((volatile uint32_t *)(e1000_mmio + reg));
}

/* Leitura da EEPROM da placa */
static uint16_t e1000_read_eeprom(uint8_t addr) {
    uint32_t val = (1) | ((uint32_t)addr << 8);
    e1000_write(REG_EERD, val);

    /* Espera o bit 4 (Done) */
    uint32_t timeout = 50000;
    while (timeout--) {
        uint32_t r = e1000_read(REG_EERD);
        if (r & (1 << 4)) {
            return (uint16_t)((r >> 16) & 0xFFFF);
        }
    }
    return 0;
}

/* Leitura do MAC Address da placa */
static void e1000_read_mac(void) {
    /* Primeiro tenta ler dos registradores Receive Address (RAL0/RAH0) */
    uint32_t ral = e1000_read(REG_RAL);
    uint32_t rah = e1000_read(REG_RAH);

    if (ral != 0 || (rah & 0xFFFF) != 0) {
        mac_address[0] = (uint8_t)(ral & 0xFF);
        mac_address[1] = (uint8_t)((ral >> 8) & 0xFF);
        mac_address[2] = (uint8_t)((ral >> 16) & 0xFF);
        mac_address[3] = (uint8_t)((ral >> 24) & 0xFF);
        mac_address[4] = (uint8_t)(rah & 0xFF);
        mac_address[5] = (uint8_t)((rah >> 8) & 0xFF);
    } else {
        /* Fallback: le da EEPROM interna */
        uint16_t w0 = e1000_read_eeprom(0);
        uint16_t w1 = e1000_read_eeprom(1);
        uint16_t w2 = e1000_read_eeprom(2);

        mac_address[0] = (uint8_t)(w0 & 0xFF);
        mac_address[1] = (uint8_t)((w0 >> 8) & 0xFF);
        mac_address[2] = (uint8_t)(w1 & 0xFF);
        mac_address[3] = (uint8_t)((w1 >> 8) & 0xFF);
        mac_address[4] = (uint8_t)(w2 & 0xFF);
        mac_address[5] = (uint8_t)((w2 >> 8) & 0xFF);
    }

    memcpy(e1000_stats.mac_addr, mac_address, 6);
}

/* Inicializacao do anel de recepcao (RX) */
static int e1000_init_rx(void) {
    rx_descs = (struct e1000_rx_desc *)kmalloc_a(
        E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    if (!rx_descs) return -1;

    memset(rx_descs, 0, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    rx_descs_phys = virt_to_phys(rx_descs);

    for (int i = 0; i < E1000_NUM_RX_DESC; i++) {
        rx_buffers[i] = kmalloc_a(E1000_BUFFER_SIZE);
        if (!rx_buffers[i]) return -2;

        memset(rx_buffers[i], 0, E1000_BUFFER_SIZE);
        rx_descs[i].addr = virt_to_phys(rx_buffers[i]);
        rx_descs[i].status = 0;
    }

    e1000_write(REG_RDBAL, (uint32_t)(rx_descs_phys & 0xFFFFFFFF));
    e1000_write(REG_RDBAH, (uint32_t)(rx_descs_phys >> 32));
    e1000_write(REG_RDLEN, E1000_NUM_RX_DESC * sizeof(struct e1000_rx_desc));
    e1000_write(REG_RDH, 0);
    e1000_write(REG_RDT, E1000_NUM_RX_DESC - 1);

    /* Habilita recepcao com Broadcast, Multicast e Unicast promiscuo */
    uint32_t rctl = E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_MPE |
                    E1000_RCTL_UPE | E1000_RCTL_BSIZE_2048 | E1000_RCTL_SECRC;
    e1000_write(REG_RCTL, rctl);

    rx_cur = 0;
    return 0;
}

/* Inicializacao do anel de transmissao (TX) */
static int e1000_init_tx(void) {
    tx_descs = (struct e1000_tx_desc *)kmalloc_a(
        E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    if (!tx_descs) return -1;

    memset(tx_descs, 0, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    tx_descs_phys = virt_to_phys(tx_descs);

    for (int i = 0; i < E1000_NUM_TX_DESC; i++) {
        tx_buffers[i] = kmalloc_a(E1000_BUFFER_SIZE);
        if (!tx_buffers[i]) return -2;

        memset(tx_buffers[i], 0, E1000_BUFFER_SIZE);
        tx_descs[i].addr = virt_to_phys(tx_buffers[i]);
        tx_descs[i].cmd = 0;
        tx_descs[i].status = E1000_TXD_STAT_DD; /* Pronto para uso inicial */
    }

    e1000_write(REG_TDBAL, (uint32_t)(tx_descs_phys & 0xFFFFFFFF));
    e1000_write(REG_TDBAH, (uint32_t)(tx_descs_phys >> 32));
    e1000_write(REG_TDLEN, E1000_NUM_TX_DESC * sizeof(struct e1000_tx_desc));
    e1000_write(REG_TDH, 0);
    e1000_write(REG_TDT, 0);

    /* Configura espaco inter-pacote padrao (IPG recomendado pelo manual) */
    e1000_write(REG_TIPG, 0x0060200A);

    /* Habilita transmissao com padding e limites de colisao padrao */
    uint32_t tctl = E1000_TCTL_EN | E1000_TCTL_PSP |
                    (0x0F << E1000_TCTL_CT_SHIFT) |
                    (0x40 << E1000_TCTL_COLD_SHIFT);
    e1000_write(REG_TCTL, tctl);

    tx_cur = 0;
    return 0;
}

/* Inicializa a placa de rede e1000 */
int e1000_init(void) {
    pci_device_t *pci = pci_get_net();
    if (!pci) {
        serial_print("[e1000] Nenhuma placa de rede Ethernet encontrada no barramento PCI.\n");
        return -1;
    }

    /* Verifica se e fabricante Intel (0x8086) */
    if (pci->vendor_id != 0x8086) {
        serial_print("[e1000] Placa de rede detectada nao e Intel e1000 (VID: ");
        serial_print_hex(pci->vendor_id);
        serial_print(" DID: ");
        serial_print_hex(pci->device_id);
        serial_print(")\n");
        return -2;
    }

    /* Le o BAR0 para obter a regiao de registradores MMIO */
    uint32_t bar0 = pci_read_config(pci->bus, pci->device, pci->function, 0x10);
    if (bar0 & 1) {
        serial_print("[e1000] BAR0 e I/O Port, esperado MMIO.\n");
        return -3;
    }

    uintptr_t mmio_phys = (uintptr_t)(bar0 & ~0xF);
    if ((bar0 & 0x6) == 0x4) {
        /* BAR0 de 64 bits */
        uint32_t bar0_high = pci_read_config(pci->bus, pci->device, pci->function, 0x14);
        mmio_phys |= ((uintptr_t)bar0_high << 32);
    }

    /* Mapeia a regiao MMIO de 128KB no direct-map virtual do kernel */
    for (uintptr_t p = 0; p < 0x20000; p += 4096) {
        vmm_map_page_in(kernel_directory,
                        (void *)(mmio_phys + p),
                        phys_to_virt(mmio_phys + p),
                        PTE_P | PTE_W | PTE_PCD | PTE_NX);
    }

    e1000_mmio = (volatile uint8_t *)phys_to_virt(mmio_phys);

    /* Habilita Bus Master e MMIO no Command Register do PCI */
    uint16_t pci_cmd = (uint16_t)pci_read_config(pci->bus, pci->device, pci->function, 0x04);
    pci_cmd |= (1 << 1) | (1 << 2); /* Memory Space Enable + Bus Master */
    pci_write_config(pci->bus, pci->device, pci->function, 0x04, pci_cmd);

    /* Desativa interrupcoes de hardware durante o setup (usaremos polling) */
    e1000_write(REG_IMC, 0xFFFFFFFF);
    (void)e1000_read(REG_ICR);

    /* Levanta o link da placa (Set Link Up) */
    uint32_t ctrl = e1000_read(REG_CTRL);
    ctrl |= E1000_CTRL_SLU;
    ctrl &= ~E1000_CTRL_RST;
    e1000_write(REG_CTRL, ctrl);

    /* Le o endereco MAC */
    e1000_read_mac();

    /* Garante que o registrador Receive Address tem o MAC e bit AV (Address Valid) setado */
    uint32_t ral = mac_address[0] | ((uint32_t)mac_address[1] << 8) |
                   ((uint32_t)mac_address[2] << 16) | ((uint32_t)mac_address[3] << 24);
    uint32_t rah = mac_address[4] | ((uint32_t)mac_address[5] << 8) | (1U << 31);
    e1000_write(REG_RAL, ral);
    e1000_write(REG_RAH, rah);

    /* Inicializa aneis de RX e TX */
    if (e1000_init_rx() != 0) {
        serial_print("[e1000] Falha ao alocar anel RX.\n");
        return -4;
    }

    if (e1000_init_tx() != 0) {
        serial_print("[e1000] Falha ao alocar anel TX.\n");
        return -5;
    }

    uint32_t status = e1000_read(REG_STATUS);
    e1000_stats.link_up = (status & (1 << 1)) != 0;

    e1000_initialized = true;

    serial_print("[e1000] Placa Intel e1000 inicializada com sucesso!\n");
    serial_print("[e1000] MAC: ");
    static const char hex_chars[] = "0123456789abcdef";
    for (int i = 0; i < 6; i++) {
        char b[3];
        b[0] = hex_chars[(mac_address[i] >> 4) & 0xF];
        b[1] = hex_chars[mac_address[i] & 0xF];
        b[2] = '\0';
        serial_print(b);
        if (i < 5) serial_print(":");
    }
    serial_print(" | Link: ");
    serial_print(e1000_stats.link_up ? "UP\n" : "DOWN\n");

    return 0;
}

/* Envio de pacote Ethernet bruto */
int e1000_send_packet(const void *data, uint16_t len) {
    if (!e1000_initialized || !data || len == 0 || len > E1000_BUFFER_SIZE) {
        return -1;
    }

    /* Verifica se o descritor atual ja foi processado pelo hardware */
    if (!(tx_descs[tx_cur].status & E1000_TXD_STAT_DD)) {
        /* Buffer cheio / hardware ocupado */
        return -2;
    }

    /* Copia os dados para o buffer de DMA contiguo do descritor */
    memcpy(tx_buffers[tx_cur], data, len);

    tx_descs[tx_cur].length = len;
    tx_descs[tx_cur].cmd = E1000_TXD_CMD_EOP | E1000_TXD_CMD_IFCS | E1000_TXD_CMD_RS;
    tx_descs[tx_cur].status = 0;

    /* Avanca o anel de TX */
    uint32_t prev = tx_cur;
    tx_cur = (tx_cur + 1) % E1000_NUM_TX_DESC;

    /* Notifica o hardware atualizando o registrador Tail (TDT) */
    e1000_write(REG_TDT, tx_cur);

    e1000_stats.packets_tx++;
    e1000_stats.bytes_tx += len;

    (void)prev;
    return 0;
}

/* Verificacao e recepcao de pacote Ethernet */
int e1000_poll_packet(void *out_buffer, uint16_t max_len) {
    if (!e1000_initialized || !out_buffer || max_len == 0) {
        return 0;
    }

    /* Verifica se o descritor RX atual recebeu dados do hardware */
    if (!(rx_descs[rx_cur].status & E1000_RXD_STAT_DD)) {
        return 0; /* Nenhum pacote disponivel */
    }

    uint16_t len = rx_descs[rx_cur].length;
    if (len > max_len) {
        len = max_len;
    }

    /* Copia os dados do buffer de DMA para a pilha de rede */
    memcpy(out_buffer, rx_buffers[rx_cur], len);

    /* Devolve o descritor para o hardware */
    rx_descs[rx_cur].status = 0;
    uint32_t old_cur = rx_cur;
    rx_cur = (rx_cur + 1) % E1000_NUM_RX_DESC;

    /* Atualiza o registrador RDT informando o buffer devolvido */
    e1000_write(REG_RDT, old_cur);

    e1000_stats.packets_rx++;
    e1000_stats.bytes_rx += len;

    return len;
}

bool e1000_is_available(void) {
    return e1000_initialized;
}

const uint8_t *e1000_get_mac(void) {
    return mac_address;
}

void e1000_get_stats(e1000_stats_t *out_stats) {
    if (out_stats) {
        if (e1000_initialized) {
            uint32_t status = e1000_read(REG_STATUS);
            e1000_stats.link_up = (status & (1 << 1)) != 0;
        }
        memcpy(out_stats, &e1000_stats, sizeof(e1000_stats_t));
    }
}
