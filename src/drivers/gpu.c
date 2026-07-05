#include "gpu.h"
#include "pci.h"
#include "serial.h"
#include "vmm.h"
#include "string.h"

/* ── GPU Information ── */
static uint16_t gpu_vendor = 0;
static uint16_t gpu_device = 0;
static uint32_t gpu_bar0 = 0;
static uint32_t gpu_bar2 = 0;
static const char *gpu_vendor_name = "Unknown";

/* ── MSR helpers ── */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t val) {
    uint32_t lo = (uint32_t)val;
    uint32_t hi = (uint32_t)(val >> 32);
    asm volatile("wrmsr" : : "c"(msr), "a"(lo), "d"(hi));
}

/* ── MTRR Constants ── */
#define IA32_MTRRCAP           0x00FE
#define IA32_MTRR_DEF_TYPE     0x02FF
#define IA32_MTRR_PHYSBASE0    0x0200
#define IA32_MTRR_PHYSMASK0    0x0201

#define MTRR_TYPE_UC           0x00
#define MTRR_TYPE_WC           0x01
#define MTRR_TYPE_WT           0x04
#define MTRR_TYPE_WP           0x05
#define MTRR_TYPE_WB           0x06

/* ── MTRR Write-Combining Setup ── */
void gpu_setup_wc_mtrr(uint64_t fb_phys, uint64_t fb_size) {
    /* Read MTRR capabilities */
    uint64_t cap = rdmsr(IA32_MTRRCAP);
    int num_var = cap & 0xFF;
    
    serial_print("GPU: MTRR capability: ");
    serial_print_hex((uint32_t)cap);
    serial_print(" var_count=");
    char ns[4]; itoa(num_var, ns, 10); serial_print(ns);
    
    /* Check if WC is supported (bit 10) */
    if (!(cap & (1 << 10))) {
        serial_print(" WC NOT SUPPORTED!\n");
        return;
    }
    serial_print(" WC supported\n");
    
    /* Round fb_size up to power of 2 for MTRR mask */
    uint64_t size_po2 = 1;
    while (size_po2 < fb_size) size_po2 <<= 1;
    
    /* Find a free variable MTRR slot (base == 0 and mask valid bit == 0) */
    int slot = -1;
    for (int i = 0; i < num_var && i < 8; i++) {
        uint64_t mask = rdmsr(IA32_MTRR_PHYSMASK0 + i * 2);
        if (!(mask & (1 << 11))) { /* Valid bit not set = free slot */
            slot = i;
            break;
        }
    }
    
    if (slot < 0) {
        serial_print("GPU: No free MTRR slot!\n");
        return;
    }
    
    serial_print("GPU: Using MTRR slot ");
    char ss[4]; itoa(slot, ss, 10); serial_print(ss);
    serial_print(" for WC at ");
    serial_print_hex((uint32_t)(fb_phys >> 32));
    serial_print_hex((uint32_t)fb_phys);
    serial_print(" size=");
    serial_print_hex((uint32_t)size_po2);
    serial_print("\n");
    
    /* Calculate mask: invert the size bits, set Valid bit */
    /* For 36-bit physical addressing (common): mask out bits 35:12 */
    uint64_t mask_val = (~(size_po2 - 1)) & 0x0000000FFFFFFFFF;
    mask_val |= (1 << 11); /* Valid */
    
    /* Write MTRR: base with WC type, then mask */
    wrmsr(IA32_MTRR_PHYSBASE0 + slot * 2, (fb_phys & 0xFFFFFFFFFFFFF000ULL) | MTRR_TYPE_WC);
    wrmsr(IA32_MTRR_PHYSMASK0 + slot * 2, mask_val);
    
    serial_print("GPU: MTRR Write-Combining ENABLED!\n");
}

/* ── GPU Detection ── */
void init_gpu(void) {
    serial_print("GPU: Scanning PCI for display controller...\n");
    
    pci_device_t *gpu = pci_get_gpu();
    if (!gpu) {
        serial_print("GPU: No display controller found in PCI\n");
        return;
    }
    
    gpu_vendor = gpu->vendor_id;
    gpu_device = gpu->device_id;
    
    /* Identify vendor */
    switch (gpu_vendor) {
        case 0x8086: gpu_vendor_name = "Intel"; break;
        case 0x1002: gpu_vendor_name = "AMD/ATI"; break;
        case 0x10DE: gpu_vendor_name = "NVIDIA"; break;
        case 0x1234: gpu_vendor_name = "Bochs/QEMU"; break;
        case 0x1AF4: gpu_vendor_name = "VirtIO"; break;
        case 0x15AD: gpu_vendor_name = "VMware"; break;
        default:     gpu_vendor_name = "Unknown"; break;
    }
    
    serial_print("GPU: Detected ");
    serial_print(gpu_vendor_name);
    serial_print(" GPU (vendor=0x");
    serial_print_hex(gpu_vendor);
    serial_print(" device=0x");
    serial_print_hex(gpu_device);
    serial_print(")\n");
    
    /* Read BARs */
    gpu_bar0 = pci_read_config(gpu->bus, gpu->device, gpu->function, 0x10) & ~0xF;
    gpu_bar2 = pci_read_config(gpu->bus, gpu->device, gpu->function, 0x18) & ~0xF;
    
    serial_print("GPU: BAR0=0x");
    serial_print_hex(gpu_bar0);
    serial_print(" BAR2=0x");
    serial_print_hex(gpu_bar2);
    serial_print("\n");
    
    /* Enable Bus Master + Memory Space on PCI */
    uint32_t pci_cmd = pci_read_config(gpu->bus, gpu->device, gpu->function, 0x04);
    pci_cmd |= (1 << 1) | (1 << 2); /* Memory Space + Bus Master */
    pci_write_config(gpu->bus, gpu->device, gpu->function, 0x04, pci_cmd);
    
    /* MTRR setup disabled temporarily due to #GP on some hardware */
    /*
    extern uint64_t vga_fb_addr;
    extern uint32_t vga_fb_pitch, vga_fb_height;
    if (vga_fb_addr != 0) {
        uint64_t fb_size = (uint64_t)vga_fb_pitch * vga_fb_height;
        gpu_setup_wc_mtrr(vga_fb_addr, fb_size);
    }
    */
    
    serial_print("GPU: Driver initialized.\n");
}

/* ── Accessors ── */
uint16_t gpu_get_vendor(void) { return gpu_vendor; }
const char *gpu_get_vendor_name(void) { return gpu_vendor_name; }
