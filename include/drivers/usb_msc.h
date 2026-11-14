#ifndef USB_MSC_H
#define USB_MSC_H

#include <stdint.h>
#include <stdbool.h>

/* SCSI Commands */
#define SCSI_TEST_UNIT_READY 0x00
#define SCSI_REQUEST_SENSE   0x03
#define SCSI_INQUIRY         0x12
#define SCSI_READ_CAPACITY10 0x25
#define SCSI_READ10          0x28
#define SCSI_WRITE10         0x2A

/* USB Mass Storage CBW (Command Block Wrapper) */
typedef struct __attribute__((packed)) {
    uint32_t signature;          /* 0x43425355 ("USBC") */
    uint32_t tag;                /* Tag identificador */
    uint32_t data_transfer_len;  /* Comprimento em bytes da transferencia de dados */
    uint8_t  flags;              /* 0x80 = IN (Device to Host), 0x00 = OUT (Host to Device) */
    uint8_t  lun;                /* Logical Unit Number (0) */
    uint8_t  cdb_length;         /* Tamanho do comando SCSI (geralmente 6, 10 ou 16) */
    uint8_t  cdb[16];            /* Bloco de comando SCSI */
} usb_msc_cbw_t;

/* USB Mass Storage CSW (Command Status Wrapper) */
typedef struct __attribute__((packed)) {
    uint32_t signature;          /* 0x53425355 ("USBS") */
    uint32_t tag;                /* Tag coincidente com o CBW */
    uint32_t data_residue;       /* Diferenca entre requisitado e transferido */
    uint8_t  status;             /* 0x00 = Sucesso, 0x01 = Falha, 0x02 = Erro de fase */
} usb_msc_csw_t;

/* API Publica do Driver USB MSC */
void     usb_msc_init(void);
bool     usb_msc_is_available(void);
uint64_t usb_msc_get_sector_count(void);
uint32_t usb_msc_get_sector_size(void);
const char *usb_msc_get_model(void);

int usb_msc_read_sectors(uint32_t lba, uint32_t count, void *buffer);
int usb_msc_write_sectors(uint32_t lba, uint32_t count, const void *buffer);

#endif /* USB_MSC_H */
