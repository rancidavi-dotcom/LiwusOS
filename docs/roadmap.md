# LiwusOS — Roadmap de Hardware Real

> Documento de referência do desenvolvimento. Objetivo: tornar o LiwusOS
> **utilizável em um notebook/PC moderno** (pós-2015), não só em QEMU/BIOS.

## Contexto e alvo

- Cenário alvo: **notebook/PC moderno** — firmware **UEFI**, áudio **HDA**,
  armazenamento **NVMe**, **Wi-Fi**.
- Estado atual: boota em QEMU (BIOS/legacy + multiboot2 + GRUB), com AC97,
  AHCI/ATA, UHCI/EHCI (USB 1.1/2.0), rtl8139/r8169.
- Regra de ouro: **não quebrar o caminho BIOS/QEMU atual**. Toda fase mantém
  o boot legado funcionando enquanto adiciona o caminho moderno.
- Sempre que possível, validar headless em QEMU antes de hardware físico.

## Fases

### Fase 0 — UEFI boot (portão de entrada) — `CONCLUÍDA`
Sem UEFI o firmware moderno ignora o ISO; nada do resto importa.

- [x] Mapear o boot atual (GRUB + `multiboot2`, `grub.cfg`, framebuffer VBE).
- [x] Confirmar `grub.cfg` adequado a GOP: já usava `insmod all_video` +
      `gfxpayload=keep`; nenhuma alteração necessária.
- [x] Gerar ISO com boot UEFI (`scripts/make_uefi_iso.sh`). Híbrido
      (BIOS+UEFI) usando os módulos `x86_64-efi` do sistema; se ausentes,
      extrai `grub-efi-amd64-bin` para `build/grub-efi` (sem root) e monta
      via `unshare -rm` + bind mount sobre `/usr/lib/grub`. Último recurso:
      ISO só-UEFI com `-d`.
- [x] Validar boot em **OVMF** (`q35` + `OVMF_CODE_4M.fd`), serial até
      `LIWUS_BOOT_READY`, GUI via GOP e 0 exceções.
- [x] Manter alvos separados (`make uefi-iso` / `make uefi-check`) sem
      afectar o boot BIOS (`make liwusos.iso`).

Entregável: `make uefi-iso` gera `liwusos-uefi.iso` híbrido (BIOS+UEFI);
`make uefi-check` valida automaticamente em OVMF.

Validação (2026-09-17, mesmo ISO híbrido): UEFI PASS 12–30s / 0 exceções;
BIOS PASS 8s / 0 exceções (regressão verificada).

### Fase 1 — Áudio HDA — `CONCLUÍDA`
Hoje só há AC97 (não existe em hardware pós-~2008). Notebook moderno usa
Intel HDA.

- [x] Enumerar o controlador HDA via PCI (classe `0x0403`).
- [x] Implementar CORB/RIRB (verbos de codec) e BDL de stream de saída.
- [x] Reaproveitar a infraestrutura de BDL/DMA do AC97.
- [x] Detectar codecs, escolher DAC, configurar taxa/volume.
- [x] Testar em QEMU: `-device intel-hda -device hda-duplex`.

Entregável: `audio.c` detecta HDA ou AC97 e toca o mesmo som nos dois.

Commit: `014c1cc` (driver HDA — controlador, codec e playback DMA).

### Fase 2 — Armazenamento/USB modernos — `CONCLUÍDA`
Faltam **NVMe** (SSDs modernos) e **XHCI** (USB 3 — teclado/mouse/pendrive
em máquinas novas).

- [x] Driver NVMe (admin queue + I/O queues, comandos identify/read/write).
- [x] Driver XHCI (USB 3), integrando com a camada USB existente
      (`usb.c`, `usb_hid.c`, `scsi.c`/`pen.c`).
- [x] Testar em QEMU: `-device nvme` e `-device qemu-xhci`.

Entregável: OS monta disco SDFS/FAT32 em NVMe e aceita input via USB3.

Commits: `16bfda4` (NVMe — admin/IO queues, identify e I/O SDFS) e `f8882f0`
(xHCI — bring-up, slots/EP0, HID e polling). Regressão: `scripts/xhci_boot_test.sh`
PASS e `scripts/run_tests.sh --net` 42/44.

### Fase 3 — Plataforma (ACPI, ECAM, SMP) — `CONCLUÍDA`
Hardware moderno usa ACPI e config PCI via MMIO (ECAM/MCFG).

- [x] Parser ACPI (RSDP/XSDT), MADT e MCFG.
- [x] Acesso PCI via ECAM quando disponível (fallback CF8/CFC).
- [x] Subir APs (SMP) com o IOAPIC roteado corretamente.
- [x] Desligar/reboot via ACPI (power management básico).

Entregável: boot estável com múltiplos núcleos e shutdown correto.

Commits: `80d408f` (ACPI RSDP/XSDT, FADT, MCFG/ECAM + shutdown/reboot) e
`apic/smp` (APIC sobre ACPI, APs em long mode). Validado: i440fx (RSDT,
CF8/CFC), q35 (MCFG ECAM ativo, `-smp 4` sobe CPUs 1–3), OVMF (XSDT, ECAM
0xE0000000); `run_tests.sh --net` 42/44, `gui_boot_test.sh`,
`xhci_boot_test.sh` e `make uefi-check` PASS.

### Fase 4 — Wi-Fi (mais caro, por último)
Não há stack 802.11. "Vida real" em notebook = Wi-Fi.

- [ ] Escolher um chipset inicial (ex.: USB RTL8188EU/8192CU) ou RNDIS
      (tethering) como atalho.
- [ ] Stack 802.11 mínima (scan, auth, assoc, WPA2) + integração com a
      netstack existente.
- [ ] Intel iwlwifi (precisa de firmware) como fase posterior.

Entregável: conectar em rede Wi-Fi e navegar/baixar.

### Fase 5 — Input real — `CONCLUÍDA`
Alvo é desktop/PC sem touchpad, então touchpad/trackpoint ficam fora de escopo.

- [x] Layouts de teclado adicionais (ABNT2 já pronto): US e US International (com dead keys via AltGr).
- [x] Ajustes de sensibilidade/aceleração do mouse (comando `msense`).

Entregável: teclado multi-layout e mouse configurável.

Commits: `input-fase5` (keyboard layouts US/US-INTL + mouse sensitivity/acceleration). Validado: `run_tests.sh --net` 42/44, `gui_boot_test.sh`, `xhci_boot_test.sh`, `make uefi-check` PASS.

### Fase 6 — HTTPS/TLS (BearSSL) — `CONCLUÍDA`
Stack de rede agora suporta HTTPS/TLS 1.2 via BearSSL (bare-metal, sem dependências POSIX).

- [x] Integração BearSSL 0.6 (static lib, ~850 KB) com build customizado (`CONF=LiwusOS`): `-fno-stack-protector`, sem fortify, sem pthreads/POSIX.
- [x] TLS 1.2 client: `br_ssl_client_init_full`, `br_ssl_client_reset`, SNI, handshake completo.
- [x] Simplified I/O (`br_sslio`) com callbacks TCP personalizados (`tcp_receive`/`tcp_send`).
- [x] `http_get_url()` detecta `https://` e usa TLS automaticamente (porta 443, SNI).
- [x] Stubs POSIX mínimos para BearSSL: `time`, `__errno_location`, `open`/`read`/`close` (stubs).
- [x] Certificado X.509 validation: `br_x509_minimal` com trust anchors (pode ser desabilitado para testes).

Entregável: `wget https://...` e `http_get_url("https://...")` funcionam.

Commits: `bearssl-tls` (BearSSL TLS 1.2 client + HTTPS HTTP client). Validado: `run_tests.sh --net` 42/44, `gui_boot_test.sh`, `xhci_boot_test.sh`, `make uefi-check` PASS.

### Backlog / desejáveis (não bloqueiam uso diário)
- [ ] Vídeo: **MJPEG/AVI** reaproveitando libjpeg/libpng (atalho viável).
      H.264/AAC só depois; custo/patentes altíssimos.
- [ ] Browser mínimo (HTML/CSS básico).
- [ ] Instalador/persistência robusta em disco físico.

## Como validar

- Kernel tests: `bash scripts/run_tests.sh --net` (inclui scheduler, SDFS, rede).
- Boot normal headless: `scripts/run.sh`-like com `-serial file:...` até
  `LIWUS_BOOT_READY`; verificar 0 `CPU exception`.
- UEFI: QEMU + OVMF (`/usr/share/OVMF/OVMF_CODE_4M.fd`).
- HDA: `-device intel-hda -device hda-duplex`.
- NVMe/XHCI: `-device nvme`, `-device qemu-xhci`.

## Histórico de decisões

- 2026-09: priorizadas Fases 0–5 acima; vídeo (.mp4) rebaixado para backlog
  por não destravar usabilidade diária.
- 2026-09: scheduler ganhou prioridade (weighted round-robin) + `sleep` real
  + comando `ps` (commit `b825410`).
- 2026-09: httpd passou a ser iniciado manualmente pelo terminal (commit
  `174dfa0`).
