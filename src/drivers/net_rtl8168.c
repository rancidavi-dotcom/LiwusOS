#include "net_rtl8168.h"
#include "pci.h"
#include "kheap.h"
#include "vmm.h"
#include "kernel_layout.h"
#include "serial.h"
#include "string.h"
#include "io.h"

/* Registradores da Realtek RTL8168 / RTL8111 */
#define RTL_REG_MAC0         0x00
#define RTL_REG_MAR0         0x08
#define RTL_REG_TX_DESC_LO   0x20
#define RTL_REG_TX_DESC_HI   0x24
#define RTL_REG_CHIPCMD      0x37
#define RTL_REG_TXPOLL       0x38
#define RTL_REG_IMR          0x3C
#define RTL_REG_ISR          0x3E
#define RTL_REG_TCR          0x40
#define RTL_REG_RCR          0x44
#define RTL_REG_CFG9346      0x50
#define RTL_REG_PHYSTATUS    0x6C
#define RTL_REG_RMS          0xDA
#define RTL_REG_CPLUSCMD     0xE0
#define RTL_REG_RX_DESC_LO   0xE4
#define RTL_REG_RX_DESC_HI   0xE8
#define RTL_REG_MTPS         0xEC

/* Bits de Comando */
#define RTL_CMD_RST          (1 << 4)  /* Software Reset */
#define RTL_CMD_RE           (1 << 3)  /* Receiver Enable */
#define RTL_CMD_TE           (1 << 2)  /* Transmitter Enable */

/* Cfg9346 (0x50) */
#define RTL_CFG9346_LOCK     0x00
#define RTL_CFG9346_UNLOCK   0xC0

/* CPlusCmd (0xE0) */
#define RTL_CPLUS_PCIMULRW   (1 << 3)
#define RTL_CPLUS_PCIDAC     (1 << 4)

/* Bits de ISR (0x3E) */
#define RTL_ISR_RDU          (1 << 4)  /* Rx Descriptor Unavailable */
#define RTL_ISR_FOVW         (1 << 6)  /* Rx FIFO Overflow */

/* Bits de Descritores (opts1) */
#define RTL_DESC_OWN         (1U << 31) /* 1 = Placa possui, 0 = CPU possui */
#define RTL_DESC_EOR         (1U << 30) /* Fim do anel (End of Ring) */
#define RTL_DESC_FS          (1U << 29) /* Primeiro segmento do pacote */
#define RTL_DESC_LS          (1U << 28) /* Ultimo segmento do pacote */
#define RTL_DESC_RX_RES      (1U << 21) /* Receive Error Summary */

/* Estrutura de Descritor RTL8168 (16 bytes) */
struct rtl8168_desc {
    volatile uint32_t opts1;
    volatile uint32_t opts2;
    volatile uint64_t addr;
} __attribute__((packed));

/* Estado interno do driver */
static volatile uint8_t *rtl_mmio = 0;
static uint16_t rtl_io_base = 0;
static bool rtl_use_mmio = false;
static bool rtl_need_dac = false;
static bool rtl_initialized = false;
static uint8_t mac_address[6] = {0};
static rtl8168_stats_t rtl_stats = {0};

/* Anéis de descritores e buffers contíguos na RAM */
static struct rtl8168_desc *rx_descs = 0;
static void *rx_buffers[RTL8168_NUM_RX_DESC] = {0};
static uint32_t rx_cur = 0;

static struct rtl8168_desc *tx_descs = 0;
static void *tx_buffers[RTL8168_NUM_TX_DESC] = {0};
static uint32_t tx_cur = 0;

/* Leitura e escrita agnóstica de MMIO / I/O */
static inline void rtl_write8(uint16_t reg, uint8_t val) {
    if (rtl_use_mmio) *(volatile uint8_t *)(rtl_mmio + reg) = val;
    else outb(rtl_io_base + reg, val);
}

static inline void rtl_write16(uint16_t reg, uint16_t val) {
    if (rtl_use_mmio) *(volatile uint16_t *)(rtl_mmio + reg) = val;
    else outw(rtl_io_base + reg, val);
}

static inline void rtl_write32(uint16_t reg, uint32_t val) {
    if (rtl_use_mmio) *(volatile uint32_t *)(rtl_mmio + reg) = val;
    else outl(rtl_io_base + reg, val);
}

static inline uint8_t rtl_read8(uint16_t reg) {
    if (rtl_use_mmio) return *(volatile uint8_t *)(rtl_mmio + reg);
    return inb(rtl_io_base + reg);
}

static inline uint16_t rtl_read16(uint16_t reg) {
    if (rtl_use_mmio) return *(volatile uint16_t *)(rtl_mmio + reg);
    return inw(rtl_io_base + reg);
}

static inline uint32_t rtl_read32(uint16_t reg) {
    if (rtl_use_mmio) return *(volatile uint32_t *)(rtl_mmio + reg);
    return inl(rtl_io_base + reg);
}

/* Leitura do MAC Address físico da placa */
static void rtl8168_read_mac(void) {
    for (int i = 0; i < 6; i++) {
        mac_address[i] = rtl_read8(RTL_REG_MAC0 + i);
    }
    memcpy(rtl_stats.mac_addr, mac_address, 6);
}

/* Inicializa o anel de recepção (RX) */
static int rtl8168_init_rx(void) {
    rx_descs = (struct rtl8168_desc *)kmalloc_a(
        RTL8168_NUM_RX_DESC * sizeof(struct rtl8168_desc));
    if (!rx_descs) return -1;

    memset(rx_descs, 0, RTL8168_NUM_RX_DESC * sizeof(struct rtl8168_desc));

    for (int i = 0; i < RTL8168_NUM_RX_DESC; i++) {
        rx_buffers[i] = kmalloc_a(RTL8168_BUFFER_SIZE);
        if (!rx_buffers[i]) return -2;

        memset(rx_buffers[i], 0, RTL8168_BUFFER_SIZE);
        uint64_t buf_phys = virt_to_phys(rx_buffers[i]);
        if (buf_phys > 0xFFFFFFFFULL) rtl_need_dac = true;
        rx_descs[i].addr = buf_phys;

        uint32_t opts = RTL_DESC_OWN | (RTL8168_BUFFER_SIZE & 0x3FFF);
        if (i == RTL8168_NUM_RX_DESC - 1) {
            opts |= RTL_DESC_EOR; /* Marca o último do anel circular */
        }
        rx_descs[i].opts2 = 0;
        rx_descs[i].opts1 = opts;
    }

    uint64_t rx_phys = virt_to_phys(rx_descs);
    if (rx_phys > 0xFFFFFFFFULL) rtl_need_dac = true;
    __asm__ volatile("mfence" ::: "memory");
    rtl_write32(RTL_REG_RX_DESC_HI, (uint32_t)(rx_phys >> 32));
    rtl_write32(RTL_REG_RX_DESC_LO, (uint32_t)(rx_phys & 0xFFFFFFFF));

    rx_cur = 0;
    return 0;
}

/* Inicializa o anel de transmissão (TX) */
static int rtl8168_init_tx(void) {
    tx_descs = (struct rtl8168_desc *)kmalloc_a(
        RTL8168_NUM_TX_DESC * sizeof(struct rtl8168_desc));
    if (!tx_descs) return -1;

    memset(tx_descs, 0, RTL8168_NUM_TX_DESC * sizeof(struct rtl8168_desc));

    for (int i = 0; i < RTL8168_NUM_TX_DESC; i++) {
        tx_buffers[i] = kmalloc_a(RTL8168_BUFFER_SIZE);
        if (!tx_buffers[i]) return -2;

        memset(tx_buffers[i], 0, RTL8168_BUFFER_SIZE);
        uint64_t buf_phys = virt_to_phys(tx_buffers[i]);
        if (buf_phys > 0xFFFFFFFFULL) rtl_need_dac = true;
        tx_descs[i].addr = buf_phys;

        uint32_t opts = 0;
        if (i == RTL8168_NUM_TX_DESC - 1) {
            opts |= RTL_DESC_EOR;
        }
        tx_descs[i].opts2 = 0;
        tx_descs[i].opts1 = opts;
    }

    uint64_t tx_phys = virt_to_phys(tx_descs);
    if (tx_phys > 0xFFFFFFFFULL) rtl_need_dac = true;
    __asm__ volatile("mfence" ::: "memory");
    rtl_write32(RTL_REG_TX_DESC_HI, (uint32_t)(tx_phys >> 32));
    rtl_write32(RTL_REG_TX_DESC_LO, (uint32_t)(tx_phys & 0xFFFFFFFF));

    tx_cur = 0;
    return 0;
}

/* Inicialização da Realtek RTL8168 / RTL8111 */
int rtl8168_init(void) {
    /* Procura especificamente a placa Realtek 0x10EC:0x8168 ou dispositivo de rede Realtek */
    pci_device_t *pci = pci_get_device(0x10EC, 0x8168);
    if (!pci) {
        /* Fallback: procura qualquer placa de rede Realtek (subclass Ethernet) */
        pci_device_t *net = pci_get_net();
        if (net && net->vendor_id == 0x10EC) {
            pci = net;
        }
    }

    if (!pci) {
        return -1; /* Placa Realtek nao presente */
    }

    serial_print("[RTL8168] Controladora Realtek PCIe GBE encontrada!\n");

    /* Habilita Bus Mastering e MMIO/IO no PCI Command Register */
    uint32_t pci_cmd = pci_read_config(pci->bus, pci->device, pci->function, 0x04);
    pci_cmd |= (1U << 0) | (1U << 1) | (1U << 2); /* I/O + Memory + Bus Master */
    pci_write_config(pci->bus, pci->device, pci->function, 0x04, pci_cmd);

    /* Varrer BARs para obter MMIO ou I/O base */
    rtl_use_mmio = false;
    rtl_io_base = 0;
    rtl_mmio = 0;
    rtl_need_dac = false;
    memset(&rtl_stats, 0, sizeof(rtl_stats));

    for (int bar_idx = 0; bar_idx < 6; bar_idx++) {
        uint32_t bar = pci_read_config(pci->bus, pci->device, pci->function, 0x10 + bar_idx * 4);
        if (bar == 0 || bar == 0xFFFFFFFF) continue;

        if (bar & 1) {
            /* I/O Port */
            if (rtl_io_base == 0) {
                rtl_io_base = (uint16_t)(bar & ~3U);
            }
        } else {
            /* MMIO Memory BAR */
            uintptr_t mmio_phys = (uintptr_t)(bar & ~0xFULL);
            if ((bar & 0x6) == 0x4 && bar_idx < 5) {
                /* 64-bit BAR: consome o proximo slot */
                bar_idx++;
                uint32_t bar_high = pci_read_config(pci->bus, pci->device, pci->function, 0x10 + bar_idx * 4);
                mmio_phys |= ((uintptr_t)bar_high << 32);
            }

            if (mmio_phys != 0 && !rtl_use_mmio) {
                uint64_t mmio_page = (uint64_t)(mmio_phys & ~0xFFFULL);
                if (vmm_map_kernel_page(mmio_page,
                                        (uint64_t)phys_to_virt(mmio_page),
                                        PTE_P | PTE_W | PTE_PCD | PTE_NX) == 0) {
                    rtl_mmio = (volatile uint8_t *)phys_to_virt(mmio_phys);
                    rtl_use_mmio = true;
                }
            }
        }
    }

    if (!rtl_use_mmio && rtl_io_base == 0) {
        serial_print("[RTL8168] Falha ao encontrar BAR de MMIO ou I/O.\n");
        return -2;
    }

    serial_print("[RTL8168] Modo de acesso: ");
    serial_print(rtl_use_mmio ? "MMIO\n" : "I/O Ports\n");

    /* 1. Software Reset */
    rtl_write8(RTL_REG_CHIPCMD, RTL_CMD_RST);
    uint32_t timeout = 50000;
    while ((rtl_read8(RTL_REG_CHIPCMD) & RTL_CMD_RST) && timeout--) {
        asm volatile("pause");
    }

    /* 2. Desativa interrupções de hardware (usaremos polling cooperativo) */
    rtl_write16(RTL_REG_IMR, 0x0000);
    rtl_write16(RTL_REG_ISR, 0xFFFF);

    /* 3. Lê o MAC Address */
    rtl8168_read_mac();
    bool mac_valid = false;
    for (int i = 0; i < 6; i++) {
        if (mac_address[i] != 0) {
            mac_valid = true;
            break;
        }
    }

    /* 4. Destrava registradores de configuracao protegidos (Cfg9346) */
    rtl_write8(RTL_REG_CFG9346, RTL_CFG9346_UNLOCK);

    /* 5. Aloca e inicializa anéis de RX e TX */
    if (rtl8168_init_rx() != 0) {
        rtl_write8(RTL_REG_CFG9346, RTL_CFG9346_LOCK);
        serial_print("[RTL8168] Falha ao alocar anel RX.\n");
        return -3;
    }

    if (rtl8168_init_tx() != 0) {
        rtl_write8(RTL_REG_CFG9346, RTL_CFG9346_LOCK);
        serial_print("[RTL8168] Falha ao alocar anel TX.\n");
        return -4;
    }

    /* 6. Limpa residuos UEFI/PXE em CPlusCmd e configura PCI Multiple R/W (+DAC se DMA > 4GB) */
    uint16_t cplus = RTL_CPLUS_PCIMULRW;
    if (rtl_need_dac) {
        cplus |= RTL_CPLUS_PCIDAC;
    }
    rtl_write16(RTL_REG_CPLUSCMD, cplus);

    /* 7. Configura tamanho máximo de pacote (RMS=2048, MTPS=0x3B) */
    rtl_write16(RTL_REG_RMS, RTL8168_BUFFER_SIZE);
    rtl_write8(RTL_REG_MTPS, 0x3B);

    /* 8. Habilita Receptor e Transmissor ANTES de TCR/RCR (requisito do silicio RTL8168) */
    rtl_write8(RTL_REG_CHIPCMD, RTL_CMD_RE | RTL_CMD_TE);
    (void)rtl_read8(RTL_REG_CHIPCMD);

    /* 9. Configura transmissão (TCR) e recepção (RCR) */
    /* TCR: standard IFG (0x3<<24) + unlimited DMA burst (0x7<<8) + AutoFIFO (1<<7) */
    rtl_write32(RTL_REG_TCR, (0x3U << 24) | (0x7U << 8) | (1U << 7));

    /* RCR: no threshold (0x7<<13) + unlimited DMA (0x7<<8) + Broadcast|Multicast|PhysicalMatch */
    uint32_t rcr_filter = mac_valid ? 0x0E : 0x0F;
    rtl_write32(RTL_REG_RCR, (0x7U << 13) | (0x7U << 8) | rcr_filter);

    /* 10. Abre filtro multicast MAR0..MAR7 */
    rtl_write32(RTL_REG_MAR0, 0xFFFFFFFFU);
    rtl_write32(RTL_REG_MAR0 + 4, 0xFFFFFFFFU);

    /* 11. Trava Cfg9346 e limpa flags iniciais de ISR */
    rtl_write8(RTL_REG_CFG9346, RTL_CFG9346_LOCK);
    rtl_write16(RTL_REG_ISR, 0xFFFF);

    /* 12. Verifica status do link físico */
    uint8_t phystat = rtl_read8(RTL_REG_PHYSTATUS);
    rtl_stats.link_up = (phystat & (1 << 1)) != 0;

    rtl_initialized = true;

    serial_print("[RTL8168] Realtek PCIe GBE inicializada com sucesso!\n");
    serial_print("[RTL8168] MAC: ");
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
    serial_print(rtl_stats.link_up ? "UP\n" : "DOWN\n");

    return 0;
}

/* Envio de pacote Ethernet bruto */
int rtl8168_send_packet(const void *data, uint16_t len) {
    if (!rtl_initialized || !data || len == 0 || len > RTL8168_BUFFER_SIZE) {
        return -1;
    }

    /* Verifica se o descritor atual pertence à placa (ainda transmitindo) */
    if (tx_descs[tx_cur].opts1 & RTL_DESC_OWN) {
        return -2; /* Fila cheia / ocupada */
    }

    /* Copia os dados para o buffer de DMA contíguo (com padding Ethernet mínimo de 60 bytes) */
    uint16_t send_len = len;
    if (send_len < 60) {
        memset(tx_buffers[tx_cur], 0, 60);
        send_len = 60;
    }
    memcpy(tx_buffers[tx_cur], data, len);

    uint32_t opts = RTL_DESC_OWN | RTL_DESC_FS | RTL_DESC_LS | (send_len & 0x3FFF);
    if (tx_cur == RTL8168_NUM_TX_DESC - 1) {
        opts |= RTL_DESC_EOR;
    }

    tx_descs[tx_cur].opts2 = 0;
    __asm__ volatile("mfence" ::: "memory");
    tx_descs[tx_cur].opts1 = opts;
    __asm__ volatile("mfence" ::: "memory");

    /* Dispara o polling de transmissão na placa */
    rtl_write8(RTL_REG_TXPOLL, 0x40);

    tx_cur = (tx_cur + 1) % RTL8168_NUM_TX_DESC;

    rtl_stats.packets_tx++;
    rtl_stats.bytes_tx += len;

    return 0;
}

/* Verificação e recepção de pacotes */
int rtl8168_poll_packet(void *out_buffer, uint16_t max_len) {
    if (!rtl_initialized || !out_buffer || max_len == 0) {
        return 0;
    }

    uint16_t isr = rtl_read16(RTL_REG_ISR);

    for (int i = 0; i < RTL8168_NUM_RX_DESC; i++) {
        /* Verifica se o descritor atual pertence à CPU (OWN == 0) */
        if (rx_descs[rx_cur].opts1 & RTL_DESC_OWN) {
            break;
        }

        uint32_t opts = rx_descs[rx_cur].opts1;
        uint16_t len = (uint16_t)(opts & 0x3FFF);
        bool valid = !(opts & RTL_DESC_RX_RES) &&
                     ((opts & (RTL_DESC_FS | RTL_DESC_LS)) == (RTL_DESC_FS | RTL_DESC_LS));

        uint16_t copy_len = 0;
        if (valid && len > 4) {
            copy_len = len - 4; /* Remove 4 bytes de CRC/FCS */
            if (copy_len > max_len) {
                copy_len = max_len;
            }
            memcpy(out_buffer, rx_buffers[rx_cur], copy_len);
        }

        /* Devolve o descritor para o hardware */
        uint32_t return_opts = RTL_DESC_OWN | (RTL8168_BUFFER_SIZE & 0x3FFF);
        if (rx_cur == RTL8168_NUM_RX_DESC - 1) {
            return_opts |= RTL_DESC_EOR;
        }

        rx_descs[rx_cur].opts2 = 0;
        __asm__ volatile("mfence" ::: "memory");
        rx_descs[rx_cur].opts1 = return_opts;
        __asm__ volatile("mfence" ::: "memory");

        rx_cur = (rx_cur + 1) % RTL8168_NUM_RX_DESC;

        if (copy_len > 0) {
            rtl_stats.packets_rx++;
            rtl_stats.bytes_rx += copy_len;
            if (isr != 0) {
                rtl_write16(RTL_REG_ISR, isr);
            }
            return copy_len;
        }
    }

    if (isr != 0) {
        rtl_write16(RTL_REG_ISR, isr);
        if (isr & (RTL_ISR_RDU | RTL_ISR_FOVW)) {
            uint8_t cmd = rtl_read8(RTL_REG_CHIPCMD);
            if ((cmd & (RTL_CMD_RE | RTL_CMD_TE)) != (RTL_CMD_RE | RTL_CMD_TE)) {
                rtl_write8(RTL_REG_CHIPCMD, RTL_CMD_RE | RTL_CMD_TE);
            }
        }
    }

    return 0;
}

bool rtl8168_is_available(void) {
    return rtl_initialized;
}

const uint8_t *rtl8168_get_mac(void) {
    return mac_address;
}

bool rtl8168_is_link_up(void) {
    if (!rtl_initialized) return false;
    uint8_t phystat = rtl_read8(RTL_REG_PHYSTATUS);
    rtl_stats.link_up = (phystat & (1 << 1)) != 0;
    return rtl_stats.link_up;
}

void rtl8168_get_stats(rtl8168_stats_t *out_stats) {
    if (out_stats) {
        (void)rtl8168_is_link_up();
        memcpy(out_stats, &rtl_stats, sizeof(rtl8168_stats_t));
    }
}
