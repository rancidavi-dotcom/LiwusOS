#include "ahci.h"
#include "kernel_layout.h"
#include "pci.h"
#include "kheap.h"
#include "serial.h"
#include "string.h"
#include "vga.h"
#include "vmm.h"

static hba_mem_t *abar;
static int ahci_initialized = 0;
static uint32_t ahci_rebased_mask = 0;

/* SATA links on real hardware (especially spinning disks) can take a moment
 * to come up. Poll DET/IPM instead of sampling once.
 *
 * NOTE: reading port->ssts is an MMIO access (a VM exit under KVM), so we
 * must poll it sparingly. Empty ports (DET==0) are never waited on. */
static int ahci_wait_port_ready(hba_port_t *port) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0x0F;
    uint8_t ipm = (ssts >> 8) & 0x0F;
    if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) return 0;
    if (det == 0) return -1;  /* no device attached: do not waste time */

    for (int i = 0; i < 200; i++) {
        for (volatile int d = 0; d < 50000; d++) asm volatile("pause");
        ssts = port->ssts;
        det = ssts & 0x0F;
        ipm = (ssts >> 8) & 0x0F;
        if (det == HBA_PORT_DET_PRESENT && ipm == HBA_PORT_IPM_ACTIVE) return 0;
    }
    return -1;
}

hba_mem_t *ahci_get_abar(void) {
    return abar;
}

/* Bounce buffer for DMA - in BSS */
static uint8_t ahci_bounce_buffer[4096] __attribute__((aligned(4096)));

/* Per-port DMA structures in BSS so multiple SATA ports on real motherboards
 * never overwrite or share each other's Command List or FIS receive area. */
static uint8_t ahci_clb_mem[32][1024] __attribute__((aligned(1024))); /* Command List per port */
static uint8_t ahci_fis_mem[32][256]  __attribute__((aligned(256)));  /* FIS receive per port */
static uint8_t ahci_ctba_mem[32][256] __attribute__((aligned(256)));  /* Command table (slot 0) per port */

#define AHCI_DEV_BUSY 0x80
#define AHCI_DEV_DRQ  0x08

static inline void ahci_poll_delay(void) {
    for (volatile int d = 0; d < 100; d++) asm volatile("pause");
}

static void port_stop_cmd(hba_port_t *port) {
    port->cmd &= ~HBA_PxCMD_ST;
    port->cmd &= ~HBA_PxCMD_FRE;
    uint64_t timeout = 0;
    while (1) {
        if (port->cmd & HBA_PxCMD_FR) { if (++timeout > 2000000) break; asm volatile("pause"); continue; }
        if (port->cmd & HBA_PxCMD_CR) { if (++timeout > 2000000) break; asm volatile("pause"); continue; }
        break;
    }
}

static void port_start_cmd(hba_port_t *port) {
    uint64_t timeout = 0;
    while ((port->cmd & HBA_PxCMD_CR) && timeout < 2000000) {
        timeout++;
        asm volatile("pause");
    }
    port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

static void port_rebase(hba_port_t *port, int portno) {
    if (portno < 0 || portno >= 32) return;
    port_stop_cmd(port);

    /* Never allow port interrupts: LiwusOS polls AHCI synchronously. */
    port->ie = 0;
    port->cmd |= HBA_PxCMD_SUD | HBA_PxCMD_POD;

    uint64_t clb_phys = virt_to_phys(ahci_clb_mem[portno]);
    uint64_t fis_phys = virt_to_phys(ahci_fis_mem[portno]);
    uint64_t ctba_phys = virt_to_phys(ahci_ctba_mem[portno]);

    serial_print("AHCI: CLB=");
    serial_print_hex(clb_phys);
    serial_print(" FB=");
    serial_print_hex(fis_phys);
    serial_print(" bounce=");
    serial_print_hex((uint64_t)ahci_bounce_buffer);
    serial_print("\n");

    memset(ahci_clb_mem[portno], 0, sizeof(ahci_clb_mem[portno]));
    memset(ahci_fis_mem[portno], 0, sizeof(ahci_fis_mem[portno]));
    memset(ahci_ctba_mem[portno], 0, sizeof(ahci_ctba_mem[portno]));

    port->clb = (uint32_t)clb_phys;
    port->clbu = (uint32_t)(clb_phys >> 32);

    port->fb = (uint32_t)fis_phys;
    port->fbu = (uint32_t)(fis_phys >> 32);

    hba_cmd_header_t *cmdheader = (hba_cmd_header_t *)ahci_clb_mem[portno];
    for (int i = 0; i < 32; i++) {
        cmdheader[i].prdtl = 1;
        cmdheader[i].ctba = (uint32_t)ctba_phys;
        cmdheader[i].ctbau = (uint32_t)(ctba_phys >> 32);
    }

    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;

    /* Clear stuck BSY/DRQ from BIOS if Command List Override is supported */
    if ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && (abar->cap & (1u << 24))) {
        port->cmd |= HBA_PxCMD_CLO;
        for (int t = 0; t < 500000 && (port->cmd & HBA_PxCMD_CLO); t++) {
            asm volatile("pause");
        }
    }

    asm volatile("mfence" ::: "memory");
    port_start_cmd(port);

    serial_print("AHCI: post-rebase CMD=");
    serial_print_hex(port->cmd);
    serial_print(" TFD=");
    serial_print_hex(port->tfd);
    serial_print(" SSTS=");
    serial_print_hex(port->ssts);
    serial_print("\n");
}

static void ahci_recover_port(hba_port_t *port) {
    port->ie = 0;
    port->cmd &= ~HBA_PxCMD_ST;
    for (int t = 0; t < 2000000 && (port->cmd & HBA_PxCMD_CR); t++) {
        asm volatile("pause");
    }
    if ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && (abar && (abar->cap & (1u << 24)))) {
        port->cmd |= HBA_PxCMD_CLO;
        for (int t = 0; t < 1000000 && (port->cmd & HBA_PxCMD_CLO); t++) {
            asm volatile("pause");
        }
    }
    /* If the SATA device itself is still stuck in BSY/DRQ, issue a brief
     * COMRESET so the device and HBA link state machines resynchronize cleanly. */
    if (port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) {
        port->cmd &= ~HBA_PxCMD_FRE;
        for (int t = 0; t < 1000000 && (port->cmd & HBA_PxCMD_FR); t++) {
            asm volatile("pause");
        }
        port->sctl = (port->sctl & ~0x0FU) | 0x01U; /* DET = 1 (COMRESET) */
        for (volatile int d = 0; d < 200000; d++) asm volatile("pause");
        port->sctl &= ~0x0FU; /* DET = 0 */
        ahci_wait_port_ready(port);
        if ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && (abar && (abar->cap & (1u << 24)))) {
            port->cmd |= HBA_PxCMD_CLO;
            for (int t = 0; t < 500000 && (port->cmd & HBA_PxCMD_CLO); t++) {
                asm volatile("pause");
            }
        }
    }
    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;
    if (abar) abar->is = 0xFFFFFFFF;
    if (!(port->cmd & HBA_PxCMD_FRE)) port->cmd |= HBA_PxCMD_FRE;
    port->cmd |= HBA_PxCMD_ST;
}

void ahci_init() {
    pci_device_t *ahci_pci = pci_get_ahci();
    if (!ahci_pci) {
        serial_print("AHCI: No controller found.\n");
        vga_puts("[disk] AHCI controller not found\n");
        return;
    }

    serial_print("AHCI: Controller found!\n");
    vga_puts("[disk] AHCI controller found, bringing up links...\n");

    /* Enable Memory Space (bit 1) + Bus Master (bit 2) + PCI Interrupt Disable (bit 10).
     * Disabling legacy PCI INTx is critical because AHCI is polled synchronously and
     * an unhandled level-triggered PCI IRQ on real hardware would cause an IRQ storm. */
    uint32_t cmd = pci_read_config(ahci_pci->bus, ahci_pci->device, ahci_pci->function, 0x04);
    cmd |= (1u << 1) | (1u << 2) | (1u << 10);
    pci_write_config(ahci_pci->bus, ahci_pci->device, ahci_pci->function, 0x04, cmd);

    /* Get ABAR (BAR5) */
    uint32_t bar5 = pci_read_config(ahci_pci->bus, ahci_pci->device, ahci_pci->function, 0x24);
    uint64_t phys_abar = bar5 & 0xFFFFFFF0ULL;
    if ((bar5 & 0x06U) == 0x04U) {
        uint32_t bar5_hi = pci_read_config(ahci_pci->bus, ahci_pci->device, ahci_pci->function, 0x28);
        phys_abar |= ((uint64_t)bar5_hi << 32);
    }

    serial_print("AHCI: ABAR=");
    serial_print_hex(phys_abar);
    serial_print("\n");

    /* Map enough 4KB-aligned pages for GHC + all 32 ports even when BAR5 is at offset 0x800 */
    uint64_t abar_page = phys_abar & ~0xFFFULL;
    for (int i = 0; i < 9; i++) {
        vmm_map_page_in(kernel_directory,
                        (void *)(abar_page + (uint64_t)i * 4096ULL),
                        phys_to_virt(abar_page + (uint64_t)i * 4096ULL),
                        PTE_P | PTE_W | PTE_PCD | PTE_NX);
    }

    abar = (hba_mem_t *)phys_to_virt(phys_abar);

    /* Perform BIOS/OS Handoff if supported by the controller (CAP2.BOH) */
    if (abar->cap2 & 1u) {
        abar->bohc |= (1u << 1); /* Set OOS (OS Owned Semaphore) */
        for (int t = 0; t < 200000; t++) {
            if (!(abar->bohc & 1u)) break;
            asm volatile("pause");
        }
    }

    /* Enable AHCI mode (GHC.AE = bit 31) and explicitly DISABLE global interrupts (GHC.IE = bit 1) */
    abar->ghc = (abar->ghc | (1u << 31)) & ~(1u << 1);
    abar->is = 0xFFFFFFFF;

    ahci_initialized = 1;

    serial_print("AHCI: GHC=");
    serial_print_hex(abar->ghc);
    serial_print(" PI=");
    serial_print_hex(abar->pi);
    serial_print(" CAP=");
    serial_print_hex(abar->cap);
    serial_print("\n");

    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;
        abar->ports[i].ie = 0;
        abar->ports[i].serr = 0xFFFFFFFF;
        abar->ports[i].is = 0xFFFFFFFF;

        char portstr[2] = {(char)('0' + (i % 10)), 0};

        if (ahci_wait_port_ready(&abar->ports[i]) != 0) {
            serial_print("AHCI: port ");
            serial_print(portstr);
            serial_print(" link not ready\n");
            continue;
        }

        uint32_t sig = abar->ports[i].sig;
        if (sig == 0xEB140101 || sig == 0xC33C0101 || sig == 0x96690101) {
            serial_print("AHCI: port ");
            serial_print(portstr);
            serial_print(" is non-ATA device, skipping\n");
            continue;
        }

        serial_print("AHCI: SATA drive found on port ");
        serial_print(portstr);
        serial_print(" ssts=");
        serial_print_hex(abar->ports[i].ssts);
        serial_print(" sig=");
        serial_print_hex(sig);
        serial_print("\n");

        port_rebase(&abar->ports[i], i);
        ahci_rebased_mask |= (1u << i);

        vga_puts("[disk] SATA drive online (port ");
        vga_puts(portstr);
        vga_puts(")\n");
    }

    if (!ahci_rebased_mask) {
        serial_print("AHCI: WARNING - no usable drive found (will retry)\n");
        vga_puts("[disk] No SATA drive ready yet, will retry\n");
    }
}

static int ahci_issue_cmd_once(uint8_t portno, uint64_t lba, uint32_t count, uint8_t *buf, int write) {
    if (!abar || portno >= 32) return 0;
    hba_port_t *port = &abar->ports[portno];

    if (!(ahci_rebased_mask & (1u << portno))) {
        port_rebase(port, portno);
        ahci_rebased_mask |= (1u << portno);
    }

    port->ie = 0;
    abar->ghc &= ~(1u << 1);
    if ((port->cmd & (HBA_PxCMD_ST | HBA_PxCMD_FRE)) != (HBA_PxCMD_ST | HBA_PxCMD_FRE)) {
        port_start_cmd(port);
    }

    /* If slot 0 is still marked active from a previous timeout, recover port */
    if ((port->sact | port->ci) & 1u) {
        ahci_recover_port(port);
    }

    int slot = 0;
    uint64_t ctba_phys = virt_to_phys(ahci_ctba_mem[portno]);
    uint64_t bounce_phys = virt_to_phys(ahci_bounce_buffer);

    hba_cmd_header_t *cmdheader = &((hba_cmd_header_t *)ahci_clb_mem[portno])[slot];
    memset(cmdheader, 0, sizeof(hba_cmd_header_t));
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t); /* 5 DWORDs */
    cmdheader->w = write ? 1 : 0;
    /* CRITICAL: cmdheader->c MUST be 0 for normal ATA DMA commands!
     * Setting C=1 ("Clear Busy upon R_OK") causes real Intel AHCI controllers
     * to clear PxCI[slot] and PxTFD.BSY as soon as the H2D command FIS gets
     * R_OK over the wire, BEFORE the drive transfers DMA data or sends its
     * closing D2H Register FIS! */
    cmdheader->c = 0;
    cmdheader->prdtl = 1;
    cmdheader->prdbc = 0;
    cmdheader->ctba = (uint32_t)ctba_phys;
    cmdheader->ctbau = (uint32_t)(ctba_phys >> 32);

    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t *)ahci_ctba_mem[portno];
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));

    if (count == 0 || count * 512 > sizeof(ahci_bounce_buffer)) return 0;
    if (write) {
        memcpy(ahci_bounce_buffer, buf, count * 512);
    } else {
        memset(ahci_bounce_buffer, 0, count * 512);
    }

    cmdtbl->prdt_entry[0].dba = (uint32_t)bounce_phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(bounce_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = (count * 512) - 1;
    cmdtbl->prdt_entry[0].i = 0; /* Poll mode: do not assert interrupt */

    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis);
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;                         /* Command register update */
    cmdfis->command = write ? 0x35 : 0x25; /* READ/WRITE DMA EXT (LBA48) */
    cmdfis->device = 1 << 6;               /* LBA mode */

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
    abar->is = 0xFFFFFFFF;

    /* Wait for port not to be busy */
    uint64_t spin = 0;
    while ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && spin < 1000000) {
        spin++;
        ahci_poll_delay();
    }
    if (spin >= 1000000) {
        serial_print("AHCI: port busy before cmd, recovering TFD=");
        serial_print_hex(port->tfd);
        serial_print("\n");
        ahci_recover_port(port);
        if (port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) return 0;
    }

    asm volatile("mfence" ::: "memory");
    port->ci = 1u << slot;

    /* Wait for completion (~3 seconds max for physical HDD seeks) */
    uint64_t timeout2 = 0;
    while (1) {
        if ((port->ci & (1u << slot)) == 0)
            break;
        if (port->is & ((1u << 30) | (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26))) {
            serial_print("AHCI: Error! IS=");
            serial_print_hex(port->is);
            serial_print(" SERR=");
            serial_print_hex(port->serr);
            serial_print(" TFD=");
            serial_print_hex(port->tfd);
            serial_print("\n");
            ahci_recover_port(port);
            return 0;
        }
        if (++timeout2 > 3000000) {
            serial_print("AHCI: Command timeout! CI=");
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
            ahci_recover_port(port);
            return 0;
        }
        ahci_poll_delay();
    }

    /* Ensure the drive also cleared BSY/DRQ before reading bounce buffer */
    uint64_t post_spin = 0;
    while ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && post_spin < 500000) {
        post_spin++;
        ahci_poll_delay();
    }

    if (port->tfd & 0x01) {
        serial_print("AHCI: TFD error after completion! TFD=");
        serial_print_hex(port->tfd);
        serial_print("\n");
        ahci_recover_port(port);
        return 0;
    }

    asm volatile("mfence" ::: "memory");
    if (!write) memcpy(buf, ahci_bounce_buffer, count * 512);
    return 1;
}

static int ahci_issue_cmd(uint8_t portno, uint64_t lba, uint32_t count, uint8_t *buf, int write) {
    for (int attempt = 0; attempt < 3; attempt++) {
        if (ahci_issue_cmd_once(portno, lba, count, buf, write)) return 1;
    }
    return 0;
}

int ahci_read_sector(uint8_t portno, uint64_t lba, uint32_t count, uint8_t *buf) {
    if (!ahci_initialized) return 0;
    return ahci_issue_cmd(portno, lba, count, buf, 0);
}

int ahci_write_sector(uint8_t portno, uint64_t lba, uint32_t count, uint8_t *buf) {
    if (!ahci_initialized) return 0;
    return ahci_issue_cmd(portno, lba, count, buf, 1);
}

/* Issue ATA FLUSH CACHE EXT through a no-data AHCI command. */
int ahci_flush_cache(uint8_t portno) {
    if (!ahci_initialized || !abar || portno >= 32) return 0;
    hba_port_t *port = &abar->ports[portno];
    if (!(ahci_rebased_mask & (1u << portno))) {
        port_rebase(port, portno);
        ahci_rebased_mask |= (1u << portno);
    }

    port->ie = 0;
    abar->ghc &= ~(1u << 1);
    if ((port->cmd & (HBA_PxCMD_ST | HBA_PxCMD_FRE)) != (HBA_PxCMD_ST | HBA_PxCMD_FRE)) {
        port_start_cmd(port);
    }
    if ((port->sact | port->ci) & 1u) {
        ahci_recover_port(port);
    }

    int slot = 0;
    uint64_t ctba_phys = virt_to_phys(ahci_ctba_mem[portno]);

    hba_cmd_header_t *header = &((hba_cmd_header_t *)ahci_clb_mem[portno])[slot];
    memset(header, 0, sizeof(hba_cmd_header_t));
    header->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    header->w = 0;
    header->c = 0; /* MUST be 0 so HBA waits for the D2H FIS after cache flush completes */
    header->prdtl = 0;
    header->ctba = (uint32_t)ctba_phys;
    header->ctbau = (uint32_t)(ctba_phys >> 32);

    hba_cmd_tbl_t *table = (hba_cmd_tbl_t *)ahci_ctba_mem[portno];
    memset(table, 0, sizeof(hba_cmd_tbl_t));
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)&table->cfis;
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = 0xEA; /* FLUSH CACHE EXT */
    fis->device = 1 << 6;

    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;
    abar->is = 0xFFFFFFFF;

    uint64_t spin = 0;
    while ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && spin < 1000000) {
        spin++;
        ahci_poll_delay();
    }
    if (spin >= 1000000) {
        ahci_recover_port(port);
        return 1;
    }

    asm volatile("mfence" ::: "memory");
    port->ci = 1u << slot;
    uint64_t timeout = 0;
    while (port->ci & (1u << slot)) {
        if (port->is & ((1u << 30) | (1u << 29) | (1u << 28) |
                        (1u << 27) | (1u << 26))) {
            ahci_recover_port(port);
            return 1;
        }
        if (++timeout > 3000000) {
            ahci_recover_port(port);
            return 1;
        }
        ahci_poll_delay();
    }
    if (port->tfd & 0x01) {
        ahci_recover_port(port);
    }
    return 1;
}

uint64_t ahci_identify(uint8_t portno) {
    if (!ahci_initialized || !abar || portno >= 32) return 0;
    hba_port_t *port = &abar->ports[portno];

    if (!(ahci_rebased_mask & (1u << portno))) {
        port_rebase(port, portno);
        ahci_rebased_mask |= (1u << portno);
    }

    port->ie = 0;
    abar->ghc &= ~(1u << 1);
    if ((port->cmd & (HBA_PxCMD_ST | HBA_PxCMD_FRE)) != (HBA_PxCMD_ST | HBA_PxCMD_FRE)) {
        port_start_cmd(port);
    }
    if ((port->sact | port->ci) & 1u) {
        ahci_recover_port(port);
    }

    int slot = 0;
    uint64_t ctba_phys = virt_to_phys(ahci_ctba_mem[portno]);
    uint64_t bounce_phys = virt_to_phys(ahci_bounce_buffer);

    hba_cmd_header_t *cmdheader = &((hba_cmd_header_t *)ahci_clb_mem[portno])[slot];
    memset(cmdheader, 0, sizeof(hba_cmd_header_t));
    cmdheader->cfl = sizeof(fis_reg_h2d_t) / sizeof(uint32_t);
    cmdheader->w = 0;
    cmdheader->c = 0; /* MUST be 0 so HBA waits for PIO Setup + Data FIS from drive */
    cmdheader->prdtl = 1;
    cmdheader->prdbc = 0;
    cmdheader->ctba = (uint32_t)ctba_phys;
    cmdheader->ctbau = (uint32_t)(ctba_phys >> 32);

    hba_cmd_tbl_t *cmdtbl = (hba_cmd_tbl_t *)ahci_ctba_mem[portno];
    memset(cmdtbl, 0, sizeof(hba_cmd_tbl_t));
    memset(ahci_bounce_buffer, 0, 512);

    cmdtbl->prdt_entry[0].dba = (uint32_t)bounce_phys;
    cmdtbl->prdt_entry[0].dbau = (uint32_t)(bounce_phys >> 32);
    cmdtbl->prdt_entry[0].dbc = 511; /* 512 bytes */
    cmdtbl->prdt_entry[0].i = 0;   /* Poll mode: no interrupt */

    fis_reg_h2d_t *cmdfis = (fis_reg_h2d_t *)(&cmdtbl->cfis);
    memset(cmdfis, 0, sizeof(fis_reg_h2d_t));
    cmdfis->fis_type = FIS_TYPE_REG_H2D;
    cmdfis->c = 1;
    cmdfis->command = 0xEC; /* IDENTIFY DEVICE */
    cmdfis->device = 0;

    port->serr = 0xFFFFFFFF;
    port->is = 0xFFFFFFFF;
    abar->is = 0xFFFFFFFF;

    uint64_t spin = 0;
    while ((port->tfd & (AHCI_DEV_BUSY | AHCI_DEV_DRQ)) && spin < 500000) {
        spin++;
        ahci_poll_delay();
    }
    if (spin >= 500000) {
        ahci_recover_port(port);
        return 0;
    }

    asm volatile("mfence" ::: "memory");
    port->ci = 1u << slot;

    uint64_t timeout2 = 0;
    while (1) {
        if ((port->ci & (1u << slot)) == 0) break;
        if (port->is & ((1u << 30) | (1u << 29) | (1u << 28) | (1u << 27) | (1u << 26))) {
            ahci_recover_port(port);
            return 0;
        }
        if (++timeout2 > 1000000) {
            ahci_recover_port(port);
            return 0;
        }
        ahci_poll_delay();
    }
    if (port->tfd & 0x01) {
        ahci_recover_port(port);
        return 0;
    }
    asm volatile("mfence" ::: "memory");

    uint16_t *buf = (uint16_t *)ahci_bounce_buffer;
    uint64_t sectors = 0;
    if (buf[83] & (1 << 10)) { /* LBA48 supported */
        sectors = *(uint64_t *)&buf[100];
    } else {
        sectors = *(uint32_t *)&buf[60];
    }
    if (sectors == 0 && (buf[0] != 0 && buf[0] != 0xFFFF && !(buf[0] & 0x8000))) {
        sectors = *(uint32_t *)&buf[60];
    }
    return sectors;
}

int ahci_find_first(uint8_t *port_out) {
    if (!abar) return -1;
    uint32_t pi = abar->pi;
    for (int i = 0; i < 32; i++) {
        if (!(pi & (1u << i))) continue;

        if (ahci_wait_port_ready(&abar->ports[i]) != 0) continue;

        uint32_t sig = abar->ports[i].sig;
        if (sig == 0xEB140101 || sig == 0xC33C0101 || sig == 0x96690101) continue;

        if (!(ahci_rebased_mask & (1u << i))) {
            serial_print("AHCI: drive became ready on port ");
            char portstr[2] = {(char)('0' + (i % 10)), 0};
            serial_print(portstr);
            serial_print("\n");
            port_rebase(&abar->ports[i], i);
            ahci_rebased_mask |= (1u << i);
        }

        sig = abar->ports[i].sig;
        if (sig == 0xEB140101 || sig == 0xC33C0101 || sig == 0x96690101) continue;

        ahci_initialized = 1;
        /* Verify port actually responds as a SATA disk via IDENTIFY or ATA sig */
        uint64_t secs = ahci_identify((uint8_t)i);
        if (secs == 0 && sig != 0x00000101) continue;

        *port_out = (uint8_t)i;
        return 0;
    }
    return -1;
}
