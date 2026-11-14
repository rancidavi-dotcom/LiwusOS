#ifndef HDA_H
#define HDA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "pci.h"

/* Registradores do Controlador Intel HDA (Offsets MMIO) */
#define HDA_REG_GCAP       0x00  /* Global Capabilities (16-bit) */
#define HDA_REG_VMIN       0x02  /* Minor Version (8-bit) */
#define HDA_REG_VMAJ       0x03  /* Major Version (8-bit) */
#define HDA_REG_OUTPAY     0x04  /* Output Payload Capability (16-bit) */
#define HDA_REG_INPAY      0x06  /* Input Payload Capability (16-bit) */
#define HDA_REG_GCTL       0x08  /* Global Control (32-bit) */
#define HDA_REG_WAKEEN     0x0C  /* Wake Enable (16-bit) */
#define HDA_REG_STATESTS   0x0E  /* State Change Status (16-bit) */
#define HDA_REG_GSTS       0x10  /* Global Status (16-bit) */
#define HDA_REG_INTCTL     0x20  /* Interrupt Control (32-bit) */
#define HDA_REG_INTSTS     0x24  /* Interrupt Status (32-bit) */
#define HDA_REG_CORBLBASE  0x40  /* CORB Lower Base Address (32-bit) */
#define HDA_REG_CORBUBASE  0x44  /* CORB Upper Base Address (32-bit) */
#define HDA_REG_CORBWP     0x48  /* CORB Write Pointer (16-bit) */
#define HDA_REG_CORBRP     0x4A  /* CORB Read Pointer (16-bit, bit 15 = RST) */
#define HDA_REG_CORBCTL    0x4C  /* CORB Control (8-bit, bit 1 = RUN) */
#define HDA_REG_CORBSTS    0x4D  /* CORB Status (8-bit) */
#define HDA_REG_CORBSIZE   0x4E  /* CORB Size (8-bit) */
#define HDA_REG_RIRBLBASE  0x50  /* RIRB Lower Base Address (32-bit) */
#define HDA_REG_RIRBUBASE  0x54  /* RIRB Upper Base Address (32-bit) */
#define HDA_REG_RIRBWP     0x58  /* RIRB Write Pointer (16-bit, bit 15 = RST) */
#define HDA_REG_RINTCNT    0x5A  /* Response Interrupt Count (16-bit) */
#define HDA_REG_RIRBCTL    0x5C  /* RIRB Control (8-bit, bit 1 = DMA_EN, bit 0 = IRQ_EN) */
#define HDA_REG_RIRBSTS    0x5D  /* RIRB Status (8-bit) */
#define HDA_REG_RIRBSIZE   0x5E  /* RIRB Size (8-bit) */
#define HDA_REG_ICO        0x60  /* Immediate Command Output (32-bit) */
#define HDA_REG_ICI        0x64  /* Immediate Command Input (32-bit) */
#define HDA_REG_ICS        0x68  /* Immediate Command Status (16-bit) */

/* Flags de GCTL */
#define HDA_GCTL_CRST      (1 << 0) /* Controller Reset (1 = Run, 0 = Reset) */

/* Flags de ICS */
#define HDA_ICS_ICB        (1 << 0) /* Immediate Command Busy */
#define HDA_ICS_IRV        (1 << 1) /* Immediate Result Valid */

/* Offsets dos registradores de Stream Descriptor */
#define HDA_SD_CTL         0x00 /* Stream Control (24/32-bit) */
#define HDA_SD_STS         0x03 /* Stream Status (8-bit) */
#define HDA_SD_LPIB        0x04 /* Link Position in Buffer (32-bit) */
#define HDA_SD_CBL         0x08 /* Cyclic Buffer Length (32-bit) */
#define HDA_SD_LVI         0x0C /* Last Valid Index (16-bit) */
#define HDA_SD_FIFOS       0x0E /* FIFO Size (16-bit) */
#define HDA_SD_FMT         0x12 /* Format (16-bit) */
#define HDA_SD_BDLPL       0x18 /* BDL Pointer Lower (32-bit) */
#define HDA_SD_BDLPU       0x1C /* BDL Pointer Upper (32-bit) */

#define HDA_SD_CTL_SRST    (1 << 0)
#define HDA_SD_CTL_RUN     (1 << 1)
#define HDA_SD_CTL_IOCE    (1 << 2)

/* Estrutura de entrada do BDL (Buffer Descriptor List) - 16 bytes */
typedef struct __attribute__((packed)) {
    uint64_t addr;    /* Endereço físico do buffer de áudio */
    uint32_t length;  /* Tamanho em bytes */
    uint32_t ioc;     /* Bit 0 = Interrupt on completion */
} hda_bdl_entry_t;

/* API pública do driver de áudio HDA */
int hda_init(void);
bool hda_is_available(void);
const char *hda_get_codec_name(void);
uint16_t hda_get_codec_vid(void);
uint16_t hda_get_codec_did(void);
uint16_t hda_get_statests(void);
pci_device_t *hda_get_pci_device(void);
uint32_t hda_get_stream_status(void);
uint32_t hda_get_stream_position(void);
uint8_t hda_get_output_routes(void);
void hda_play_pcm(const int16_t *samples, uint32_t num_samples);
void hda_beep(uint32_t freq_hz, uint32_t duration_ms);
void hda_startup_sound(void);
void hda_sleep_ms(uint32_t ms);
void hda_set_volume(uint8_t volume_percent);
uint8_t hda_get_volume(void);

#endif /* HDA_H */
