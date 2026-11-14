#include "hda.h"
#include "pci.h"
#include "vmm.h"
#include "pmm.h"
#include "kheap.h"
#include "serial.h"
#include "timer.h"
#include "string.h"
#include "kernel_layout.h"

extern page_directory_t *kernel_directory;

/* Speaker da Placa-mãe (Buzzer físico via PIT 8254 Timer 2 e Porta 0x61) */
static void pc_speaker_start(uint32_t freq_hz) {
    if (freq_hz < 20 || freq_hz > 20000) return;
    uint32_t div = 1193180 / freq_hz;
    asm volatile("outb %0, $0x43" : : "a"((uint8_t)0xB6));
    asm volatile("outb %0, $0x42" : : "a"((uint8_t)(div & 0xFF)));
    asm volatile("outb %0, $0x42" : : "a"((uint8_t)((div >> 8) & 0xFF)));
    uint8_t tmp;
    asm volatile("inb $0x61, %0" : "=a"(tmp));
    if (tmp != (tmp | 3)) {
        asm volatile("outb %0, $0x61" : : "a"((uint8_t)(tmp | 3)));
    }
}

static void pc_speaker_stop(void) {
    uint8_t tmp;
    asm volatile("inb $0x61, %0" : "=a"(tmp));
    asm volatile("outb %0, $0x61" : : "a"((uint8_t)(tmp & 0xFC)));
}

/* Variáveis de estado do controlador HDA */
static bool hda_initialized = false;
static uintptr_t hda_mmio_base = 0;
static uint32_t hda_stream_offset = 0;
static char hda_codec_name[64] = "Nenhum";
static uint8_t hda_volume = 85; /* Volume padrão 85% */

/* Diagnóstico */
static pci_device_t *hda_active_pci = NULL;
static uint16_t hda_active_statests = 0;
static uint16_t hda_active_vid = 0;
static uint16_t hda_active_did = 0;
static uint8_t hda_active_slot = 0;
static uint8_t hda_output_routes = 0;

/* CORB (Command Outbound Ring Buffer) e RIRB (Response Inbound Ring Buffer) */
static volatile uint32_t *corb_buf = NULL;
static uint64_t corb_phys = 0;
static uint16_t corb_wp = 0;

static volatile uint32_t *rirb_buf = NULL;
static uint64_t rirb_phys = 0;
static uint16_t rirb_rp = 0;

/* DMA buffers para Áudio PCM */
static volatile hda_bdl_entry_t *hda_bdl = NULL;
static uint64_t hda_bdl_phys = 0;
static int16_t *hda_dma_buffer = NULL;
static uint64_t hda_dma_buffer_phys = 0;
#define HDA_BUFFER_SAMPLES 8192
#define HDA_BUFFER_BYTES   (HDA_BUFFER_SAMPLES * 2 * sizeof(int16_t)) /* 32 KB */

/* Tabela de seno de 256 pontos (valores pré-calculados em escala 16-bit) */
static const int16_t sine_table_256[256] = {
        0,   804,  1607,  2410,  3211,  4011,  4807,  5601,
     6392,  7179,  7961,  8739,  9511, 10278, 11038, 11792,
    12539, 13278, 14009, 14732, 15446, 16150, 16845, 17530,
    18204, 18867, 19519, 20159, 20787, 21402, 22004, 22594,
    23169, 23731, 24278, 24811, 25329, 25831, 26318, 26789,
    27244, 27683, 28105, 28510, 28897, 29268, 29621, 29955,
    30272, 30571, 30851, 31113, 31356, 31580, 31785, 31970,
    32137, 32284, 32412, 32520, 32609, 32678, 32727, 32757,
    32767, 32757, 32727, 32678, 32609, 32520, 32412, 32284,
    32137, 31970, 31785, 31580, 31356, 31113, 30851, 30571,
    30272, 29955, 29621, 29268, 28897, 28510, 28105, 27683,
    27244, 26789, 26318, 25831, 25329, 24811, 24278, 23731,
    23169, 22594, 22004, 21402, 20787, 20159, 19519, 18867,
    18204, 17530, 16845, 16150, 15446, 14732, 14009, 13278,
    12539, 11792, 11038, 10278,  9511,  8739,  7961,  7179,
     6392,  5601,  4807,  4011,  3211,  2410,  1607,   804,
        0,  -804, -1607, -2410, -3211, -4011, -4807, -5601,
    -6392, -7179, -7961, -8739, -9511,-10278,-11038,-11792,
   -12539,-13278,-14009,-14732,-15446,-16150,-16845,-17530,
   -18204,-18867,-19519,-20159,-20787,-21402,-22004,-22594,
   -23169,-23731,-24278,-24811,-25329,-25831,-26318,-26789,
   -27244,-27683,-28105,-28510,-28897,-29268,-29621,-29955,
   -30272,-30571,-30851,-31113,-31356,-31580,-31785,-31970,
   -32137,-32284,-32412,-32520,-32609,-32678,-32727,-32757,
   -32767,-32757,-32727,-32678,-32609,-32520,-32412,-32284,
   -32137,-31970,-31785,-31580,-31356,-31113,-30851,-30571,
   -30272,-29955,-29621,-29268,-28897,-28510,-28105,-27683,
   -27244,-26789,-26318,-25831,-25329,-24811,-24278,-23731,
   -23169,-22594,-22004,-21402,-20787,-20159,-19519,-18867,
   -18204,-17530,-16845,-16150,-15446,-14732,-14009,-13278,
   -12539,-11792,-11038,-10278, -9511, -8739, -7961, -7179,
    -6392, -5601, -4807, -4011, -3211, -2410, -1607,  -804
};

/* Operações de leitura/escrita MMIO */
static inline uint8_t hda_read8(uint32_t reg) {
    return *(volatile uint8_t *)(hda_mmio_base + reg);
}
static inline uint16_t hda_read16(uint32_t reg) {
    return *(volatile uint16_t *)(hda_mmio_base + reg);
}
static inline uint32_t hda_read32(uint32_t reg) {
    return *(volatile uint32_t *)(hda_mmio_base + reg);
}
static inline void hda_write8(uint32_t reg, uint8_t val) {
    *(volatile uint8_t *)(hda_mmio_base + reg) = val;
}
static inline void hda_write16(uint32_t reg, uint16_t val) {
    *(volatile uint16_t *)(hda_mmio_base + reg) = val;
}
static inline void hda_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(hda_mmio_base + reg) = val;
}

/* Micro-delay via porta de I/O de diagnóstico 0x80 */
static void hda_delay_us(uint32_t us) {
    for (uint32_t i = 0; i < us; i++) {
        asm volatile("outb %%al, $0x80" : : "a"(0));
    }
}

void hda_sleep_ms(uint32_t ms) {
    if (ms == 0) return;

    /* Se interrupções estiverem habilitadas, usa timer_ticks (100 Hz = 10 ms por tick) */
    uint64_t rflags;
    asm volatile("pushfq; popq %0" : "=r"(rflags));
    if (rflags & (1 << 9)) {
        uint32_t ticks = (ms + 9) / 10;
        if (ticks == 0) ticks = 1;
        uint32_t start = timer_ticks;
        while ((timer_ticks - start) < ticks) {
            asm volatile("pause");
        }
        return;
    }

    /* Fallback durante boot com interrupções desabilitadas */
    for (uint32_t i = 0; i < ms; i++) {
        for (uint32_t j = 0; j < 1000; j++) {
            asm volatile("outb %%al, $0x80" : : "a"(0));
        }
    }
}

/* Inicializa os anéis DMA CORB e RIRB (Mandatório pela Intel para envio de comandos) */
static int hda_init_corb_rirb(void) {
    if (!corb_buf) {
        void *ring_mem = pmm_alloc_block();
        if (!ring_mem) return -1;
        memset(ring_mem, 0, 4096);

        uint64_t ring_phys = virt_to_phys(ring_mem);

        corb_buf = (volatile uint32_t *)ring_mem;
        corb_phys = ring_phys;

        rirb_buf = (volatile uint32_t *)((uint8_t *)ring_mem + 2048);
        rirb_phys = ring_phys + 2048;
    }

    /* 1. Desliga os motores CORB e RIRB antes de configurar */
    hda_write8(HDA_REG_CORBCTL, 0);
    hda_write8(HDA_REG_RIRBCTL, 0);
    hda_delay_us(100);

    /* 2. Configura endereços base do CORB (256 entradas de 32-bit = 1024 bytes) */
    hda_write32(HDA_REG_CORBLBASE, (uint32_t)(corb_phys & 0xFFFFFFFF));
    hda_write32(HDA_REG_CORBUBASE, (uint32_t)(corb_phys >> 32));
    hda_write8(HDA_REG_CORBSIZE, 0x02); /* 0x02 = 256 entradas */

    /* Reset do Read Pointer do CORB */
    hda_write16(HDA_REG_CORBRP, 0x8000); /* CORBRP_RST = 1 */
    for (int i = 0; i < 2000; i++) {
        if (hda_read16(HDA_REG_CORBRP) & 0x8000) break;
        hda_delay_us(10);
    }
    hda_write16(HDA_REG_CORBRP, 0x0000); /* Limpa RST */
    for (int i = 0; i < 2000; i++) {
        if (!(hda_read16(HDA_REG_CORBRP) & 0x8000)) break;
        hda_delay_us(10);
    }

    /* Zera ponteiro de escrita do CORB */
    hda_write16(HDA_REG_CORBWP, 0);
    corb_wp = 0;

    /* 3. Configura endereços base do RIRB (256 entradas de 64-bit = 2048 bytes) */
    hda_write32(HDA_REG_RIRBLBASE, (uint32_t)(rirb_phys & 0xFFFFFFFF));
    hda_write32(HDA_REG_RIRBUBASE, (uint32_t)(rirb_phys >> 32));
    hda_write8(HDA_REG_RIRBSIZE, 0x02); /* 0x02 = 256 entradas */

    /* Reset do Write Pointer do RIRB */
    hda_write16(HDA_REG_RIRBWP, 0x8000); /* RIRBWP_RST = 1 */

    /* Notificação a cada 1 resposta recebida */
    hda_write16(HDA_REG_RINTCNT, 1);
    rirb_rp = 0;

    /* 4. Inicia os motores CORB e RIRB */
    hda_write8(HDA_REG_CORBCTL, 0x02); /* CORBCTL_RUN = 1 */
    hda_write8(HDA_REG_RIRBCTL, 0x02); /* RIRBCTL_DMA_EN = 1 */
    hda_delay_us(500);

    return 0;
}

/* Envia um verbo de comando para o codec via CORB/RIRB (com fallback por Immediate Command) */
static uint32_t hda_send_verb(uint8_t codec, uint8_t nid, uint32_t payload) {
    if (!hda_mmio_base) return 0;

    uint32_t cmd = ((uint32_t)(codec & 0x0F) << 28) |
                   ((uint32_t)(nid & 0xFF) << 20) |
                   (payload & 0xFFFFF);

    /* 1. Envio prioritário via anel DMA CORB / RIRB (obrigatório em chipsets Intel físicos) */
    if (corb_buf && rirb_buf) {
        corb_wp = (corb_wp + 1) & 0xFF;
        corb_buf[corb_wp] = cmd;
        hda_write16(HDA_REG_CORBWP, corb_wp);

        for (int timeout = 0; timeout < 5000; timeout++) {
            uint16_t rirb_hw_wp = hda_read16(HDA_REG_RIRBWP) & 0xFF;
            if (rirb_hw_wp != rirb_rp) {
                rirb_rp = (rirb_rp + 1) & 0xFF;
                uint32_t res = rirb_buf[rirb_rp * 2]; /* uint32_t inferior = resposta */
                hda_write8(HDA_REG_RIRBSTS, 0x05);
                return res;
            }
            hda_delay_us(10);
        }
    }

    /* 2. Fallback: Immediate Command Interface (para QEMU ou controladoras antigas) */
    for (int i = 0; i < 2000; i++) {
        if (!(hda_read16(HDA_REG_ICS) & HDA_ICS_ICB)) break;
        hda_delay_us(10);
    }
    hda_write16(HDA_REG_ICS, hda_read16(HDA_REG_ICS) | HDA_ICS_IRV);
    hda_write32(HDA_REG_ICO, cmd);
    hda_write16(HDA_REG_ICS, hda_read16(HDA_REG_ICS) | HDA_ICS_ICB);

    for (int i = 0; i < 2000; i++) {
        uint16_t ics = hda_read16(HDA_REG_ICS);
        if (!(ics & HDA_ICS_ICB)) {
            return hda_read32(HDA_REG_ICI);
        }
        hda_delay_us(10);
    }

    return 0;
}

/* Decode codec connection lists, including short/long and range encodings. */
static uint8_t hda_get_connections(uint8_t codec, uint8_t nid, uint8_t *out,
                                   uint8_t capacity) {
    uint32_t param = hda_send_verb(codec, nid, 0xF000E);
    uint8_t encoded_count = (uint8_t)(param & 0x7F);
    bool long_form = (param & 0x80) != 0;
    uint8_t count = 0;
    uint8_t last = 0;

    if (!encoded_count) return 0;
    for (uint8_t i = 0; i < encoded_count && count < capacity; i++) {
        uint16_t entry;
        bool range;
        if (long_form) {
            uint8_t base = (uint8_t)(i & ~1u);
            uint32_t packed = hda_send_verb(codec, nid, 0xF0200 | base);
            entry = (uint16_t)((packed >> ((i & 1u) * 16)) & 0xFFFF);
            range = (entry & 0x8000) != 0;
            entry &= 0x7FFF;
        } else {
            uint8_t base = (uint8_t)(i & ~3u);
            uint32_t packed = hda_send_verb(codec, nid, 0xF0200 | base);
            uint8_t short_entry = (uint8_t)((packed >> ((i & 3u) * 8)) & 0xFF);
            range = (short_entry & 0x80) != 0;
            entry = short_entry & 0x7F;
        }
        if (entry >= 128) continue;
        uint8_t target = (uint8_t)entry;
        if (range && count && target > last) {
            for (uint16_t n = (uint16_t)last + 1; n <= target && count < capacity; n++)
                out[count++] = (uint8_t)n;
        } else {
            out[count++] = target;
        }
        last = target;
    }
    return count;
}

/* Walk from a pin toward an output converter and program each selected edge. */
static bool hda_find_output_path(uint8_t codec, uint8_t nid, bool visited[128],
                                 uint8_t depth) {
    if (nid == 0 || nid >= 128 || visited[nid] || depth >= 32) return false;
    uint32_t caps = hda_send_verb(codec, nid, 0xF0009);
    uint8_t type = (uint8_t)((caps >> 20) & 0x0F);
    if (type == 0x0) return true; /* Audio Output Converter (DAC). */
    if (type != 0x2 && type != 0x3 && type != 0x4) return false;

    visited[nid] = true;
    uint8_t connections[128];
    uint8_t count = hda_get_connections(codec, nid, connections, sizeof(connections));
    for (uint8_t i = 0; i < count; i++) {
        uint8_t source = connections[i];
        if (source == 0 || source >= 128 || visited[source]) continue;
        if (hda_find_output_path(codec, source, visited, depth + 1)) {
            if ((type == 0x3 || type == 0x4) && count > 1)
                hda_send_verb(codec, nid, 0x70100 | i);
            /* HDA amp controls exist on mixers, selectors, pins and DACs.
             * A discovered path is not usable if any one of those amps stays
             * muted. Set both channels, selecting the input index for the
             * chosen connection where the widget has an input amplifier. */
            if (caps & (1u << 2))
                hda_send_verb(codec, nid, 0x3B07F); /* Output amp, L/R, unmuted */
            if ((caps & (1u << 1)) && i < 16)
                hda_send_verb(codec, nid, 0x3707F | ((uint32_t)i << 8));
            return true;
        }
    }
    visited[nid] = false;
    return false;
}

/* Configura os caminhos de saída anunciados pelo próprio codec. */
static uint8_t hda_setup_codec(uint8_t codec) {
    /* Identifica fabricante do Codec (NID 0, Verbo 0xF0000) */
    uint32_t vid_did = hda_send_verb(codec, 0x00, 0xF0000);
    uint16_t vid = (vid_did >> 16) & 0xFFFF;
    uint16_t did = vid_did & 0xFFFF;

    serial_print("[hda] Resposta do Codec slot ");
    serial_print_hex(codec);
    serial_print(": VID=0x");
    serial_print_hex(vid);
    serial_print(" DID=0x");
    serial_print_hex(did);
    serial_print("\n");

    if (vid == 0 || vid == 0xFFFF) return 0;

    /* Prioriza exibição de codec Realtek se encontrado */
    if (vid == 0x10EC || hda_active_vid == 0) {
        hda_active_slot = codec;
        hda_active_vid = vid;
        hda_active_did = did;

        if (vid == 0x10EC) {
            if (did == 0x0662) strcpy(hda_codec_name, "Realtek ALC662 (6-Canal HD Audio)");
            else if (did == 0x0887) strcpy(hda_codec_name, "Realtek ALC887 (7.1 HD Audio)");
            else if (did == 0x0892) strcpy(hda_codec_name, "Realtek ALC892 (7.1 HD Audio)");
            else if (did == 0x0888) strcpy(hda_codec_name, "Realtek ALC888 (HD Audio)");
            else if (did == 0x0269) strcpy(hda_codec_name, "Realtek ALC269 (Integrated Audio)");
            else strcpy(hda_codec_name, "Realtek High Definition Audio");
        } else if (vid == 0x8086) {
            strcpy(hda_codec_name, "Intel Integrated HD Audio");
        } else if (vid == 0x11D4) {
            strcpy(hda_codec_name, "Analog Devices SoundMAX");
        } else if (vid == 0x1013) {
            strcpy(hda_codec_name, "Cirrus Logic HD Audio");
        } else if (vid == 0x10DE) {
            strcpy(hda_codec_name, "NVIDIA HDMI HD Audio");
        } else {
            strcpy(hda_codec_name, "HD Audio Codec Genérico");
        }
    }

    /* Liga o Audio Function Group (AFG - NID 0x01) em D0 (ativo) */
    hda_send_verb(codec, 0x01, 0x70500);
    hda_sleep_ms(15);

    /* Consulta nós subordinados ao AFG */
    uint32_t sub_nodes = hda_send_verb(codec, 0x01, 0xF0004);
    uint8_t start_nid = (sub_nodes >> 16) & 0xFF;
    uint8_t count = sub_nodes & 0xFF;
    if (count == 0 || start_nid == 0 || start_nid >= 128) return 0;
    if ((uint16_t)start_nid + count > 128) count = (uint8_t)(128 - start_nid);

    /* Ativa os widgets e prepara os conversores de saída para o stream. */
    for (uint16_t node = start_nid; node < (uint16_t)start_nid + count; node++) {
        uint8_t nid = (uint8_t)node;
        uint32_t cap = hda_send_verb(codec, nid, 0xF0009); /* Widget Capabilities */
        uint8_t type = (cap >> 20) & 0x0F;

        /* Coloca o nó em D0 (Power ativo) */
        hda_send_verb(codec, nid, 0x70500);

        if (type == 0x0) {
            /* Audio Output Converter (DAC) */
            hda_send_verb(codec, nid, 0x20011); /* Set Format: 48kHz, 16-bit, estéreo (2 canais) */
            hda_send_verb(codec, nid, 0x70610); /* Stream 1, Canal 0 */
            if (cap & (1u << 2))
                hda_send_verb(codec, nid, 0x3B07F); /* Unmute output amp. */
        } else if (type == 0x2 || type == 0x3) {
            /* Mixer/selector controls are set only on the discovered path. */
        } else if (type == 0x4) {
            /* Pin route and output control are configured after discovery. */
        }
    }

    uint8_t routed = 0;
    for (uint16_t node = start_nid; node < (uint16_t)start_nid + count && node < 128; node++) {
        uint8_t nid = (uint8_t)node;
        uint32_t caps = hda_send_verb(codec, nid, 0xF0009);
        if (((caps >> 20) & 0x0F) != 0x4) continue;
        uint32_t pin_caps = hda_send_verb(codec, nid, 0xF000C);
        if (!(pin_caps & (1u << 4))) continue;

        uint32_t config = hda_send_verb(codec, nid, 0xF001C);
        uint8_t connectivity = (uint8_t)(config >> 30);
        uint8_t device = (uint8_t)((config >> 20) & 0x0F);
        if (config != 0 && config != 0xFFFFFFFFu && connectivity == 1) continue;
        if (config != 0 && config != 0xFFFFFFFFu && device > 2) continue;

        bool visited[128] = { false };
        if (!hda_find_output_path(codec, nid, visited, 0)) continue;
        hda_send_verb(codec, nid, 0x70500);
        hda_send_verb(codec, nid, 0x70700 | (device == 2 ? 0xC0 : 0x40));
        if (pin_caps & (1u << 16)) hda_send_verb(codec, nid, 0x70C02);
        if (caps & (1u << 2)) hda_send_verb(codec, nid, 0x3B07F);
        routed++;
    }
    return routed;
}

/* Inicializa o controlador Intel HDA */
int hda_init(void) {
    int hda_dev_count = pci_get_hda_count();
    if (hda_dev_count == 0) {
        serial_print("[hda] Nenhum controlador PCI HD Audio encontrado.\n");
        return -1;
    }

    pci_device_t *selected_pci = NULL;
    uint16_t found_statests = 0;

    /* Tenta cada controlador HD Audio até encontrar um com Codecs conectados */
    for (int dev_idx = 0; dev_idx < hda_dev_count; dev_idx++) {
        pci_device_t *pci = pci_get_hda_by_index(dev_idx);
        if (!pci) continue;

        serial_print("[hda] Testando controlador PCI: Bus=");
        serial_print_hex(pci->bus);
        serial_print(" Dev=");
        serial_print_hex(pci->device);
        serial_print(" Fn=");
        serial_print_hex(pci->function);
        serial_print(" VID:DID=");
        serial_print_hex(pci->vendor_id);
        serial_print(":");
        serial_print_hex(pci->device_id);
        serial_print("\n");

        /* Habilita Bus Master e MMIO */
        uint16_t pci_cmd = (uint16_t)pci_read_config(pci->bus, pci->device, pci->function, 0x04);
        pci_cmd |= (1 << 1) | (1 << 2);
        pci_write_config(pci->bus, pci->device, pci->function, 0x04, pci_cmd);

        uint32_t bar0 = pci_read_config(pci->bus, pci->device, pci->function, 0x10);
        if (bar0 & 1) continue; /* I/O port, ignora */

        uint64_t mmio_phys = (uint64_t)(bar0 & ~0xF);
        if ((bar0 & 0x6) == 0x4) {
            uint32_t bar0_high = pci_read_config(pci->bus, pci->device, pci->function, 0x14);
            mmio_phys |= ((uint64_t)bar0_high << 32);
        }

        /* Mapeia a regiao MMIO de 16KB no direct-map com Cache Desabilitado (PCD) */
        for (uintptr_t p = 0; p < 0x4000; p += 4096) {
            vmm_map_kernel_page((uint64_t)(mmio_phys + p),
                                (uint64_t)phys_to_virt(mmio_phys + p),
                                PTE_P | PTE_W | PTE_PCD | PTE_NX);
        }
        hda_mmio_base = (uintptr_t)phys_to_virt(mmio_phys);

        /* Reset do controlador via GCTL.CRST */
        hda_write32(HDA_REG_GCTL, 0);
        for (int i = 0; i < 2000; i++) {
            if (!(hda_read32(HDA_REG_GCTL) & HDA_GCTL_CRST)) break;
            hda_delay_us(10);
        }
        hda_sleep_ms(2);

        hda_write32(HDA_REG_GCTL, HDA_GCTL_CRST);
        for (int i = 0; i < 5000; i++) {
            if (hda_read32(HDA_REG_GCTL) & HDA_GCTL_CRST) break;
            hda_delay_us(10);
        }

        /* Aguarda estabilização dos codecs analógicos na linha HDA Link (até 60 ms) */
        uint16_t statests = 0;
        for (int t = 0; t < 60; t++) {
            statests = hda_read16(HDA_REG_STATESTS);
            if (statests != 0) break;
            hda_sleep_ms(1);
        }

        serial_print("[hda] STATESTS lido: 0x");
        serial_print_hex(statests);
        serial_print("\n");

        if (statests != 0) {
            selected_pci = pci;
            found_statests = statests;
            break;
        }
    }

    /* A controller without a codec in STATESTS is not an audio output device. */
    if (!selected_pci || found_statests == 0) return -1;

    hda_active_pci = selected_pci;
    hda_active_statests = found_statests;

    /* Inicializa anéis de comando DMA CORB e RIRB */
    if (hda_init_corb_rirb() != 0) {
        serial_print("[hda] Falha ao inicializar CORB/RIRB.\n");
        return -2;
    }

    /* Configura todos os codecs detectados */
    uint8_t routed = 0;
    for (int c = 0; c < 15; c++) {
        if (found_statests & (1 << c)) {
            uint8_t count = hda_setup_codec((uint8_t)c);
            if ((uint16_t)routed + count > 255) routed = 255;
            else routed = (uint8_t)(routed + count);
        }
    }
    hda_output_routes = routed;
    if (hda_active_vid == 0 || routed == 0) {
        serial_print("[hda] Codec ou rota analogica de saida nao encontrado.\n");
        return -3;
    }

    /* Aloca o buffer de DMA contíguo de 32 KB para áudio diretamente do PMM */
    if (!hda_dma_buffer) {
        uint64_t base = 0, size = 0;
        if (pmm_claim_contiguous_region(0x1000000, HDA_BUFFER_BYTES, HDA_BUFFER_BYTES, &base, &size) == 0) {
            hda_dma_buffer = (int16_t *)phys_to_virt(base);
            hda_dma_buffer_phys = base;
        } else {
            void *buf = kmalloc(HDA_BUFFER_BYTES + 4096);
            if (!buf) return -4;
            uint64_t p = (virt_to_phys(buf) + 4095) & ~4095ULL;
            hda_dma_buffer = (int16_t *)phys_to_virt(p);
            hda_dma_buffer_phys = p;
        }
    }
    if (!hda_dma_buffer || !hda_dma_buffer_phys) return -4;
    memset(hda_dma_buffer, 0, HDA_BUFFER_BYTES);

    /* Aloca lista de descritores de buffer (BDL) usando página pura do PMM */
    if (!hda_bdl) {
        void *bdl_page = pmm_alloc_block();
        if (!bdl_page) return -4;
        memset(bdl_page, 0, 4096);
        hda_bdl = (volatile hda_bdl_entry_t *)bdl_page;
        hda_bdl_phys = virt_to_phys(bdl_page);
    }

    /* Divide o buffer de 32 KB em 2 fatias de 16 KB no BDL */
    uint32_t half_bytes = HDA_BUFFER_BYTES / 2;
    hda_bdl[0].addr = hda_dma_buffer_phys;
    hda_bdl[0].length = half_bytes;
    hda_bdl[0].ioc = 0;

    hda_bdl[1].addr = hda_dma_buffer_phys + half_bytes;
    hda_bdl[1].length = half_bytes;
    hda_bdl[1].ioc = 0;

    /* Determina offset do primeiro Output Stream */
    uint16_t gcap = hda_read16(HDA_REG_GCAP);
    uint8_t num_iss = (gcap >> 8) & 0x0F;
    uint8_t num_oss = (gcap >> 12) & 0x0F;
    if (num_oss == 0) return -5;
    /* Output Stream Descriptor 0 follows the controller's input descriptors. */
    hda_stream_offset = 0x80 + (uint32_t)num_iss * 0x20;
    uint32_t so = hda_stream_offset;

    /* 1. Reset do Stream Descriptor (Ciclo SRST obrigatório da Intel) */
    hda_write8(so + HDA_SD_CTL, HDA_SD_CTL_SRST);
    for (int i = 0; i < 2000; i++) {
        if (hda_read8(so + HDA_SD_CTL) & HDA_SD_CTL_SRST) break;
        hda_delay_us(10);
    }
    hda_sleep_ms(1);

    hda_write8(so + HDA_SD_CTL, 0);
    for (int i = 0; i < 2000; i++) {
        if (!(hda_read8(so + HDA_SD_CTL) & HDA_SD_CTL_SRST)) break;
        hda_delay_us(10);
    }
    hda_sleep_ms(1);

    /* 2. Limpa status anterior do stream (W1C) */
    hda_write8(so + HDA_SD_STS, 0x1C);

    /* 3. Define ponteiro do BDL */
    hda_write32(so + HDA_SD_BDLPL, (uint32_t)(hda_bdl_phys & 0xFFFFFFFF));
    hda_write32(so + HDA_SD_BDLPU, (uint32_t)(hda_bdl_phys >> 32));

    /* 4. Comprimento do buffer cíclico */
    hda_write32(so + HDA_SD_CBL, HDA_BUFFER_BYTES);

    /* 5. Índice do último descritor (2 buffers -> índice 1) */
    hda_write16(so + HDA_SD_LVI, 1);

    /* 6. Formato: 48 kHz, 16 bits, estéreo 2 canais (0x0011) */
    hda_write16(so + HDA_SD_FMT, 0x0011);

    /* 7. Inicia o stream com Tag 1 e RUN ativo */
    uint32_t sctl = (1 << 20) | HDA_SD_CTL_RUN;
    hda_write32(so + HDA_SD_CTL, sctl);

    for (int i = 0; i < 5000; i++) {
        if (hda_read32(so + HDA_SD_CTL) & HDA_SD_CTL_RUN) break;
        hda_delay_us(10);
    }
    if (!(hda_read32(so + HDA_SD_CTL) & HDA_SD_CTL_RUN)) {
        serial_print("[hda] O stream de saida nao iniciou (RUN=0).\n");
        return -5;
    }

    hda_initialized = true;
    serial_print("[hda] Saida HDA ativa; codec e rota analogica detectados.\n");
    return 0;
}

bool hda_is_available(void) {
    return hda_initialized;
}

const char *hda_get_codec_name(void) {
    return hda_codec_name;
}

uint16_t hda_get_codec_vid(void) {
    return hda_active_vid;
}

uint16_t hda_get_codec_did(void) {
    return hda_active_did;
}

uint16_t hda_get_statests(void) {
    return hda_active_statests;
}

pci_device_t *hda_get_pci_device(void) {
    return hda_active_pci;
}

uint32_t hda_get_stream_status(void) {
    if (!hda_initialized || !hda_mmio_base) return 0;
    return hda_read32(hda_stream_offset + HDA_SD_CTL);
}

uint32_t hda_get_stream_position(void) {
    if (!hda_initialized || !hda_mmio_base) return 0;
    return hda_read32(hda_stream_offset + HDA_SD_LPIB);
}

uint8_t hda_get_output_routes(void) {
    return hda_output_routes;
}

void hda_set_volume(uint8_t volume_percent) {
    if (volume_percent > 100) volume_percent = 100;
    hda_volume = volume_percent;
}

uint8_t hda_get_volume(void) {
    return hda_volume;
}

/* Reproduz um tom sonoro (Alta fidelidade HDA, ou PC Speaker como fallback) */
void hda_beep(uint32_t freq_hz, uint32_t duration_ms) {
    if (freq_hz < 20) freq_hz = 20;
    if (freq_hz > 15000) freq_hz = 15000;
    if (duration_ms == 0) duration_ms = 100;

    if (hda_initialized && hda_dma_buffer) {
        /* Síntese de onda senoidal limpa via Intel HDA (Realtek DAC) */
        uint32_t sample_rate = 48000;
        uint32_t step = ((uint64_t)freq_hz * 256ULL * 65536ULL) / sample_rate;
        uint32_t phase = 0;
        int32_t amp = (28000 * (int32_t)hda_volume) / 100;

        /* Preenche os 32 KB completos do buffer cíclico DMA para reprodução contínua */
        for (uint32_t i = 0; i < HDA_BUFFER_SAMPLES; i++) {
            uint8_t table_idx = (phase >> 16) & 0xFF;
            int16_t sample = (int16_t)(((int32_t)sine_table_256[table_idx] * amp) / 32768);

            hda_dma_buffer[i * 2 + 0] = sample;
            hda_dma_buffer[i * 2 + 1] = sample;

            phase += step;
        }

        hda_sleep_ms(duration_ms);
        memset(hda_dma_buffer, 0, HDA_BUFFER_BYTES);
    } else {
        /* Fallback: Buzzer da placa-mãe caso o HDA não esteja disponível */
        pc_speaker_start(freq_hz);
        hda_sleep_ms(duration_ms);
        pc_speaker_stop();
    }
}

/* Reproduz buffer PCM estéreo cru de 16-bit 48kHz */
void hda_play_pcm(const int16_t *samples, uint32_t num_samples) {
    if (!hda_initialized || !hda_dma_buffer || !samples) return;

    uint32_t sample_rate = 48000;
    uint32_t played = 0;

    while (played < num_samples) {
        uint32_t chunk = num_samples - played;
        if (chunk > HDA_BUFFER_SAMPLES) chunk = HDA_BUFFER_SAMPLES;

        for (uint32_t i = 0; i < chunk; i++) {
            int32_t left = (int32_t)samples[(played + i) * 2 + 0];
            int32_t right = (int32_t)samples[(played + i) * 2 + 1];

            hda_dma_buffer[i * 2 + 0] = (int16_t)((left * hda_volume) / 100);
            hda_dma_buffer[i * 2 + 1] = (int16_t)((right * hda_volume) / 100);
        }

        played += chunk;
        uint32_t chunk_ms = (chunk * 1000) / sample_rate;
        if (chunk_ms > 0) {
            hda_sleep_ms(chunk_ms);
        }
    }

    memset(hda_dma_buffer, 0, HDA_BUFFER_BYTES);
}

/* Som harmônico de inicialização do LiwusOS (Acorde de boas-vindas: C5 -> E5 -> G5 -> C6) */
void hda_startup_sound(void) {
    /* Notas da escala: C5 (523 Hz), E5 (659 Hz), G5 (784 Hz), C6 (1046 Hz) */
    hda_beep(523, 160);
    hda_sleep_ms(30);
    hda_beep(659, 160);
    hda_sleep_ms(30);
    hda_beep(784, 160);
    hda_sleep_ms(30);
    hda_beep(1046, 320);
}
