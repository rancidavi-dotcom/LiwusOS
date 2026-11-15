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
static uint16_t corb_mask = 0xFF;

static volatile uint32_t *rirb_buf = NULL;
static uint64_t rirb_phys = 0;
static uint16_t rirb_rp = 0;
static uint16_t rirb_mask = 0xFF;
static bool corb_rirb_active = false;

/* Guarda os DACs ativos para ressincronizar apos ligar o Stream DMA */
static uint8_t hda_active_dacs[16];
static uint8_t hda_active_dac_count = 0;

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
    corb_rirb_active = false;
    if (!corb_buf) {
        void *ring_mem = pmm_alloc_block();
        if (!ring_mem) return -1;
        uint64_t ring_phys = virt_to_phys(ring_mem);
        /* Mapeia a pagina de CORB/RIRB com Cache Disable (PTE_PCD) para coerencia DMA total */
        vmm_map_kernel_page(ring_phys, (uint64_t)ring_mem,
                            PTE_P | PTE_W | PTE_PCD | PTE_NX);
        memset(ring_mem, 0, 4096);

        corb_buf = (volatile uint32_t *)ring_mem;
        corb_phys = ring_phys;

        rirb_buf = (volatile uint32_t *)((uint8_t *)ring_mem + 2048);
        rirb_phys = ring_phys + 2048;
    } else {
        memset((void *)corb_buf, 0, 4096);
    }

    /* 1. Desliga os motores CORB e RIRB antes de configurar */
    hda_write8(HDA_REG_CORBCTL, 0);
    hda_write8(HDA_REG_RIRBCTL, 0);
    for (int i = 0; i < 1000; i++) {
        if (!(hda_read8(HDA_REG_CORBCTL) & 0x02) &&
            !(hda_read8(HDA_REG_RIRBCTL) & 0x02))
            break;
        hda_delay_us(10);
    }

    /* 2. Configura endereços base e tamanho do CORB conforme capacidades (CORBSZCAP) */
    hda_write32(HDA_REG_CORBLBASE, (uint32_t)(corb_phys & 0xFFFFFFFF));
    hda_write32(HDA_REG_CORBUBASE, (uint32_t)(corb_phys >> 32));
    uint8_t corbsz = hda_read8(HDA_REG_CORBSIZE);
    uint8_t corb_cap = (corbsz >> 4) & 0x0F;
    if (corb_cap & 0x04) {
        hda_write8(HDA_REG_CORBSIZE, 0x02);
        corb_mask = 0xFF;
    } else if (corb_cap & 0x02) {
        hda_write8(HDA_REG_CORBSIZE, 0x01);
        corb_mask = 0x0F;
    } else {
        hda_write8(HDA_REG_CORBSIZE, 0x00);
        corb_mask = 0x01;
    }

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

    /* 3. Configura endereços base e tamanho do RIRB conforme capacidades (RIRBSZCAP) */
    hda_write32(HDA_REG_RIRBLBASE, (uint32_t)(rirb_phys & 0xFFFFFFFF));
    hda_write32(HDA_REG_RIRBUBASE, (uint32_t)(rirb_phys >> 32));
    uint8_t rirbsz = hda_read8(HDA_REG_RIRBSIZE);
    uint8_t rirb_cap = (rirbsz >> 4) & 0x0F;
    if (rirb_cap & 0x04) {
        hda_write8(HDA_REG_RIRBSIZE, 0x02);
        rirb_mask = 0xFF;
    } else if (rirb_cap & 0x02) {
        hda_write8(HDA_REG_RIRBSIZE, 0x01);
        rirb_mask = 0x0F;
    } else {
        hda_write8(HDA_REG_RIRBSIZE, 0x00);
        rirb_mask = 0x01;
    }

    /* Reset do Write Pointer do RIRB */
    hda_write16(HDA_REG_RIRBWP, 0x8000); /* RIRBWP_RST = 1 */
    hda_delay_us(50);

    /* Notificação a cada 1 resposta recebida */
    hda_write16(HDA_REG_RINTCNT, 1);
    hda_write8(HDA_REG_RIRBSTS, 0x05);
    rirb_rp = 0;

    /* 4. Inicia os motores CORB e RIRB */
    hda_write8(HDA_REG_CORBCTL, 0x02); /* CORBCTL_RUN = 1 */
    hda_write8(HDA_REG_RIRBCTL, 0x02); /* RIRBCTL_DMA_EN = 1 */
    hda_delay_us(500);
    corb_rirb_active = true;

    return 0;
}

/* Envia um verbo de comando para o codec via CORB/RIRB (com fallback por Immediate Command) */
static uint32_t hda_send_verb(uint8_t codec, uint8_t nid, uint32_t payload) {
    if (!hda_mmio_base) return 0;

    uint32_t cmd = ((uint32_t)(codec & 0x0F) << 28) |
                   ((uint32_t)(nid & 0xFF) << 20) |
                   (payload & 0xFFFFF);

    /* 1. Envio prioritário via anel DMA CORB / RIRB (obrigatório em chipsets Intel/AMD físicos) */
    if (corb_rirb_active && corb_buf && rirb_buf) {
        /* Sincroniza rirb_rp antes de enviar para descartar respostas atrasadas/unsolicited */
        rirb_rp = hda_read16(HDA_REG_RIRBWP) & rirb_mask;
        corb_wp = (hda_read16(HDA_REG_CORBWP) + 1) & corb_mask;
        corb_buf[corb_wp] = cmd;
        asm volatile("mfence" ::: "memory");
        hda_write16(HDA_REG_CORBWP, corb_wp);

        for (int timeout = 0; timeout < 25000; timeout++) {
            uint16_t rirb_hw_wp = hda_read16(HDA_REG_RIRBWP) & rirb_mask;
            while (rirb_hw_wp != rirb_rp) {
                rirb_rp = (rirb_rp + 1) & rirb_mask;
                asm volatile("mfence" ::: "memory");
                uint32_t res = rirb_buf[rirb_rp * 2];
                uint32_t res_ex = rirb_buf[rirb_rp * 2 + 1];
                hda_write8(HDA_REG_RIRBSTS, 0x05);
                /* Ignora respostas nao-solicitadas (Unsolicited Response, bit 4 de res_ex) */
                if (res_ex & (1u << 4)) continue;
                return res;
            }
            hda_delay_us(10);
        }
    }

    /* 2. Fallback: Immediate Command Interface (pausa CORB/RIRB conforme especificacao Intel 3.3.32) */
    uint8_t saved_corb = hda_read8(HDA_REG_CORBCTL);
    uint8_t saved_rirb = hda_read8(HDA_REG_RIRBCTL);
    if (saved_corb || saved_rirb) {
        hda_write8(HDA_REG_CORBCTL, 0);
        hda_write8(HDA_REG_RIRBCTL, 0);
        hda_delay_us(50);
    }

    for (int i = 0; i < 2000; i++) {
        if (!(hda_read16(HDA_REG_ICS) & HDA_ICS_ICB)) break;
        hda_delay_us(10);
    }
    hda_write16(HDA_REG_ICS, HDA_ICS_IRV);
    hda_write32(HDA_REG_ICO, cmd);
    hda_write16(HDA_REG_ICS, HDA_ICS_ICB | HDA_ICS_IRV);

    uint32_t imm_res = 0;
    for (int i = 0; i < 5000; i++) {
        uint16_t ics = hda_read16(HDA_REG_ICS);
        if (!(ics & HDA_ICS_ICB) && (ics & HDA_ICS_IRV)) {
            imm_res = hda_read32(HDA_REG_ICI);
            hda_write16(HDA_REG_ICS, HDA_ICS_IRV);
            break;
        }
        hda_delay_us(10);
    }

    if (saved_corb || saved_rirb) {
        hda_write8(HDA_REG_CORBCTL, saved_corb);
        hda_write8(HDA_REG_RIRBCTL, saved_rirb);
    }
    return imm_res;
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

/* Calcula o ganho nominal (0 dB ou maximo valido) conforme Intel HDA Spec 7.3.4.10:
 * amp_cap bits [6:0] = Offset (passo 0 dB), bits [14:8] = NumSteps (passo maximo).
 * Jamais envia 0x7F se NumSteps < 0x7F (em amplificadores Mute-Only onde NumSteps=0,
 * enviar 0x7F faz codecs Realtek ignorarem o desmute!). */
static uint8_t hda_calc_amp_gain(uint32_t amp_cap) {
    if (amp_cap == 0 || amp_cap == 0xFFFFFFFFu) return 0x00;
    uint8_t offset = (uint8_t)(amp_cap & 0x7F);
    uint8_t num_steps = (uint8_t)((amp_cap >> 8) & 0x7F);
    if (num_steps == 0) return 0x00;
    if (offset > 0 && offset <= num_steps) return offset;
    return num_steps;
}

static void hda_unmute_output_amp(uint8_t codec, uint8_t nid, uint32_t widget_caps,
                                  uint32_t afg_out_amp_cap) {
    if (!(widget_caps & (1u << 2))) return; /* OutAmpPresent == 0 */
    uint32_t amp_cap = afg_out_amp_cap;
    if (widget_caps & (1u << 3)) { /* AmpParamOverride */
        uint32_t ov = hda_send_verb(codec, nid, 0xF0012);
        if (ov != 0 && ov != 0xFFFFFFFFu) amp_cap = ov;
    }
    uint8_t gain = hda_calc_amp_gain(amp_cap);
    uint8_t num_steps = (uint8_t)((amp_cap >> 8) & 0x7F);
    /* Desmuta L+R Output Amp no passo 0 (garante desmute em Mute-Only) e depois aplica gain */
    hda_send_verb(codec, nid, 0x3B000);
    if (gain > 0) {
        hda_send_verb(codec, nid, 0x3B000 | (uint32_t)(gain & 0x7F));
    } else if (amp_cap == 0 && num_steps == 0) {
        /* Fallback caso o codec nao reporte amp_cap mas tenha volume variavel */
        hda_send_verb(codec, nid, 0x3B03F);
        hda_send_verb(codec, nid, 0x3B000);
    }
}

static void hda_unmute_input_amp(uint8_t codec, uint8_t nid, uint32_t widget_caps,
                                 uint32_t afg_in_amp_cap, uint8_t index) {
    if (!(widget_caps & (1u << 1)) || index >= 16) return; /* InAmpPresent == 0 */
    uint32_t amp_cap = afg_in_amp_cap;
    if (widget_caps & (1u << 3)) { /* AmpParamOverride */
        uint32_t ov = hda_send_verb(codec, nid, 0xF000D);
        if (ov != 0 && ov != 0xFFFFFFFFu) amp_cap = ov;
    }
    uint8_t gain = hda_calc_amp_gain(amp_cap);
    /* Desmuta L+R Input Amp para a conexao 'index' no passo 0 e depois aplica gain */
    hda_send_verb(codec, nid, 0x37000 | ((uint32_t)index << 8));
    if (gain > 0) {
        hda_send_verb(codec, nid, 0x37000 | ((uint32_t)index << 8) | (uint32_t)(gain & 0x7F));
    }
}

/* Walk from a pin toward an output converter and program each selected edge. */
static bool hda_find_output_path(uint8_t codec, uint8_t nid, bool visited[128],
                                 uint8_t depth, uint32_t afg_out_amp_cap,
                                 uint32_t afg_in_amp_cap) {
    if (nid == 0 || nid >= 128 || visited[nid] || depth >= 32) return false;
    uint32_t caps = hda_send_verb(codec, nid, 0xF0009);
    uint8_t type = (uint8_t)((caps >> 20) & 0x0F);
    if (type == 0x0) {
        /* Audio Output Converter (DAC): garante D0, formato, stream 1 e desmute */
        hda_send_verb(codec, nid, 0x70500);
        hda_send_verb(codec, nid, 0x20011);
        hda_send_verb(codec, nid, 0x70610);
        hda_unmute_output_amp(codec, nid, caps, afg_out_amp_cap);
        hda_unmute_input_amp(codec, nid, caps, afg_in_amp_cap, 0);
        return true;
    }
    if (type != 0x2 && type != 0x3 && type != 0x4) return false;

    visited[nid] = true;
    uint8_t connections[128];
    uint8_t count = hda_get_connections(codec, nid, connections, sizeof(connections));
    bool found_any = false;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t source = connections[i];
        if (source == 0 || source >= 128 || visited[source]) continue;
        if (hda_find_output_path(codec, source, visited, depth + 1,
                                 afg_out_amp_cap, afg_in_amp_cap)) {
            if (!found_any && (type == 0x3 || type == 0x4) && count > 1) {
                hda_send_verb(codec, nid, 0x70100 | i);
            }
            hda_unmute_output_amp(codec, nid, caps, afg_out_amp_cap);
            hda_unmute_input_amp(codec, nid, caps, afg_in_amp_cap, i);
            found_any = true;
            /* Para Audio Mixers (type == 0x2, ex: NID 0x0C no Realtek), continua
             * habilitando todas as entradas que venham de DACs; para Pin/Selector,
             * basta a primeira rota valida ate um DAC. */
            if (type != 0x2) break;
        }
    }
    visited[nid] = false;
    return found_any;
}

/* Configura um Pin Complex de saída analógica (P2 Line-Out / Headphone / Speaker) */
static void hda_enable_output_pin(uint8_t codec, uint8_t nid, uint32_t caps,
                                  uint32_t pin_caps, uint8_t device,
                                  uint32_t afg_out_amp_cap, uint32_t afg_in_amp_cap) {
    hda_send_verb(codec, nid, 0x70500); /* Power D0 */
    /* Se o pino suportar Headphone Drive (bit 3 de pin_caps) ou for HP Out (device == 2),
     * ativa OUT_EN (0x40) | HP_EN (0x80) = 0xC0 para acionar o amplificador de fone/caixinha P2! */
    uint8_t pin_ctl = ((pin_caps & (1u << 3)) || device == 2) ? 0xC0 : 0x40;
    hda_send_verb(codec, nid, 0x70700 | pin_ctl);
    /* Liga EAPD (External Amplifier Power Down -> Bit 1 = 1 = Amplificador Externo Ligado) */
    hda_send_verb(codec, nid, 0x70C02);
    hda_unmute_output_amp(codec, nid, caps, afg_out_amp_cap);
    hda_unmute_input_amp(codec, nid, caps, afg_in_amp_cap, 0);
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

    /* Descobre o Audio Function Group (AFG) a partir do Root Node 0x00 (Intel HDA Spec 7.3.4.1) */
    uint8_t afg_nid = 0x01;
    uint32_t root_sub = hda_send_verb(codec, 0x00, 0xF0004);
    uint8_t fg_start = (root_sub >> 16) & 0xFF;
    uint8_t fg_count = root_sub & 0xFF;
    if (fg_start > 0 && fg_count > 0 && fg_count < 16) {
        for (uint8_t i = 0; i < fg_count; i++) {
            uint8_t fnid = (uint8_t)(fg_start + i);
            uint32_t fg_type = hda_send_verb(codec, fnid, 0xF0005);
            if ((fg_type & 0xFF) == 0x01) { /* 0x01 = Audio Function Group */
                afg_nid = fnid;
                break;
            }
        }
    }

    /* Liga o Audio Function Group (AFG) em D0 (ativo), ativa EAPD e GPIOs da placa-mae */
    hda_send_verb(codec, afg_nid, 0x70500);
    hda_sleep_ms(20);
    hda_send_verb(codec, afg_nid, 0x70C02); /* EAPD global no AFG */

    /* Muitas placas-mae controlam o transistor de mute do conector P2 via linhas GPIO do AFG! */
    uint32_t gpio_cap = hda_send_verb(codec, afg_nid, 0xF0011);
    uint8_t num_gpios = (uint8_t)(gpio_cap & 0xFF);
    if (num_gpios > 0 || vid == 0x10EC) {
        uint8_t gpio_mask = (num_gpios > 0 && num_gpios < 8) ? (uint8_t)((1u << num_gpios) - 1u) : 0xFF;
        hda_send_verb(codec, afg_nid, 0x71600 | gpio_mask); /* GPIO Enable Mask */
        hda_send_verb(codec, afg_nid, 0x71700 | gpio_mask); /* GPIO Direction = Output */
        hda_send_verb(codec, afg_nid, 0x71500 | gpio_mask); /* GPIO Data = High (Unmute P2 amp) */
    }

    /* Le as capacidades padrao de amplificador do AFG (usadas quando AmpParamOverride == 0) */
    uint32_t afg_out_amp_cap = hda_send_verb(codec, afg_nid, 0xF0012);
    uint32_t afg_in_amp_cap  = hda_send_verb(codec, afg_nid, 0xF000D);

    /* Consulta nós subordinados ao AFG */
    uint32_t sub_nodes = hda_send_verb(codec, afg_nid, 0xF0004);
    uint8_t start_nid = (sub_nodes >> 16) & 0xFF;
    uint8_t count = sub_nodes & 0xFF;
    if (count == 0 || start_nid == 0 || start_nid >= 128) return 0;
    if ((uint16_t)start_nid + count > 128) count = (uint8_t)(128 - start_nid);

    /* 1. Ativa todos os widgets em D0 e prepara os conversores de saída (DACs) e Mixers */
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
            hda_send_verb(codec, nid, 0x70C02); /* EAPD on DAC if supported */
            hda_unmute_output_amp(codec, nid, cap, afg_out_amp_cap);
            hda_unmute_input_amp(codec, nid, cap, afg_in_amp_cap, 0);
            if (hda_active_dac_count < sizeof(hda_active_dacs)) {
                hda_active_dacs[hda_active_dac_count++] = nid;
            }
        } else if (type == 0x2) {
            /* Audio Mixer (ex: NID 0x0C no Realtek): desmuta amplificador de saida e entradas de DAC */
            hda_unmute_output_amp(codec, nid, cap, afg_out_amp_cap);
            uint8_t conns[32];
            uint8_t nconn = hda_get_connections(codec, nid, conns, sizeof(conns));
            for (uint8_t i = 0; i < nconn && i < 16; i++) {
                uint32_t src_cap = hda_send_verb(codec, conns[i], 0xF0009);
                uint8_t src_type = (src_cap >> 20) & 0x0F;
                if (src_type == 0x0 || i == 0) {
                    hda_unmute_input_amp(codec, nid, cap, afg_in_amp_cap, i);
                }
            }
        }
    }

    /* 2. Roteia todos os Pin Complexes de saída analógica (Line-Out, Speaker, HP-Out, P2) */
    uint8_t routed = 0;
    for (int pass = 0; pass < 2; pass++) {
        for (uint16_t node = start_nid; node < (uint16_t)start_nid + count && node < 128; node++) {
            uint8_t nid = (uint8_t)node;
            uint32_t caps = hda_send_verb(codec, nid, 0xF0009);
            if (((caps >> 20) & 0x0F) != 0x4) continue; /* Apenas Pin Complex */

            /* Ignora pinos exclusivamente digitais (HDMI / S/PDIF: Bit 9 = Digital) na busca P2 analogica */
            bool is_digital = (caps & (1u << 9)) != 0;
            if (is_digital) continue;

            uint32_t pin_caps = hda_send_verb(codec, nid, 0xF000C);
            if (!(pin_caps & (1u << 4))) continue; /* Precisa ser Output Capable (Bit 4) */

            uint32_t config = hda_send_verb(codec, nid, 0xF001C);
            uint8_t device = (uint8_t)((config >> 20) & 0x0F);

            /* No Pass 0, habilita todos os pinos de saida (Line Out=0, Speaker=1, HP Out=2, Other=0xF)
             * mesmo que o BIOS tenha deixado connectivity==1 no Config Default!
             * No Pass 1 (se nada roteou no Pass 0), habilita qualquer pino analogico Output-Capable. */
            if (pass == 0) {
                if (config != 0 && config != 0xFFFFFFFFu &&
                    device > 2 && device != 0x0F) {
                    continue;
                }
            }

            bool visited[128] = { false };
            if (!hda_find_output_path(codec, nid, visited, 0,
                                      afg_out_amp_cap, afg_in_amp_cap))
                continue;

            hda_enable_output_pin(codec, nid, caps, pin_caps, device,
                                  afg_out_amp_cap, afg_in_amp_cap);
            routed++;
        }
        if (routed > 0) break;
    }

    /* Atualiza o codec ativo apenas se ele forneceu rotas analogicas reais (ou se nenhum outro forneceu) */
    if (routed > 0 || hda_active_vid == 0) {
        hda_active_slot = codec;
        hda_active_vid = vid;
        hda_active_did = did;

        if (vid == 0x10EC) {
            if (did == 0x0662) strcpy(hda_codec_name, "Realtek ALC662 (6-Canal HD Audio)");
            else if (did == 0x0887) strcpy(hda_codec_name, "Realtek ALC887 (7.1 HD Audio)");
            else if (did == 0x0892) strcpy(hda_codec_name, "Realtek ALC892 (7.1 HD Audio)");
            else if (did == 0x0897) strcpy(hda_codec_name, "Realtek ALC897 (HD Audio)");
            else if (did == 0x0888) strcpy(hda_codec_name, "Realtek ALC888 (HD Audio)");
            else if (did == 0x0256) strcpy(hda_codec_name, "Realtek ALC256 (HD Audio)");
            else if (did == 0x0269) strcpy(hda_codec_name, "Realtek ALC269 (Integrated Audio)");
            else strcpy(hda_codec_name, "Realtek High Definition Audio");
        } else if (vid == 0x1106) {
            strcpy(hda_codec_name, "VIA Vinyl HD Audio");
        } else if (vid == 0x111D) {
            strcpy(hda_codec_name, "IDT High Definition Audio");
        } else if (vid == 0x14F1) {
            strcpy(hda_codec_name, "Conexant SmartAudio HD");
        } else if (vid == 0x8086) {
            strcpy(hda_codec_name, "Intel Integrated HD Audio");
        } else if (vid == 0x11D4) {
            strcpy(hda_codec_name, "Analog Devices SoundMAX");
        } else if (vid == 0x1013) {
            strcpy(hda_codec_name, "Cirrus Logic HD Audio");
        } else if (vid == 0x10DE) {
            strcpy(hda_codec_name, "NVIDIA HDMI HD Audio");
        } else {
            strcpy(hda_codec_name, "HD Audio Codec Generico");
        }
    }

    return routed;
}

/* Acorda o controlador PCI em D0 e ativa PCIe Snoop / TC0 conforme Intel/AMD/NVIDIA specs */
static void hda_configure_pci_device(pci_device_t *pci) {
    /* 1. Percorre a lista de Capabilities PCI (offset 0x34) para:
     *    - Acordar Power Management (Cap ID 0x01) de D3 para D0
     *    - Desativar No-Snoop no PCI Express Device Control (Cap ID 0x10, offset +8, bit 11) */
    uint16_t status = (uint16_t)(pci_read_config(pci->bus, pci->device, pci->function, 0x04) >> 16);
    if (status & (1u << 4)) {
        uint8_t cap_ptr = (uint8_t)(pci_read_config(pci->bus, pci->device, pci->function, 0x34) & 0xFC);
        int guard = 0;
        while (cap_ptr >= 0x40 && guard++ < 32) {
            uint32_t cap_hdr = pci_read_config(pci->bus, pci->device, pci->function, cap_ptr);
            uint8_t cap_id = (uint8_t)(cap_hdr & 0xFF);
            uint8_t next_ptr = (uint8_t)((cap_hdr >> 8) & 0xFC);
            if (cap_id == 0x01) { /* PCI Power Management */
                uint32_t pmcsr = pci_read_config(pci->bus, pci->device, pci->function, cap_ptr + 4);
                if ((pmcsr & 0x03) != 0) {
                    pmcsr &= ~0x03u; /* D0 state */
                    pci_write_config(pci->bus, pci->device, pci->function, cap_ptr + 4, pmcsr);
                    hda_sleep_ms(10);
                }
            } else if (cap_id == 0x10) { /* PCI Express */
                uint32_t devctl = pci_read_config(pci->bus, pci->device, pci->function, cap_ptr + 8);
                if (devctl & (1u << 11)) {
                    devctl &= ~(1u << 11); /* Clear Enable No-Snoop */
                    pci_write_config(pci->bus, pci->device, pci->function, cap_ptr + 8, devctl);
                }
            }
            cap_ptr = next_ptr;
        }
    }

    /* 2. Intel PCH/ICH: TCSEL (offset 0x44) bits [2:0] = 0 (Traffic Class TC0) */
    if (pci->vendor_id == 0x8086) {
        uint32_t tcsel = pci_read_config(pci->bus, pci->device, pci->function, 0x44);
        tcsel &= ~0x07u;
        pci_write_config(pci->bus, pci->device, pci->function, 0x44, tcsel);
    }

    /* 3. AMD / ATI FCH & Ryzen HD Audio: offset 0x42 bit 1 = Enable Snoop (0x02) */
    if (pci->vendor_id == 0x1022 || pci->vendor_id == 0x1002) {
        uint32_t misc = pci_read_config(pci->bus, pci->device, pci->function, 0x40);
        /* Offset 0x42 corresponde aos bits [23:16] da dword 0x40 */
        misc = (misc & ~(0x07u << 16)) | (0x02u << 16);
        pci_write_config(pci->bus, pci->device, pci->function, 0x40, misc);
    }

    /* 4. Habilita Memory Space (bit 1) e Bus Master (bit 2) no PCI Command */
    uint32_t cmd_sts = pci_read_config(pci->bus, pci->device, pci->function, 0x04);
    cmd_sts |= (1u << 1) | (1u << 2);
    pci_write_config(pci->bus, pci->device, pci->function, 0x04, cmd_sts);
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
    uint8_t routed = 0;

    /* Testa cada controlador HD Audio ate encontrar um que possua codec e rota analogica (P2) */
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

        hda_configure_pci_device(pci);

        uint32_t bar0 = pci_read_config(pci->bus, pci->device, pci->function, 0x10);
        if (bar0 & 1) continue; /* I/O port, ignora */

        uint64_t mmio_phys = (uint64_t)(bar0 & ~0xF);
        if ((bar0 & 0x6) == 0x4) {
            uint32_t bar0_high = pci_read_config(pci->bus, pci->device, pci->function, 0x14);
            mmio_phys |= ((uint64_t)bar0_high << 32);
        }
        if (mmio_phys == 0) continue;

        /* Mapeia a regiao MMIO de 16KB no direct-map com Cache Desabilitado (PCD) */
        for (uintptr_t p = 0; p < 0x4000; p += 4096) {
            vmm_map_kernel_page((uint64_t)(mmio_phys + p),
                                (uint64_t)phys_to_virt(mmio_phys + p),
                                PTE_P | PTE_W | PTE_PCD | PTE_NX);
        }
        hda_mmio_base = (uintptr_t)phys_to_virt(mmio_phys);

        /* Reset do controlador via GCTL.CRST */
        hda_write32(HDA_REG_GCTL, 0);
        for (int i = 0; i < 5000; i++) {
            if (!(hda_read32(HDA_REG_GCTL) & HDA_GCTL_CRST)) break;
            hda_delay_us(10);
        }
        hda_sleep_ms(2);

        hda_write32(HDA_REG_GCTL, HDA_GCTL_CRST);
        for (int i = 0; i < 5000; i++) {
            if (hda_read32(HDA_REG_GCTL) & HDA_GCTL_CRST) break;
            hda_delay_us(10);
        }
        /* Aguarda no minimo 1ms (Intel HDA Spec 4.3: 521 us min) para codecs sinalizarem STATESTS */
        hda_sleep_ms(2);

        /* Desativa DMA Position Buffer e zera SSYNC para nao congelar o link */
        hda_write32(0x70, 0);
        hda_write32(0x74, 0);
        hda_write32(0x34, 0);
        hda_write32(0x38, 0);

        /* Aguarda estabilização dos codecs analógicos na linha HDA Link (até 100 ms) */
        uint16_t statests = 0;
        for (int t = 0; t < 100; t++) {
            statests = hda_read16(HDA_REG_STATESTS);
            if (statests != 0) break;
            hda_sleep_ms(1);
        }

        serial_print("[hda] STATESTS lido: 0x");
        serial_print_hex(statests);
        serial_print("\n");

        if (statests == 0) continue;

        /* Inicializa anéis de comando DMA CORB e RIRB neste controlador */
        if (hda_init_corb_rirb() != 0) {
            serial_print("[hda] Falha ao inicializar CORB/RIRB.\n");
            continue;
        }

        hda_active_dac_count = 0;
        uint8_t ctrl_routed = 0;
        for (int c = 0; c < 15; c++) {
            if (statests & (1 << c)) {
                uint8_t count = hda_setup_codec((uint8_t)c);
                if ((uint16_t)ctrl_routed + count > 255) ctrl_routed = 255;
                else ctrl_routed = (uint8_t)(ctrl_routed + count);
            }
        }

        if (ctrl_routed > 0) {
            selected_pci = pci;
            found_statests = statests;
            routed = ctrl_routed;
            break;
        }
        serial_print("[hda] Controlador sem rotas analogicas (provavel HDMI); testando proximo...\n");
    }

    if (!selected_pci || found_statests == 0 || hda_active_vid == 0 || routed == 0) {
        serial_print("[hda] Codec ou rota analogica de saida nao encontrado.\n");
        return -3;
    }

    hda_active_pci = selected_pci;
    hda_active_statests = found_statests;
    hda_output_routes = routed;

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
        /* Mapeia todas as paginas do buffer PCM DMA com Cache Disable (PTE_PCD)
         * para que escritas da CPU cheguem imediatamente a DRAM mesmo sob PCIe No-Snoop! */
        for (uint64_t off = 0; off < HDA_BUFFER_BYTES; off += 4096) {
            vmm_map_kernel_page(hda_dma_buffer_phys + off,
                                (uint64_t)phys_to_virt(hda_dma_buffer_phys + off),
                                PTE_P | PTE_W | PTE_PCD | PTE_NX);
        }
    }
    if (!hda_dma_buffer || !hda_dma_buffer_phys) return -4;
    memset(hda_dma_buffer, 0, HDA_BUFFER_BYTES);

    /* Aloca lista de descritores de buffer (BDL) usando página pura do PMM com Cache Disable */
    if (!hda_bdl) {
        void *bdl_page = pmm_alloc_block();
        if (!bdl_page) return -4;
        hda_bdl_phys = virt_to_phys(bdl_page);
        vmm_map_kernel_page(hda_bdl_phys, (uint64_t)bdl_page,
                            PTE_P | PTE_W | PTE_PCD | PTE_NX);
        memset(bdl_page, 0, 4096);
        hda_bdl = (volatile hda_bdl_entry_t *)bdl_page;
    }

    /* Divide o buffer de 32 KB em 2 fatias de 16 KB no BDL */
    uint32_t half_bytes = HDA_BUFFER_BYTES / 2;
    hda_bdl[0].addr = hda_dma_buffer_phys;
    hda_bdl[0].length = half_bytes;
    hda_bdl[0].ioc = 0;

    hda_bdl[1].addr = hda_dma_buffer_phys + half_bytes;
    hda_bdl[1].length = half_bytes;
    hda_bdl[1].ioc = 0;
    asm volatile("mfence" ::: "memory");

    /* Determina offset do primeiro Output Stream */
    uint16_t gcap = hda_read16(HDA_REG_GCAP);
    uint8_t num_iss = (gcap >> 8) & 0x0F;
    uint8_t num_oss = (gcap >> 12) & 0x0F;
    if (num_oss == 0) return -5;
    /* Output Stream Descriptor 0 follows the controller's input descriptors. */
    hda_stream_offset = 0x80 + (uint32_t)num_iss * 0x20;
    uint32_t so = hda_stream_offset;

    /* 1. Para e reseta o Stream Descriptor (Ciclo SRST obrigatório da Intel Spec 4.5.1) */
    hda_write8(so + HDA_SD_CTL, 0);
    for (int i = 0; i < 2000; i++) {
        if (!(hda_read8(so + HDA_SD_CTL) & HDA_SD_CTL_RUN)) break;
        hda_delay_us(10);
    }

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

    /* 7. Configura Stream Tag 1 (bits [7:4] de SD_CTL+2) e inicia RUN (bit 1 de SD_CTL+0) */
    hda_write8(so + HDA_SD_CTL + 2, 0x10); /* Stream Tag = 1 */
    hda_write8(so + HDA_SD_CTL, HDA_SD_CTL_RUN);

    for (int i = 0; i < 5000; i++) {
        if (hda_read8(so + HDA_SD_CTL) & HDA_SD_CTL_RUN) break;
        hda_delay_us(10);
    }
    if (!(hda_read8(so + HDA_SD_CTL) & HDA_SD_CTL_RUN)) {
        serial_print("[hda] O stream de saida nao iniciou (RUN=0).\n");
        return -5;
    }

    /* 8. Ressincroniza os DACs com o Stream 1 ja ativo no link HDA */
    for (uint8_t i = 0; i < hda_active_dac_count; i++) {
        hda_send_verb(hda_active_slot, hda_active_dacs[i], 0x20011);
        hda_send_verb(hda_active_slot, hda_active_dacs[i], 0x70610);
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

static void hda_ensure_stream_running(void) {
    if (!hda_initialized || !hda_mmio_base) return;
    uint32_t so = hda_stream_offset;
    uint8_t sts = hda_read8(so + HDA_SD_STS);
    if (sts & 0x1C) {
        hda_write8(so + HDA_SD_STS, 0x1C);
    }
    if (!(hda_read8(so + HDA_SD_CTL) & HDA_SD_CTL_RUN)) {
        hda_write8(so + HDA_SD_CTL + 2, 0x10);
        hda_write8(so + HDA_SD_CTL, HDA_SD_CTL_RUN);
    }
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
        asm volatile("mfence" ::: "memory");
        hda_ensure_stream_running();

        hda_sleep_ms(duration_ms);
        memset(hda_dma_buffer, 0, HDA_BUFFER_BYTES);
        asm volatile("mfence" ::: "memory");
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
        asm volatile("mfence" ::: "memory");
        hda_ensure_stream_running();

        played += chunk;
        uint32_t chunk_ms = (chunk * 1000) / sample_rate;
        if (chunk_ms > 0) {
            hda_sleep_ms(chunk_ms);
        }
    }

    memset(hda_dma_buffer, 0, HDA_BUFFER_BYTES);
    asm volatile("mfence" ::: "memory");
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
