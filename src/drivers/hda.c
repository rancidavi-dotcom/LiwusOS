#include "hda.h"
#include "kheap.h"
#include "pci.h"
#include "serial.h"
#include "string.h"
#include "task.h"

/*
 * Intel High Definition Audio (HDA) driver for LiwusOS.
 *
 * The controller exposes a 16 KiB memory-mapped register window in BAR0.
 * Codec verbs are issued through the "Immediate Command" interface (IC/IR/
 * IRS at 0x60/0x64/0x68) instead of the CORB/RIRB DMA rings, which keeps the
 * initialization path simple and poll-free of ring-pointer bookkeeping.
 *
 * Playback uses one output stream descriptor (stream index 4, the first
 * output stream).  A 32-entry buffer descriptor list points at 32 fixed
 * 1024-frame chunks; the engine reads them in a ring.  The producer callback
 * refills already-consumed chunks while the driver polls SD_LPIB, mirroring
 * the polling design of the AC'97 driver (no IRQ routing required).  QEMU
 * snapshots the BDL when the stream starts, so the descriptors are written
 * once with fixed addresses and only the PCM contents change.
 */

extern void vmm_map_page(void *phys, void *virt, uint64_t flags);

/* ---- Controller registers (BAR0 byte offsets) ---- */
#define HDA_GCAP     0x00
#define HDA_VMIN     0x02
#define HDA_VMAJ     0x03
#define HDA_GCTL     0x08
#define HDA_STATESTS 0x0E
#define HDA_IR       0x64
#define HDA_IRS      0x68
#define HDA_IC       0x60

#define HDA_GCTL_CRST 0x00000001

/* Stream descriptors start at 0x80, one per 0x20 bytes. */
#define HDA_SD_BASE(n) (0x80 + (n) * 0x20)
#define HDA_SD_CTL     0x00
#define HDA_SD_STS     0x03
#define HDA_SD_LPIB    0x04
#define HDA_SD_CBL     0x08
#define HDA_SD_LVI     0x0C
#define HDA_SD_FMT     0x12
#define HDA_SD_BDLPL   0x18
#define HDA_SD_BDLPU   0x1C

#define HDA_SD_CTL_SRST 0x00000001
#define HDA_SD_CTL_RUN  0x00000002

/* ---- Codec verbs ---- */
#define HDA_VERB_GET_PARAM      0x0F00
#define HDA_VERB_GET_CONN_LIST  0x0F02
#define HDA_VERB_SET_STREAM_FMT 0x0200
#define HDA_VERB_SET_AMP        0x0300
#define HDA_VERB_SET_CONN_SEL   0x0701
#define HDA_VERB_SET_POWER      0x0705
#define HDA_VERB_SET_STREAM_ID  0x0706
#define HDA_VERB_SET_PIN_CTL    0x0707
#define HDA_VERB_SET_EAPD       0x070C

/* ---- Parameters ---- */
#define HDA_PAR_VENDOR_ID   0x00
#define HDA_PAR_NODE_COUNT  0x04
#define HDA_PAR_FUNC_TYPE   0x05
#define HDA_PAR_WIDGET_CAP  0x09
#define HDA_PAR_PIN_CAP     0x0C
#define HDA_PAR_CONNLIST    0x0E
#define HDA_PAR_AMP_OUT_CAP 0x12

/* ---- Widget types ---- */
#define HDA_WID_AUD_OUT 0x0
#define HDA_WID_AUD_IN  0x1
#define HDA_WID_AUD_MIX 0x2
#define HDA_WID_AUD_SEL 0x3
#define HDA_WID_PIN     0x4

#define HDA_GRP_AUDIO_FUNCTION 0x01

#define HDA_PINCAP_OUT    0x00000010
#define HDA_PINCAP_EAPD   0x00010000
#define HDA_PINCTL_OUT_EN 0x40

#define HDA_AMP_SET_OUTPUT (1u << 15)
#define HDA_AMP_SET_LEFT   (1u << 13)
#define HDA_AMP_SET_RIGHT  (1u << 12)
#define HDA_AMP_MUTE       (1u << 7)

#define HDA_RING_ENTRIES 32
#define HDA_CHUNK_FRAMES 1024
#define HDA_CHUNK_BYTES  (HDA_CHUNK_FRAMES * 4)
#define HDA_RING_BYTES   (HDA_RING_ENTRIES * HDA_CHUNK_BYTES)

#define HDA_OUT_SD  4 /* first playback stream descriptor */
#define HDA_OUT_TAG 1 /* stream tag bound to the DAC */

typedef struct {
    uint64_t addr;
    uint32_t len;
    uint32_t flags;
} __attribute__((packed)) hda_bdl_t;

static int hda_present;
static int hda_volume = 100;
static int hda_muted;
static uint32_t hda_rate = AUDIO_DEFAULT_RATE;
static volatile int hda_busy;

static volatile uint8_t *hda_mmio;
static uint8_t hda_cad = 0xFF;
static uint8_t hda_dac_nid = 0xFF;
static uint8_t hda_pin_nid = 0xFF;

static hda_bdl_t *hda_bdl;
static int16_t *hda_chunks[HDA_RING_ENTRIES];

/* ------------------------------------------------------------------ */
/* Register access                                                    */
/* ------------------------------------------------------------------ */

static inline uint8_t hda_r8(uint32_t off) {
    return *(volatile uint8_t *)(hda_mmio + off);
}
static inline uint16_t hda_r16(uint32_t off) {
    return *(volatile uint16_t *)(hda_mmio + off);
}
static inline uint32_t hda_r32(uint32_t off) {
    return *(volatile uint32_t *)(hda_mmio + off);
}
static inline void hda_w8(uint32_t off, uint8_t v) {
    *(volatile uint8_t *)(hda_mmio + off) = v;
}
static inline void hda_w16(uint32_t off, uint16_t v) {
    *(volatile uint16_t *)(hda_mmio + off) = v;
}
static inline void hda_w32(uint32_t off, uint32_t v) {
    *(volatile uint32_t *)(hda_mmio + off) = v;
}

static void hda_delay(uint32_t iters) {
    for (volatile uint32_t i = 0; i < iters; i++)
        asm volatile("pause");
}

/* ------------------------------------------------------------------ */
/* Codec command interface (Immediate Command)                        */
/* ------------------------------------------------------------------ */

static uint32_t hda_cmd(uint8_t cad, uint8_t nid, uint16_t verb,
                        uint16_t payload) {
    return ((uint32_t)cad << 28) | ((uint32_t)nid << 20) |
           ((uint32_t)verb << 8) | (uint32_t)payload;
}

static int hda_ic(uint32_t cmd, uint32_t *resp) {
    uint32_t timeout = 500000;

    while ((hda_r16(HDA_IRS) & 0x1) && --timeout)
        asm volatile("pause");
    if (timeout == 0)
        return -1;

    hda_w32(HDA_IC, cmd);
    hda_w16(HDA_IRS, 0x1); /* start command, clears result-valid */

    timeout = 500000;
    while (!(hda_r16(HDA_IRS) & 0x2) && --timeout)
        asm volatile("pause");
    if (timeout == 0)
        return -1;

    if (resp)
        *resp = hda_r32(HDA_IR);
    hda_w16(HDA_IRS, 0x0); /* clear result-valid for the next command */
    return 0;
}

static uint32_t hda_param(uint8_t nid, uint8_t param) {
    uint32_t v = 0xFFFFFFFF;
    if (hda_ic(hda_cmd(hda_cad, nid, HDA_VERB_GET_PARAM, param), &v) != 0)
        return 0xFFFFFFFF;
    return v;
}

static int hda_widget_type(uint8_t nid) {
    uint32_t cap = hda_param(nid, HDA_PAR_WIDGET_CAP);
    if (cap == 0xFFFFFFFF)
        return -1;
    return (int)((cap >> 20) & 0xF);
}

static int hda_conn_count(uint8_t nid) {
    uint32_t v = hda_param(nid, HDA_PAR_CONNLIST);
    if (v == 0xFFFFFFFF)
        return 0;
    return (int)(v & 0x7F); /* bit15 = long form (unsupported entries) */
}

static uint8_t hda_conn_entry(uint8_t nid, int idx) {
    uint32_t v = 0;
    int base = idx & ~3;
    if (hda_ic(hda_cmd(hda_cad, nid, HDA_VERB_GET_CONN_LIST, (uint16_t)base),
               &v) != 0)
        return 0;
    return (uint8_t)((v >> (8 * (idx & 3))) & 0xFF);
}

static void hda_set_conn_sel(uint8_t nid, int idx) {
    hda_ic(hda_cmd(hda_cad, nid, HDA_VERB_SET_CONN_SEL, (uint16_t)idx), 0);
}

/* Depth-first search from an output pin down to a DAC widget. */
static int hda_find_dac(uint8_t nid, int depth) {
    if (depth > 8)
        return -1;
    int type = hda_widget_type(nid);
    if (type == HDA_WID_AUD_OUT)
        return (int)nid;
    if (type != HDA_WID_AUD_MIX && type != HDA_WID_AUD_SEL &&
        type != HDA_WID_PIN)
        return -1;

    int n = hda_conn_count(nid);
    for (int i = 0; i < n; i++) {
        uint8_t child = hda_conn_entry(nid, i);
        if (!child)
            continue;
        int ctype = hda_widget_type(child);
        if (ctype == HDA_WID_AUD_OUT) {
            if (type == HDA_WID_AUD_SEL)
                hda_set_conn_sel(nid, i);
            return (int)child;
        }
        if (ctype == HDA_WID_AUD_MIX || ctype == HDA_WID_AUD_SEL) {
            int dac = hda_find_dac(child, depth + 1);
            if (dac >= 0) {
                if (type == HDA_WID_AUD_SEL)
                    hda_set_conn_sel(nid, i);
                return dac;
            }
        }
    }
    return -1;
}

/* ------------------------------------------------------------------ */
/* Mixer / amp                                                        */
/* ------------------------------------------------------------------ */

static void hda_amp_update(void) {
    if (!hda_present)
        return;
    uint32_t ampcap = hda_param(hda_dac_nid, HDA_PAR_AMP_OUT_CAP);
    /* Gain index that corresponds to 0 dB (maximum volume). */
    uint32_t offset = (ampcap == 0xFFFFFFFF) ? 0 : (ampcap & 0x7F);
    uint32_t amp = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
                   (offset & 0x7F);
    if (hda_muted)
        amp |= HDA_AMP_MUTE;
    hda_ic(hda_cmd(hda_cad, hda_dac_nid, HDA_VERB_SET_AMP, (uint16_t)amp), 0);
}

void hda_set_volume(int percent) {
    if (percent < 0)
        percent = 0;
    if (percent > 100)
        percent = 100;
    hda_volume = percent;
    hda_amp_update();
}

int hda_get_volume(void) {
    return hda_volume;
}

void hda_set_muted(int muted) {
    hda_muted = muted ? 1 : 0;
    hda_amp_update();
}

int hda_get_muted(void) {
    return hda_muted;
}

/* ------------------------------------------------------------------ */
/* Playback stream                                                    */
/* ------------------------------------------------------------------ */

/* HDA stream format word: 16-bit stereo PCM at the requested rate. */
static uint16_t hda_format_for_rate(uint32_t hz) {
    switch (hz) {
    case 48000: return 0x0011;
    case 44100: return 0x4011;
    case 96000: return 0x0811; /* 48k x2   */
    case 88200: return 0x4811; /* 44.1k x2 */
    case 32000: return 0x1311; /* 48k x2/3 */
    case 22050: return 0x4111; /* 44.1k /2 */
    case 24000: return 0x0111; /* 48k /2   */
    case 16000: return 0x0211; /* 48k /3   */
    case 11025: return 0x4311; /* 44.1k /4 */
    case 8000:  return 0x0611; /* 48k /6   */
    default:    return 0x0011;
    }
}

static void hda_stream_stop(void) {
    hda_w32(HDA_SD_BASE(HDA_OUT_SD) + HDA_SD_CTL, 0);
}

static void hda_stream_setup(uint16_t fmt) {
    uint32_t base = HDA_SD_BASE(HDA_OUT_SD);
    uint32_t timeout;

    hda_w32(base + HDA_SD_CTL, 0);
    hda_delay(1000);
    hda_w32(base + HDA_SD_CTL, HDA_SD_CTL_SRST);
    timeout = 100000;
    while (!(hda_r32(base + HDA_SD_CTL) & HDA_SD_CTL_SRST) && --timeout)
        asm volatile("pause");
    hda_w32(base + HDA_SD_CTL, 0);
    timeout = 100000;
    while ((hda_r32(base + HDA_SD_CTL) & HDA_SD_CTL_SRST) && --timeout)
        asm volatile("pause");

    hda_w16(base + HDA_SD_FMT, fmt);
    hda_w32(base + HDA_SD_BDLPL, (uint32_t)(uintptr_t)hda_bdl);
    hda_w32(base + HDA_SD_BDLPU, 0);
    hda_w32(base + HDA_SD_CBL, HDA_RING_BYTES);
    hda_w16(base + HDA_SD_LVI, HDA_RING_ENTRIES - 1);
    hda_w8(base + HDA_SD_STS, 0x1C); /* write-1-clear status */
}

static void hda_stream_start(void) {
    uint32_t base = HDA_SD_BASE(HDA_OUT_SD);
    hda_w32(base + HDA_SD_CTL,
            ((uint32_t)HDA_OUT_TAG << 20) | HDA_SD_CTL_RUN);
}

static uint32_t hda_play_pos(void) {
    uint32_t lpib = hda_r32(HDA_SD_BASE(HDA_OUT_SD) + HDA_SD_LPIB);
    return (lpib / HDA_CHUNK_BYTES) % HDA_RING_ENTRIES;
}

static int hda_run(audio_stream_fill_t fill, void *user, volatile int *stop) {
    uint32_t fill_idx = 0;
    uint32_t last_pos = 0;
    uint32_t free_slots = 0;
    uint32_t consumed = 0;
    uint32_t fed = 0;     /* absolute number of full chunks produced so far */
    uint32_t eof_abs = 0; /* absolute index of the first partial chunk */
    uint32_t first_got = 0;
    int eof = 0;

    /* Prefill the whole ring and snapshot the buffer descriptor list. */
    for (uint32_t i = 0; i < HDA_RING_ENTRIES; i++) {
        uint32_t got = fill(user, hda_chunks[i], HDA_CHUNK_FRAMES);
        hda_bdl[i].addr = (uint64_t)(uintptr_t)hda_chunks[i];
        hda_bdl[i].len = HDA_CHUNK_BYTES;
        hda_bdl[i].flags = 0;
        if (i == 0)
            first_got = got;
        if (got < HDA_CHUNK_FRAMES) {
            eof = 1;
            eof_abs = fed;
            for (uint32_t j = i + 1; j < HDA_RING_ENTRIES; j++) {
                hda_bdl[j].addr = (uint64_t)(uintptr_t)hda_chunks[j];
                hda_bdl[j].len = HDA_CHUNK_BYTES;
                hda_bdl[j].flags = 0;
                memset(hda_chunks[j], 0, HDA_CHUNK_BYTES);
            }
            break;
        }
        fed++;
        fill_idx = i + 1;
    }
    if (eof && eof_abs == 0 && first_got == 0)
        return 0; /* empty source */
    if (fill_idx >= HDA_RING_ENTRIES)
        fill_idx = 0;

    uint16_t fmt = hda_format_for_rate(hda_rate);
    hda_ic(hda_cmd(hda_cad, hda_dac_nid, HDA_VERB_SET_STREAM_FMT, fmt), 0);
    hda_ic(hda_cmd(hda_cad, hda_dac_nid, HDA_VERB_SET_STREAM_ID,
                   (uint16_t)((HDA_OUT_TAG << 4) | 0)), 0);
    hda_stream_setup(fmt);
    hda_stream_start();

    while (hda_busy) {
        if (stop && !*stop)
            break;

        uint32_t pos = hda_play_pos();
        uint32_t delta =
            (pos + HDA_RING_ENTRIES - last_pos) & (HDA_RING_ENTRIES - 1);
        free_slots += delta;
        if (free_slots > HDA_RING_ENTRIES)
            free_slots = HDA_RING_ENTRIES;
        consumed += delta;
        last_pos = pos;

        if (eof && consumed > eof_abs)
            break; /* last real chunk has been played out */

        while (!eof && free_slots > 0) {
            uint32_t got = fill(user, hda_chunks[fill_idx], HDA_CHUNK_FRAMES);
            free_slots--;
            fill_idx = (fill_idx + 1) & (HDA_RING_ENTRIES - 1);
            if (got < HDA_CHUNK_FRAMES) {
                eof = 1;
                eof_abs = fed;
            } else {
                fed++;
            }
        }

        if (free_slots == 0)
            switch_task();
        else
            for (volatile int b = 0; b < 4000; b++)
                asm volatile("pause");
    }

    hda_stream_stop();
    return 0;
}

int hda_play(audio_stream_fill_t fill, void *user, uint32_t hz,
             int volume_percent, volatile int *stop) {
    if (!hda_present || hda_busy || !fill)
        return -1;
    (void)volume_percent;
    if (hz)
        hda_set_rate(hz);
    hda_busy = 1;
    int rc = hda_run(fill, user, stop);
    hda_busy = 0;
    return rc;
}

void hda_stop(void) {
    if (!hda_present)
        return;
    hda_busy = 0;
    hda_stream_stop();
}

int hda_is_busy(void) {
    return hda_busy;
}

int hda_set_rate(uint32_t hz) {
    if (hz < 8000 || hz > 96000)
        hz = AUDIO_DEFAULT_RATE;
    hda_rate = hz;
    return 0;
}

uint32_t hda_get_rate(void) {
    return hda_rate;
}

/* ------------------------------------------------------------------ */
/* Setup                                                              */
/* ------------------------------------------------------------------ */

int hda_init(void) {
    if (hda_present)
        return 0;

    pci_device_t *dev = pci_get_hda();
    if (!dev)
        return -1;

    uint32_t bar0 =
        pci_read_config(dev->bus, dev->device, dev->function, 0x10);
    uint32_t bar1 =
        pci_read_config(dev->bus, dev->device, dev->function, 0x14);
    if (bar0 & 1) {
        serial_print("HDA: BAR0 nao e memory-mapped\n");
        return -1;
    }
    uint64_t phys = (uint64_t)(bar0 & 0xFFFFFFF0u);
    if ((bar0 & 0x6) == 0x4) /* 64-bit BAR */
        phys |= ((uint64_t)bar1 << 32);

    uint32_t cmd =
        pci_read_config(dev->bus, dev->device, dev->function, 0x04);
    cmd |= 0x02; /* memory space enable */
    cmd |= 0x04; /* bus mastering enable (DMA) */
    pci_write_config(dev->bus, dev->device, dev->function, 0x04, cmd);

    /* BAR0 covers 16 KiB: four 4 KiB pages. */
    for (int i = 0; i < 4; i++)
        vmm_map_page((void *)(uintptr_t)(phys + i * 4096),
                     (void *)(uintptr_t)(phys + i * 4096), 3);
    hda_mmio = (volatile uint8_t *)(uintptr_t)phys;

    serial_print("HDA: BAR0=0x");
    serial_print_hex(phys);
    serial_print(" GCAP=0x");
    serial_print_hex(hda_r16(HDA_GCAP));
    serial_print(" VMIN=0x");
    serial_print_hex(hda_r8(HDA_VMIN));
    serial_print("\n");

    /* Controller reset: clear CRST, then assert it. */
    hda_w32(HDA_GCTL, 0);
    hda_delay(10000);
    hda_w32(HDA_GCTL, HDA_GCTL_CRST);
    hda_delay(10000);
    for (uint32_t t = 0; t < 100000 && (hda_r32(HDA_GCTL) & HDA_GCTL_CRST); t++)
        asm volatile("pause");
    hda_delay(100000);

    /* Probe codec addresses (STATESTS is advisory; ask each codec). */
    for (uint8_t cad = 0; cad < 15; cad++) {
        uint32_t vid = 0xFFFFFFFF;
        if (hda_ic(hda_cmd(cad, 0, HDA_VERB_GET_PARAM, HDA_PAR_VENDOR_ID),
                   &vid) == 0 &&
            vid != 0xFFFFFFFF && vid != 0) {
            hda_cad = cad;
            serial_print("HDA: codec cad=");
            serial_print_hex(cad);
            serial_print(" vendor=0x");
            serial_print_hex(vid);
            serial_print("\n");
            break;
        }
    }
    if (hda_cad == 0xFF) {
        serial_print("HDA: nenhum codec respondeu\n");
        return -1;
    }

    /* Locate the audio function group. */
    uint32_t nc = hda_param(0, HDA_PAR_NODE_COUNT);
    uint8_t start = (nc == 0xFFFFFFFF) ? 1 : (uint8_t)((nc >> 16) & 0xFF);
    uint8_t count = (nc == 0xFFFFFFFF) ? 1 : (uint8_t)(nc & 0xFF);
    uint8_t func = 1;
    for (uint8_t nid = start; nid < (uint8_t)(start + count); nid++) {
        uint32_t ft = hda_param(nid, HDA_PAR_FUNC_TYPE);
        if (ft != 0xFFFFFFFF && (ft & 0xFF) == HDA_GRP_AUDIO_FUNCTION) {
            func = nid;
            break;
        }
    }

    /* Walk the widget list looking for an output pin feeding a DAC. */
    nc = hda_param(func, HDA_PAR_NODE_COUNT);
    start = (nc == 0xFFFFFFFF) ? 2 : (uint8_t)((nc >> 16) & 0xFF);
    count = (nc == 0xFFFFFFFF) ? 0 : (uint8_t)(nc & 0xFF);
    for (uint8_t nid = start;
         nid < (uint8_t)(start + count) && hda_dac_nid == 0xFF; nid++) {
        if (hda_widget_type(nid) != HDA_WID_PIN)
            continue;
        uint32_t pincap = hda_param(nid, HDA_PAR_PIN_CAP);
        if (pincap == 0xFFFFFFFF || !(pincap & HDA_PINCAP_OUT))
            continue;
        int dac = hda_find_dac(nid, 0);
        if (dac >= 0) {
            hda_pin_nid = nid;
            hda_dac_nid = (uint8_t)dac;
        }
    }
    if (hda_dac_nid == 0xFF) {
        serial_print("HDA: nenhum caminho DAC/pino encontrado\n");
        return -1;
    }
    serial_print("HDA: pin=");
    serial_print_hex(hda_pin_nid);
    serial_print(" dac=");
    serial_print_hex(hda_dac_nid);
    serial_print("\n");

    /* Buffer descriptor list must be 128-byte aligned: use a page. */
    hda_bdl = (hda_bdl_t *)kmalloc_a(sizeof(hda_bdl_t) * HDA_RING_ENTRIES);
    if (!hda_bdl)
        return -1;
    memset(hda_bdl, 0, sizeof(hda_bdl_t) * HDA_RING_ENTRIES);
    for (int i = 0; i < HDA_RING_ENTRIES; i++) {
        hda_chunks[i] = (int16_t *)kmalloc(HDA_CHUNK_BYTES);
        if (!hda_chunks[i])
            return -1;
        memset(hda_chunks[i], 0, HDA_CHUNK_BYTES);
    }

    /* Configure pin and DAC. */
    uint32_t pincap = hda_param(hda_pin_nid, HDA_PAR_PIN_CAP);
    hda_ic(hda_cmd(hda_cad, hda_pin_nid, HDA_VERB_SET_PIN_CTL,
                   HDA_PINCTL_OUT_EN), 0);
    if (pincap != 0xFFFFFFFF && (pincap & HDA_PINCAP_EAPD))
        hda_ic(hda_cmd(hda_cad, hda_pin_nid, HDA_VERB_SET_EAPD, 0x02), 0);
    hda_ic(hda_cmd(hda_cad, hda_dac_nid, HDA_VERB_SET_POWER, 0x00), 0);

    hda_present = 1;
    hda_set_volume(100);
    hda_set_rate(AUDIO_DEFAULT_RATE);

    serial_print("HDA: driver pronto (16-bit stereo, ");
    serial_print_hex(hda_rate);
    serial_print(" Hz)\n");
    return 0;
}

int hda_available(void) {
    return hda_present;
}
