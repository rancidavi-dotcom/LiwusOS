#include "ahci.h"
#include "pci.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"

static hba_mem_t *abar;
static int ahci_initialized = 0;

hba_mem_t *ahci_get_abar(void) {
    return abar;
}

/* Bounce buffer for DMA - must be in BSS (identity-mapped low memory) */
static uint8_t ahci_bounce_buffer[4096] __attribute__((aligned(4096)));

/* Pre-allocated DMA structures in BSS - guaranteed identity-mapped */
static uint8_t ahci_clb_mem[1024]  __attribute__((aligned(1024)));  /* Command List */
static uint8_t ahci_fis_mem[256]   __attribute__((aligned(256)));   /* FIS receive */
static uint8_t ahci_ctba_mem[8192] __attribute__((aligned(256)));   /* Command tables: 256 bytes * 32 slots */


#define AHCI_DEV_BUSY 0x80
#define AHCI_DEV_DRQ  0x08

static void port_stop_cmd(hba_port_t *port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;
    uint64_t timeout = 0;
    while(1) {
        if (port->cmd & HBA_PxCMD_FR) { if (++timeout > 100000000) break; asm volatile("pause"); continue; }
        if (port->cmd & HBA_PxCMD_CR) { if (++timeout > 100000000) break; asm volatile("pause"); continue; }
        break;
    }
}

static void port_start_cmd(hba_port_t *port) {
    uint64_t timeout = 0;
    while ((port->cmd & HBA_PxCMD_CR) && timeout < 100000000) {
        timeout++;
        asm volatile("pause");
    }
    
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST; 
}

static void port_rebase(hba_port_t *port, int portno) {
    (void)portno;
    port_stop_cmd(port);

    uint64_t clb_phys = (uint64_t)ahci_clb_mem;
    uint64_t fis_phys = (uint64_t)ahci_fis_mem;
    
    serial_print("AHCI: CLB=");
    serial_print_hex(clb_phys);
    serial_print(" FB=");
    serial_print_hex(fis_phys);
    serial_print(" bounce=");
    serial_print_hex((uint64_t)ahci_bounce_buffer);
    serial_print("\n");

    memset(ahci_clb_mem, 0, sizeof(ahci_clb_mem));
    memset(ahci_fis_mem, 0, sizeof(ahci_fis_mem));
    memset(ahci_ctba_mem, 0, sizeof(ahci_ctba_mem));

    port->clb = (uint32_t)clb_phys;
    port->clbu = 0;

    port->fb = (uint32_t)fis_phys;
    port->fbu = 0;

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)ahci_clb_mem;
    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = 1;
        uint64_t ctba_addr = (uint64_t)ahci_ctba_mem + i * 256;
        cmdheader[i].ctba = (uint32_t)ctba_addr;
        cmdheader[i].ctbau = 0;
    }

    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;

    port_start_cmd(port);
    
    serial_print("AHCI: post-rebase CMD=");
    serial_print_hex(port->cmd);
    serial_print(" TFD=");
    serial_print_hex(port->tfd);
    serial_print(" SSTS=");
    serial_print_hex(port->ssts);
    serial_print("\n");
}

void ahci_init() {
    pci_device_t *ahci_pci = pci_get_ahci();
    if (!ahci_pci) {
        serial_print("AHCI: No controller found.\n");
        return;
    }

    serial_print("AHCI: Controller found!\n");
    
    // Enable bus mastering + memory space
    uint32_t cmd = pci_read_config(ahci_pci->bus, ahci_pci->device, ahci_pci->function, 0x04);
    cmd |= (1 << 1) | (1 << 2); // Memory Space + Bus Master
    pci_write_config(ahci_pci->bus, ahci_pci->device, ahci_pci->function, 0x04, cmd);

    // Get ABAR (BAR5)
    uint32_t bar5 = pci_read_config(ahci_pci->bus, ahci_pci->device, ahci_pci->function, 0x24);
    uint64_t phys_abar = bar5 & 0xFFFFFFF0;

    serial_print("AHCI: ABAR=");
    serial_print_hex(phys_abar);
    serial_print("\n");

    extern void vmm_map_page(void *phys, void *virt, uint64_t flags);
    // Map enough pages for GHC + all 32 ports
    for (int i = 0; i < 8; i++) {
        vmm_map_page((void*)(phys_abar + i * 4096), (void*)(phys_abar + i * 4096), 3);
    }

    abar = (hba_mem_t *)phys_abar;
    
    // Enable AHCI mode (GHC.AE)
    abar->ghc |= (1 << 31);
    
    serial_print("AHCI: GHC=");
    serial_print_hex(abar->ghc);
    serial_print(" PI=");
    serial_print_hex(abar->pi);
    serial_print(" CAP=");
    serial_print_hex(abar->cap);
    serial_print("\n");
    
    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1<<i)) {
            abar->ports[i].serr = 0xFFFFFFFF;
            
            uint32_t ssts = abar->ports[i].ssts;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;
            
            serial_print("AHCI: port ");
            char portstr[2] = {'0' + i, 0};
            serial_print(portstr);
            serial_print(" ssts=");
            serial_print_hex(ssts);
            serial_print(" sig=");
            serial_print_hex(abar->ports[i].sig);
            serial_print(" tfd=");
            serial_print_hex(abar->ports[i].tfd);
            serial_print("\n");
            
            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                uint32_t sig = abar->ports[i].sig;
                if (sig == 0x00000101 || sig == 0xFFFFFFFF || sig == 0x00000000) {
                    serial_print("AHCI: SATA drive found on port ");
                    serial_print(portstr);
                    serial_print("\n");
                    port_rebase(&abar->ports[i], i);
                    ahci_initialized = 1;
                }
            }
        }
    }
    
    if (!ahci_initialized) {
        serial_print("AHCI: WARNING - no usable drive found!\n");
    }
}

static int find_cmdslot(hba_port_t *port) {
    uint32_t slots = (port->sact | port->ci);
    for (int i = 0; i < 32; i++) {
        if ((slots & (1<<i)) == 0)
            return i;
    }
    return -1;
}

static int ahci_issue_cmd(hba_port_t *port, uint64_t lba, uint32_t count, uint8_t *buf, int write) {
    port->is = (uint32_t)-1;
    port->is = (uint32_t)-1;
    int slot = find_cmdslot(port);
    if (slot == -1) {
        serial_print("AHCI: no free command slot!\n");
        return 0;
    }

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)ahci_clb_mem;
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t); // 5 DWORDs
    cmdheader->w = write ? 1 : 0;
    cmdheader->c = 1; // Clear busy upon R_OK
    cmdheader->p = 0;
    cmdheader->a = 0;
    cmdheader->r = 0;
    cmdheader->b = 0;
    cmdheader->prdtl = 1;
    cmdheader->prdbc = 0;
    
    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)(uint64_t)cmdheader->ctba;
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));
    
    if (count * 512 > sizeof(ahci_bounce_buffer)) return 0;
    if (write) memcpy(ahci_bounce_buffer, buf, count * 512);

    cmdtbl->prdt_entry[0].dba = (uint32_t)(uint64_t)ahci_bounce_buffer;
    cmdtbl->prdt_entry[0].dbau = 0;
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmdtbl->prdt_entry[0].i = 1;

    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = write ? 0x35 : 0x25; // READ/WRITE DMA EXT (LBA48)
    cmdfis->device = 1 << 6; // LBA mode
    
    cmdfis->lba0 = (uint8_t)lba;
    cmdfis->lba1 = (uint8_t)(lba >> 8);
    cmdfis->lba2 = (uint8_t)(lba >> 16);
    cmdfis->lba3 = (uint8_t)(lba >> 24);
    cmdfis->lba4 = (uint8_t)(lba >> 32);
    cmdfis->lba5 = (uint8_t)(lba >> 40);
    
    cmdfis->countl = count & 0xFF;
    cmdfis->counth = (count >> 8) & 0xFF;

    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;

    // Wait for port to not be busy
    uint64_t spin = 0;
    while ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && spin < 100000000) {
        spin++;
        asm volatile("pause");
    }
    if (spin >= 100000000) {
        serial_print("AHCI: port busy timeout! TFD=");
        serial_print_hex(port->tfd);
        serial_print("\n");
        return 0;
    }

    port->ci = 1<<slot;

    // Wait for completion
    uint64_t timeout2 = 0;
    while (1) {
        if ((port->ci & (1<<slot)) == 0) 
            break;
        if (port->is & ((1<<30) | (1<<29) | (1<<28) | (1<<27) | (1<<26))) {
            serial_print("AHCI: Error! IS=");
            serial_print_hex(port->is);
            serial_print(" SERR=");
            serial_print_hex(port->serr);
            serial_print(" TFD=");
            serial_print_hex(port->tfd);
            serial_print("\n");
            port->cmd &= ~HBA_PxCMD_ST;
            int t = 0; while((port->cmd & HBA_PxCMD_CR) && t < 1000000) t++;
            port->serr = 0xFFFFFFFF;
            port->is = 0xFFFFFFFF;
            port->cmd |= HBA_PxCMD_ST;
            return 0;
        }
        if (++timeout2 > 100000000) {
            serial_print("AHCI: Command hung! CI=");
            serial_print_hex(port->ci);
            serial_print(" IS=");
            serial_print_hex(port->is);
            serial_print(" CMD=");
            serial_print_hex(port->cmd);
            serial_print(" TFD=");
            serial_print_hex(port->tfd);
            serial_print(" SERR=");
            serial_print_hex(port->serr);
            serial_print("\n");
            port->cmd &= ~HBA_PxCMD_ST;
            int t = 0; while((port->cmd & HBA_PxCMD_CR) && t < 1000000) t++;
            port->serr = 0xFFFFFFFF;
            port->is = 0xFFFFFFFF;
            port->cmd |= HBA_PxCMD_ST;
            return 0;
        }
    }
    
    if (port->tfd & 0x01) {
        serial_print("AHCI: TFD error after completion! TFD=");
        serial_print_hex(port->tfd);
        serial_print("\n");
        return 0;
    }
    
    if (!write) memcpy(buf, ahci_bounce_buffer, count * 512);

    return 1;
}

int ahci_read_sector(uint8_t portno, uint64_t lba, uint32_t count, uint8_t *buf) {
    if (!ahci_initialized) return 0;
    return ahci_issue_cmd(&abar->ports[portno], lba, count, buf, 0);
}

int ahci_write_sector(uint8_t portno, uint64_t lba, uint32_t count, uint8_t *buf) {
    if (!ahci_initialized) return 0;
    return ahci_issue_cmd(&abar->ports[portno], lba, count, buf, 1);
}

int ahci_find_first(uint8_t *port_out) {
    if (!ahci_initialized) return -1;
    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (pi & (1<<i)) {
            uint32_t ssts = abar->ports[i].ssts;
            uint8_t ipm = (ssts >> 8) & 0x0F;
            uint8_t det = ssts & 0x0F;
            if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) {
                *port_out = i;
                return 0;
            }
        }
    }
    return -1;
}

uint64_t ahci_identify(uint8_t portno) {
    if (!ahci_initialized || portno >= 32) return 0;
    hba_port_t *port = &abar->ports[portno];
    
    int slot = find_cmdslot(port);
    if (slot == -1) return 0;

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t*)ahci_clb_mem;
    cmdheader += slot;
    cmdheader->cfl = sizeof(fis_reg_h2d_t)/sizeof(uint32_t);
    cmdheader->w = 0;
    cmdheader->c = 1;
    cmdheader->p = 0;
    cmdheader->a = 0;
    cmdheader->r = 0;
    cmdheader->b = 0;
    cmdheader->prdtl = 1;
    cmdheader->prdbc = 0;
    
    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t*)(uint64_t)cmdheader->ctba;
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));
    
    cmdtbl->prdt_entry[0].dba = (uint32_t)(uint64_t)ahci_bounce_buffer;
    cmdtbl->prdt_entry[0].dbau = 0;
    cmdtbl->prdt_entry[0].dbc = 511; // 512 bytes
    cmdtbl->prdt_entry[0].i = 1;

    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t*)(&cmdtbl->cfis);
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = 0xEC; // IDENTIFY DEVICE
    
    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;

    uint64_t spin = 0;
    while ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && spin < 1000000) {
        spin++; asm volatile("pause");
    }
    if (spin >= 1000000) return 0;

    port->ci = 1<<slot;

    uint64_t timeout2 = 0;
    while (1) {
        if ((port->ci & (1<<slot)) == 0) break;
        if (port->is & ((1<<30) | (1<<29) | (1<<28) | (1<<27) | (1<<26))) {
            port->cmd &= ~HBA_PxCMD_ST;
            int t = 0; while((port->cmd & HBA_PxCMD_CR) && t < 100000) { t++; asm volatile("pause"); }
            port->serr = 0xFFFFFFFF; port->is = 0xFFFFFFFF;
            port->cmd |= HBA_PxCMD_ST;
            return 0;
        }
        if (++timeout2 > 100000000) {
            port->cmd &= ~HBA_PxCMD_ST;
            int t = 0; while((port->cmd & HBA_PxCMD_CR) && t < 100000) { t++; asm volatile("pause"); }
            port->serr = 0xFFFFFFFF; port->is = 0xFFFFFFFF;
            port->cmd |= HBA_PxCMD_ST;
            return 0;
        }
        asm volatile("pause");
    }
    if (port->tfd & 0x01) return 0;
    
    uint16_t *buf = (uint16_t*)ahci_bounce_buffer;
    uint64_t sectors = 0;
    if (buf[83] & (1<<10)) { // LBA48 supported
        sectors = *(uint64_t*)&buf[100];
    } else {
        sectors = *(uint32_t*)&buf[60];
    }
    return sectors;
}
