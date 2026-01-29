#include "acpi.h"
#include "vmm.h"
#include "serial.h"
#include "string.h"
#include "io.h"

/* ---- Offsets da FADT ---- */
#define FADT_DSDT          0x28
#define FADT_SCI_INT       0x2E
#define FADT_SMI_CMD       0x30
#define FADT_ACPI_ENABLE   0x34
#define FADT_PM1A_EVT_BLK  0x38
#define FADT_PM1A_CNT_BLK  0x40
#define FADT_PM1B_CNT_BLK  0x44
#define FADT_RESET_REG     0x74
#define FADT_RESET_VALUE   0x80

#define SLP_EN             (1u << 13)

static void parse_s5slp(acpi_sdt_header_t *fadt, uint8_t *typa, uint8_t *typb);

static uint64_t rsdp_phys = 0;
static rsdp_t *rsdp = NULL;
static acpi_sdt_header_t *root_sdt = NULL;
static bool root_is_xsdt = false;
static bool acpi_ok = false;

/* ---- Acesso a inteiros não alinhados ---- */
static uint32_t rd32(const void *p) {
  uint32_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

static uint64_t rd64(const void *p) {
  uint64_t v;
  memcpy(&v, p, sizeof(v));
  return v;
}

/* Mapeia (identidade) o intervalo físico para poder lê-lo. */
static void acpi_map(uint64_t phys, uint32_t len) {
  uint64_t start = phys & ~0xFFFULL;
  uint64_t end = (phys + len + 0xFFF) & ~0xFFFULL;
  for (uint64_t a = start; a < end; a += 0x1000)
    vmm_map_page((void *)a, (void *)a, PTE_P | PTE_W | PTE_PCD);
}

static bool checksum_ok(acpi_sdt_header_t *h) {
  uint8_t sum = 0;
  uint8_t *b = (uint8_t *)h;
  for (uint32_t i = 0; i < h->length; i++)
    sum += b[i];
  return sum == 0;
}

/* Varredura legada do RSDP (EBDA + 0xE0000-0xFFFFF). */
static rsdp_t *find_rsdp_legacy(void) {
  uint16_t ebda_seg = 0;
  __asm__ volatile("movw (%%rbx), %0"
                   : "=r"(ebda_seg)
                   : "b"((uint64_t)0x40E)
                   : "memory");
  uint64_t ebda = ((uint64_t)ebda_seg) << 4;
  if (ebda > 0x400 && ebda < 0xA0000) {
    for (uint64_t a = ebda; a < ebda + 1024; a += 16) {
      if (memcmp((void *)a, "RSD PTR ", 8) == 0) {
        uint8_t sum = 0;
        for (int i = 0; i < 20; i++)
          sum += ((uint8_t *)a)[i];
        if (sum == 0)
          return (rsdp_t *)a;
      }
    }
  }
  for (uint64_t a = 0xE0000; a < 0x100000; a += 16) {
    if (memcmp((void *)a, "RSD PTR ", 8) == 0) {
      uint8_t sum = 0;
      for (int i = 0; i < 20; i++)
        sum += ((uint8_t *)a)[i];
      if (sum == 0)
        return (rsdp_t *)a;
    }
  }
  return NULL;
}

void acpi_set_rsdp(uint64_t phys) {
  if (phys) {
    rsdp_phys = phys;
    serial_print("ACPI: RSDP fornecido pelo Multiboot2 em ");
    serial_print_hex(phys);
    serial_print("\n");
  }
}

int acpi_init(void) {
  acpi_ok = false;
  root_sdt = NULL;
  root_is_xsdt = false;

  if (rsdp_phys) {
    rsdp = (rsdp_t *)rsdp_phys;
  } else {
    rsdp = find_rsdp_legacy();
  }

  if (!rsdp) {
    serial_print("ACPI: RSDP nao encontrado.\n");
    return -1;
  }

  if (memcmp(rsdp->signature, "RSD PTR ", 8) != 0) {
    serial_print("ACPI: assinatura RSDP invalida.\n");
    return -1;
  }

  bool is_v2 = false;
  if (rsdp->revision >= 2) {
    acpi_map((uint64_t)rsdp, sizeof(rsdp2_t));
    rsdp2_t *r2 = (rsdp2_t *)rsdp;
    if (r2->length >= 36) {
      uint8_t sum = 0;
      uint8_t *b = (uint8_t *)r2;
      for (uint32_t i = 0; i < r2->length; i++)
        sum += b[i];
      is_v2 = (sum == 0) && (r2->xsdt_address != 0);
    }
  }

  acpi_sdt_header_t *root;
  if (is_v2) {
    rsdp2_t *r2 = (rsdp2_t *)rsdp;
    root = (acpi_sdt_header_t *)(uint64_t)r2->xsdt_address;
    root_is_xsdt = true;
    serial_print("ACPI: usando XSDT\n");
  } else {
    root = (acpi_sdt_header_t *)(uint64_t)rsdp->rsdt_address;
    root_is_xsdt = false;
    serial_print("ACPI: usando RSDT\n");
  }

  if (!root) {
    serial_print("ACPI: tabela raiz ausente.\n");
    return -1;
  }

  acpi_map((uint64_t)root, sizeof(acpi_sdt_header_t));
  if (!checksum_ok(root)) {
    serial_print("ACPI: checksum da tabela raiz invalido.\n");
    root_sdt = NULL;
    return -1;
  }

  acpi_map((uint64_t)root, root->length);
  root_sdt = root;
  acpi_ok = true;
  serial_print("ACPI: tabela raiz OK, ");
  serial_print_hex((root->length - sizeof(acpi_sdt_header_t)) /
                   (root_is_xsdt ? 8 : 4));
  serial_print(" tabelas\n");

  /* Diagnóstico de power management (Fase 3). */
  acpi_sdt_header_t *fadt = acpi_find_table("FACP");
  if (fadt) {
    serial_print("ACPI: FADT PM1a_CNT=");
    serial_print_hex(rd32((uint8_t *)fadt + FADT_PM1A_CNT_BLK));
    serial_print(" PM1b_CNT=");
    serial_print_hex(rd32((uint8_t *)fadt + FADT_PM1B_CNT_BLK));
    if (fadt->length >= 0x81) {
      serial_print(" RESET_REG=");
      serial_print_hex(rd64((uint8_t *)fadt + FADT_RESET_REG + 4));
    }
    serial_print("\n");
    uint8_t sa = 0, sb = 0;
    parse_s5slp(fadt, &sa, &sb);
  }
  return 0;
}

bool acpi_available(void) { return acpi_ok; }

acpi_sdt_header_t *acpi_find_table(const char *signature) {
  if (!acpi_ok || !root_sdt)
    return NULL;

  uint32_t ent_size = root_is_xsdt ? 8 : 4;
  uint32_t count =
      (root_sdt->length - sizeof(acpi_sdt_header_t)) / ent_size;
  uint8_t *base = (uint8_t *)root_sdt + sizeof(acpi_sdt_header_t);

  for (uint32_t i = 0; i < count; i++) {
    uint64_t phys =
        root_is_xsdt ? rd64(base + i * 8) : (uint64_t)rd32(base + i * 4);
    if (!phys)
      continue;
    acpi_map(phys, sizeof(acpi_sdt_header_t));
    acpi_sdt_header_t *h = (acpi_sdt_header_t *)(uintptr_t)phys;
    if (memcmp(h->signature, signature, 4) != 0)
      continue;
    acpi_map(phys, h->length);
    if (!checksum_ok(h)) {
      serial_print("ACPI: checksum invalido em ");
      serial_print(signature);
      serial_print("\n");
      continue;
    }
    return h;
  }
  return NULL;
}

/* Procura \_S5 no DSDT (heurística AML) para extrair SLP_TYPa/b. */
static void parse_s5slp(acpi_sdt_header_t *fadt, uint8_t *typa, uint8_t *typb) {
  *typa = 0;
  *typb = 0;

  uint32_t dsdt_phys = rd32((uint8_t *)fadt + FADT_DSDT);
  if (!dsdt_phys)
    return;
  acpi_map(dsdt_phys, sizeof(acpi_sdt_header_t));
  acpi_sdt_header_t *dsdt = (acpi_sdt_header_t *)(uintptr_t)dsdt_phys;
  if (memcmp(dsdt->signature, "DSDT", 4) != 0)
    return;
  acpi_map(dsdt_phys, dsdt->length);

  uint8_t *p = (uint8_t *)dsdt;
  uint32_t len = dsdt->length;

  for (uint32_t i = 4; i + 4 < len; i++) {
    if (p[i] != '_' || p[i + 1] != 'S' || p[i + 2] != '5' || p[i + 3] != '_')
      continue;

    uint32_t j = i + 4;
    uint32_t lim = j + 9 < len ? j + 9 : len;
    while (j < lim && p[j] != 0x12) /* PackageOp */
      j++;
    if (j >= len || p[j] != 0x12)
      continue;
    j++;

    uint8_t lead = p[j];
    if (lead & 0xC0) {
      uint32_t n = (lead >> 6) & 3;
      j += 1 + n;
    } else {
      j += 1;
    }
    if (j >= len)
      continue;
    uint8_t numel = p[j++];
    if (numel < 2)
      continue;

    uint8_t vals[2] = {0, 0};
    for (int k = 0; k < 2 && j < len; k++) {
      uint8_t op = p[j];
      if (op == 0x0A) { /* BytePrefix */
        vals[k] = p[j + 1];
        j += 2;
      } else if (op == 0x0B) { /* WordPrefix */
        vals[k] = p[j + 1];
        j += 3;
      } else if (op == 0x0C) { /* DWordPrefix */
        vals[k] = p[j + 1];
        j += 5;
      } else if (op == 0x00) { /* ZeroOp */
        vals[k] = 0;
        j += 1;
      } else if (op == 0x01) { /* OneOp */
        vals[k] = 1;
        j += 1;
      } else {
        break;
      }
    }
    *typa = vals[0];
    *typb = vals[1];
    serial_print("ACPI: _S5 SLP_TYP = ");
    serial_print_hex(*typa);
    serial_print("\n");
    return;
  }
}

bool acpi_poweroff(void) {
  acpi_sdt_header_t *fadt = acpi_find_table("FACP");
  if (!fadt)
    return false;

  uint32_t pm1a_cnt = rd32((uint8_t *)fadt + FADT_PM1A_CNT_BLK);
  uint32_t pm1b_cnt = rd32((uint8_t *)fadt + FADT_PM1B_CNT_BLK);
  if (!pm1a_cnt)
    return false;

  /* Garante que o SCI está habilitado (ACPI mode). */
  uint32_t pm1a_evt = rd32((uint8_t *)fadt + FADT_PM1A_EVT_BLK);
  uint32_t smi_cmd = rd32((uint8_t *)fadt + FADT_SMI_CMD);
  uint8_t acpi_enable = *((uint8_t *)fadt + FADT_ACPI_ENABLE);
  if (pm1a_evt && !(inw((uint16_t)pm1a_evt) & 1) && smi_cmd && acpi_enable) {
    outb((uint16_t)smi_cmd, acpi_enable);
    for (int i = 0; i < 1000000 && !(inw((uint16_t)pm1a_evt) & 1); i++)
      io_wait();
  }

  uint8_t slp_a = 0, slp_b = 0;
  parse_s5slp(fadt, &slp_a, &slp_b);

  outw((uint16_t)pm1a_cnt, (uint16_t)((slp_a << 10) | SLP_EN));
  if (pm1b_cnt)
    outw((uint16_t)pm1b_cnt, (uint16_t)((slp_b << 10) | SLP_EN));

  /* Fallbacks de emuladores caso o S5 acima não desligue. */
  outw(0x604, 0x2000);  /* QEMU/Bochs */
  outw(0xB004, 0x2000); /* Bochs antigo */
  outw(0x4004, 0x3400); /* VirtualBox */
  return true;
}

bool acpi_reboot(void) {
  acpi_sdt_header_t *fadt = acpi_find_table("FACP");
  if (!fadt)
    return false;
  /* RESET_REG só existe em FADT rev>=2 (tabela >= 0x81 bytes). */
  if (fadt->length < 0x81)
    return false;

  uint8_t *reg = (uint8_t *)fadt + FADT_RESET_REG;
  uint8_t space = reg[0];
  uint64_t addr = rd64(reg + 4);
  uint8_t val = *((uint8_t *)fadt + FADT_RESET_VALUE);
  if (!addr)
    return false;

  if (space == 1) { /* System I/O */
    outb((uint16_t)addr, val);
    return true;
  }
  if (space == 0) { /* System Memory */
    acpi_map(addr, 1);
    *(volatile uint8_t *)(uintptr_t)addr = val;
    return true;
  }
  return false;
}
