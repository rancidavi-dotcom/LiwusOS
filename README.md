# LiwusOS

LiwusOS é um sistema operacional experimental **x86_64 (64-bit long mode)** escrito em C, que cobre a stack inteira:

- boot (Multiboot2 via GRUB)
- kernel (GDT/IDT/ISR, PMM, VMM, kheap)
- drivers (ATA PIO, ATA BM-IDE DMA, AHCI, PCI, RTL8139, PS/2, USB, VirtIO)
- sistemas de arquivo (VFS, initrd, FAT32, SDFS, devfs)
- rede (Ethernet, ARP, IPv4, ICMP, TCP, UDP, DNS, DHCP)
- userland (ELF32/64 loader, syscalls, preempção, fork)
- shell e terminal
- GUI (LGX Compositor Espacial — Canvas Infinito)
- SDK completa com newlib, libc, libm, libpng, zlib, libjpeg
- ports reais (Doom, Lua, TCC, C4, calculadora, editores)

---

# Sumário

1. [Visão Geral](#visão-geral)
2. [Estado Atual](#estado-atual)
3. [Arquitetura](#arquitetura)
4. [Fluxo de Boot](#fluxo-de-boot)
5. [GDT, IDT e Interrupções](#gdt-idt-e-interrupções)
6. [Syscalls](#syscalls)
7. [Gerenciamento de Memória](#gerenciamento-de-memória)
8. [Tasking e Scheduler](#tasking-e-scheduler)
9. [Sistemas de Arquivo](#sistemas-de-arquivo)
10. [Drivers](#drivers)
11. [Pilha de Rede](#pilha-de-rede)
12. [LGX — Compositor Espacial](#lgx--compositor-espacial)
13. [Terminal e Shell](#terminal-e-shell)
14. [Programas de Usuário](#programas-de-usuário)
15. [SDK e Toolchain](#sdk-e-toolchain)
16. [Layout do Repositório](#layout-do-repositório)
17. [Como Compilar e Rodar](#como-compilar-e-rodar)
18. [Limitacoes Atuais](#limitações-atuais)
19. [Roadmap](#roadmap)
20. [FAQ](#faq)
21. [Créditos](#créditos)

---

# Visão Geral

O LiwusOS é um sistema operacional *own-stack*: o repositório cobre o caminho inteiro do boot ao SDK, passando por kernel, drivers, rede, filesystem e GUI.

Hoje o sistema já roda:
- **Doom** (port do doomgeneric)
- **Lua 5.4** (interpretador completo)
- **TCC** (Tiny C Compiler — compile C dentro do próprio OS)
- **Shell** com comandos reais (ls, cd, mkdir, rm, cp, mv, edit, top, ping, wget, ifconfig, free, df, uptime, uname, whoami)
- **Rede Ethernet real** (ICMP ping com respostas do gateway)
- **LGX** (Compositor Espacial com Canvas Infinito, zoom/pan, minimapa, menu radial)
- **Editor de texto** (modo terminal e modo GUI)

---

# Estado Atual

O LiwusOS passou por diversas fases e hoje está num estágio maduro para um hobby OS:

- **Arquitetura: x86_64 (64-bit long mode)** — não é mais 32 bits
- **Boot:** Multiboot2 com GRUB, framebuffer 1024x768x32
- **Kernel:** Monolítico, higher-half, com suporte a ELF32 e ELF64
- **GUI e Terminal rodam simultaneamente** — o LGX Compositor Espacial e o terminal são tasks cooperativas
- **Sistema de arquivo persistente:** SDFS (Simple Disk File System) proprietário, com initrd como live/preview e SDFS no disco persistente
- **AHCI e ATA BM-IDE DMA** suportados para acesso a disco
- **Rede funcional:** ARP + IPv4 + ICMP (ping), TCP/UDP/DNS/DHCP em desenvolvimento
- **USB:** Subsistema com suporte a UHCI, EHCI e HID
- **Fork() e waitpid()** implementados com isolamento de páginas por processo
- **SDK completa:** newlib como libc, libm, libz, libpng, libjpeg, liblgx
- **TCC portado:** compile programas C diretamente no OS

---

# Arquitetura

```
Boot (GRUB Multiboot2)
├── boot.s (entrada x86_64, PML4, long mode)
├── interrupt.s (ISRs, IRQs, syscall gate)
├── linker.ld (layout do kernel)
└── grub.cfg (config GRUB)

Kernel Core (src/kernel/)
├── kernel.c (kernel_main — inicialização)
├── gdt.c (8 entradas: null, ring0 code/data, ring3 code/data, TSS)
├── idt.c (256 entradas — exceções, IRQs, syscall)
├── isr.c (handlers de exceção, IRQ, syscall)
├── pmm.c (bitmap de memória física)
├── vmm.c (paginacao 4 níveis: PML4→PDP→PD→PT)
├── kheap.c (alocador free-list com coalescing)
├── task.c (scheduler round-robin, fork, waitpid)
├── timer.c (PIT 100Hz)
├── elf.c (loader ELF32/64)
├── syscall.c (20+ syscalls)
├── string.c (libc string do kernel)
├── fast_memcpy.s (SSE2-optimized memcpy)
├── process.s (context switch assembly)
└── lgx.c (compositor espacial)

Drivers (src/drivers/)
├── ata.c (ATA PIO + BM-IDE DMA)
├── ahci.c (AHCI SATA)
├── keyboard.c (PS/2, ABNT2, Set 1)
├── mouse.c (PS/2, relativo, botoes)
├── vga.c (framebuffer + VGA text mode)
├── serial.c (COM1, 38400 baud, log)
├── pci.c (256 buses, 32 devices, 8 funções)
├── rtl8139.c (driver de rede)
├── gpu.c (VirtIO GPU init)
├── usb.c (subsistema USB)
├── uhci.c (UHCI controller)
├── ehci.c (EHCI controller)
└── usb_hid.c (USB HID)

Filesystems (src/fs/)
├── vfs.c (VFS unificado com mount points)
├── initrd.c (TAR ustar — live/preview)
├── fat32.c (FAT32 leitura/escrita, 8.3)
├── sdfs.c (Simple Disk File System, proprietário)
└── devfs.c (/dev — device filesystem)

Rede (src/net/)
├── net.c (registro de interfaces)
├── netstack.c (Ethernet/ARP/IPv4/ICMP)
├── tcp.c (TCP — em progresso)
├── udp.c (UDP)
├── http.c (HTTP client experimental)
├── dns.c (DNS resolver)
└── dhcp.c (DHCP client)

Apps (src/apps/ + apps/)
├── terminal.c (shell + editor + top, ~2000 linhas)
├── editor.c (componente de editor)
├── liw_app.c (app launcher stub)
├── apps/calc/ (calculadora)
├── apps/c4/ (C compiler in 4 functions)
├── apps/doomgeneric/ (Doom port)
├── apps/doomprobe/ (hardware probe)
├── apps/lua/ (Lua 5.4)
├── apps/tcc/ (Tiny C Compiler)
├── apps/kilo/ (Kilo editor port)
├── apps/editor_nano/ (editor GUI nano-like)
├── apps/demo_gui/ (LGX GUI demo)
└── apps/view/ (file viewer)
```

---

# Fluxo de Boot

1. **GRUB** carrega `kernel.bin` e `initrd.tar` via Multiboot2, configurando framebuffer 1024x768x32
2. **`boot.s`**: monta PML4 com identidade dos primeiros 256MB via páginas de 2MB, habilita PAE + long mode via EFER MSR, carrega GDT temporária, far jump para `entry_64`, chama `kernel_main(magic, mbi_addr)`
3. **`kernel.c`** (`kernel_main`):
   - Inicializa serial, parseia info Multiboot2 (memória, framebuffer, módulos)
   - `init_gdt()`, `init_idt()`, `init_fpu()`
   - `pmm_init()` — bitmap baseado no memory map
   - `init_vmm()` — paginação 4 níveis
   - `kheap` — alocador do kernel
   - Mapeia framebuffer do bootloader
   - `vfs_init()` — VFS unificado
   - `lgx_init()` — compositor LGX
   - `vga_init()` — terminal texto
   - `init_mouse()`, `pci_init()`, `ahci_init()`, `ata_bmide_init()`
   - `net_init()`, `tcp_init()`, `udp_init()`, `dns_init()`
   - `usb_init()` — subsistema USB
   - Carrega initrd via GRUB module
   - Inicializa RTL8139 (placa de rede)
   - Monta SDFS no disco (ATA ou AHCI), copia initrd no primeiro boot
   - Fallback para ramdisk se não há disco
   - `init_timer(100)`, `init_tasking()`, `init_syscalls()`
   - `usb_start_polling()`
   - Cria tasks: `lgx_comp` + `terminal`
   - `sti` + idle loop (HLT)

---

# GDT, IDT e Interrupções

## GDT — 8 entradas

| Offset | Descrição |
|--------|-----------|
| 0x00 | Null segment |
| 0x08 | Kernel code 64-bit (L=1, DPL=0) |
| 0x10 | Kernel data 64-bit |
| 0x18 | User code 32-bit compat |
| 0x20 | User data 32-bit |
| 0x28 | User code 64-bit (L=1, DPL=3) |
| 0x30 | User data 64-bit |
| 0x38 | TSS (Task State Segment) |

## IDT — 256 entradas

- **0–31**: Exceções da CPU (page fault, GPF, etc.)
- **32–47**: IRQs remapeados (PIC)
- **48–127**: Reservados
- **128 (0x80)**: Syscall gate (ring-3 acessível, flag 0xEE)

---

# Syscalls

Chamadas via `int $0x80` com número da syscall em `eax`.

| # | Nome | Descrição |
|---|------|-----------|
| 1 | `sys_exit` | Encerra processo |
| 2 | `sys_brk` | Expande heap |
| 3 | `sys_read` | Leitura de fd |
| 4 | `sys_write` | Escrita em fd |
| 5 | `sys_open` | Abre arquivo |
| 6 | `sys_close` | Fecha fd |
| 7 | `sys_waitpid` | Aguarda filho |
| 8 | `timer_ticks` | Ticks do timer |
| 10 | `keyboard_get_event` | Evento de teclado |
| 11 | `keyboard_is_pressed` | Estado de tecla |
| 14 | `fork_process` | Fork (duplica processo) |
| 15 | `do_execve` | Executa programa |
| 16 | `sys_tcgetattr` | Atributos do terminal |
| 17 | `sys_tcsetattr` | Define atributos do terminal |
| 18 | `sys_ioctl` | I/O control (TIOCGWINSZ) |
| 19 | `sys_lseek` | Seek em arquivo |
| 120 | `sys_gui_create_window` | Cria janela LGX |
| 121 | `sys_gui_get_buffer` | Obtém framebuffer da janela |
| 122 | `sys_refresh_window` | Atualiza janela |

O **libgloss** (`libgloss/`) provê wrappers POSIX para userland: `open`, `read`, `write`, `close`, `fork`, `execve`, `waitpid`, `exit`, `sbrk`, `lseek`, `tcgetattr`, `tcsetattr`, `ioctl`, `sleep`. Também inclui stubs de compatibilidade TCC (`sysconf`, `mprotect`, `getcwd`, `dlopen`, etc.).

---

# Gerenciamento de Memória

## PMM (Physical Memory Manager) — `pmm.c`
- Bitmap de 1 bit por página de 4KB
- Inicializa marcando tudo como usado, depois libera regiões do memory map Multiboot2
- Suporte a >4GB via endereços de 64 bits

## VMM (Virtual Memory Manager) — `vmm.c`
- Paginação 4 níveis x86_64: PML4 → PDP → PD → PT
- Páginas de 4KB com suporte a páginas grandes (2MB) com split automático
- Diretórios de página por processo com herança do kernel
- Mapeamento de framebuffer via endereço real do bootloader

## Kernel Heap — `kheap.c`
- Free-list com coalescing
- `kmalloc()`, `kmalloc_a()` (alinhado), `kmalloc_ap()` (alinhado + físico), `kfree()`
- Cresce linearmente a partir de `end` + overhead de metadados

## Layout de Memória
- Kernel: 0x100000 (1MB)
- Kernel heap: após BSS do kernel
- User heap: 0x40000000+
- User stack: 0xC0000000 (64KB, cresce para baixo)

---

# Tasking e Scheduler

- Lista circular duplamente ligada de tasks
- Round-robin com preempção via PIT (IRQ0, 100 Hz)
- Estados: RUNNING, READY, SLEEPING, ZOMBIE
- Context switch salva/restaura todos os registradores (PUSH_ALL/POP_ALL)
- TSS atualizado a cada switch (ESP0 para kernel stack em syscalls)
- `fork()` com cópia do diretório de páginas
- `waitpid()` com suspensão do pai
- `Ctrl+C` mata foreground task
- `top` exibe dados reais via `task_snapshot()`

---

# Sistemas de Arquivo

## VFS (Virtual File System) — `vfs.c`
- Pontos de montagem com resolução de path "best-match"
- Operações via ponteiros de função: `read`, `write`, `readdir`, `finddir`, `create`

## Initrd — `initrd.c`
- TAR ustar carregado pelo GRUB
- Read-only, montado para boot/preview
- Contém: doomgeneric, freedoom1.wad, lua, calc, tcc, nano, demo_gui, hello.liwpkg

## FAT32 — `fat32.c`
- Leitura e escrita completas
- Nomes curtos 8.3 (sem LFN ainda)
- Criação, deleção, renomeio, diretórios

## SDFS (Simple Disk File System) — `sdfs.c`
- Sistema proprietário para o disco persistente `liwus_disk.img`
- Block size: 4096 bytes (8 setores)
- Alocação via bitmap de blocos
- Backends: ATA PIO, BM-IDE DMA, AHCI
- Ramdisk fallback (64MB) quando não há disco
- First-boot: copia todo o initrd para o SDFS, cria flag `/.system_installed`

## DevFS — `devfs.c`
- Sistema de arquivos de dispositivo (`/dev/`)

---

# Drivers

| Driver | Arquivo | Descrição |
|--------|---------|-----------|
| ATA PIO | `ata.c` | LBA28, buses primário/secundário |
| ATA BM-IDE DMA | `ata.c` | DMA via PIIX3, ~8 setores por chamada |
| AHCI | `ahci.c` | SATA, controladora avançada |
| PS/2 Keyboard | `keyboard.c` | Set 1, ABNT2, shift/ctrl/altgr, fila de chars + eventos |
| PS/2 Mouse | `mouse.c` | Movimento relativo, botões |
| Serial | `serial.c` | COM1 (0x3F8), 38400 baud |
| VGA/Framebuffer | `vga.c` | Texto 80x25 + framebuffer linear |
| GPU | `gpu.c` | VirtIO GPU init |
| PCI | `pci.c` | Enumeração 256 buses |
| RTL8139 | `rtl8139.c` | I/O, 4 TX descritores, RX ring buffer, IRQ 11 |
| USB | `usb.c` | Subsistema USB |
| UHCI | `uhci.c` | USB 1.1 controller |
| EHCI | `ehci.c` | USB 2.0 controller |
| USB HID | `usb_hid.c` | Human Interface Device |

---

# Pilha de Rede

Interface `eth0` no RTL8139 (IP 10.0.2.15, gateway 10.0.2.2, emulado pelo QEMU).

- **Ethernet**: Quadros reais de/para a placa
- **ARP**: Resolução de MAC (request/reply)
- **IPv4**: Roteamento e manipulação de pacotes
- **ICMP**: Echo request/reply (`ping` funcional com output real)
- **TCP**: Implementação inicial (em progresso)
- **UDP**: Implementação básica
- **HTTP**: Cliente experimental (base para `wget`)
- **DNS**: Resolução de nomes
- **DHCP**: Cliente DHCP (inicial)

---

# LGX — Compositor Espacial

O LGX (Liwus Graphics eXtension) é um compositor espacial com **Canvas Infinito**, rodando a 60 FPS via software (alpha blending de bitmaps).

**Características:**
- Fundo infinito com dot-grid e orientação visual
- Cards como janelas (sem bordas, sem title bars, cantos arredondados)
- Zoom In/Out (teclas `+` / `-`)
- Pan (arrastar o fundo com o mouse)
- Return to Home (tecla `H` — reseta zoom e câmera)
- Fit to Screen (tecla `F` — enquadra todas as janelas)
- Radar HUD (minimapa no canto inferior direito)
- Off-screen edge indicators (indicadores âmbar nas bordas)
- Menu Radial no clique direito
- Tema Cartoon Minimalista com mascote Ornitorrinco
- Pop-ups de erro quando um executável falha

**Syscalls GUI** (para apps de usuário):
- `120` — `sys_gui_create_window`
- `121` — `sys_gui_get_buffer`
- `122` — `sys_refresh_window`

---

# Terminal e Shell

O terminal (`src/apps/terminal.c`, ~2000 linhas) é a interface de comando do sistema, rodando como task cooperativa junto com o LGX.

**Comandos embutidos:**
- `ls`, `cd`, `pwd`, `touch`, `mkdir`, `rm`, `cp`, `mv`
- `edit` — editor de texto em modo terminal
- `top` — monitor de tasks/memória/scheduler com dados reais
- `ifconfig`, `ping`, `wget` — rede
- `lua` — executa scripts Lua
- `free` — uso de memória RAM
- `df` — espaço em disco/initrd
- `uptime` — tempo desde o boot
- `uname` — versão e arquitetura
- `whoami` — identidade do usuário
- `help` — lista de comandos

**Suporte:**
- TAB autocomplete (integração com VFS)
- Console mode para apps (raw terminal)
- ANSI escape sequences
- Execução de programas ELF diretamente pelo nome

---

# Programas de Usuário

| Programa | Descrição |
|----------|-----------|
| **hello** | Hello World de teste |
| **calc** | Calculadora |
| **c4** | C4 — compilador C em 4 funções |
| **doomgeneric** | Doom port (render via syscall, freedoom1.wad incluso) |
| **lua** | Lua 5.4 interpreter (scripts `.lua`) |
| **tcc** | Tiny C Compiler (compile C dentro do LiwusOS) |
| **kilo** | Kilo editor port (nano-like) |
| **editor_nano** | Editor GUI nano-like |
| **demo_gui** | Demonstração da API gráfica LGX |
| **view** | Visualizador de arquivos |

---

# SDK e Toolchain

O LiwusOS possui uma SDK completa para desenvolvimento de programas userland.

**libgloss** (`libgloss/`):
- `crt0.S` — runtime C de entrada para programas
- `syscalls.c` — wrappers POSIX (open, read, write, fork, execve, etc.)

**SDK (`sdk/`)**:
- `include/` — headers C compatíveis com newlib (stdio.h, stdlib.h, string.h, unistd.h, math.h, sys/stat.h, etc.)
- `lib/` — bibliotecas pré-compiladas: `libc.a` (newlib), `libm.a` (newlib math), `libgloss.a`, `liblgx.a`, `libz.a`, `libpng.a`, `libjpeg.a`
- `tools/` — `liw-builder` (empacotamento LIW), `img-gen` (geração de imagens), `gen_ui_assets.py`, `gen_wallpaper.py`, `img2c.py`, `convert_wallpaper.py`

**Toolchain customizado** (`toolchain/`):
- Script `build-x86_64-liwusos-toolchain.sh` para construir toolchain completa

**Bibliotecas de terceiros** incluídas em `third_party/`:
- Lua 5.4.6 (código fonte completo)
- TCC (Tiny C Compiler)
- doomgeneric
- zlib, libpng, libjpeg

---

# Layout do Repositório

```
├── src/
│   ├── boot/          # boot.s, interrupt.s, linker.ld, grub.cfg
│   ├── kernel/        # kernel.c, gdt.c, idt.c, pmm.c, vmm.c, task.c, elf.c, ...
│   ├── drivers/       # ata.c, ahci.c, keyboard.c, mouse.c, rtl8139.c, usb/...
│   ├── fs/            # vfs.c, initrd.c, fat32.c, sdfs.c, devfs.c
│   ├── net/           # netstack.c, tcp.c, udp.c, dns.c, dhcp.c
│   └── apps/          # terminal.c, editor.c, liw_app.c
├── include/           # Todos os headers do kernel (47 arquivos)
├── apps/              # Ports: doomgeneric, lua, tcc, calc, c4, kilo, ...
├── libgloss/          # crt0.S + syscalls.c (glue layer userland)
├── liblgx/            # Biblioteca LGX para userland
├── libc/              # Funções string do kernel (memcpy, memset, etc.)
├── sdk/               # Headers newlib, libs pré-compiladas, ferramentas
├── third_party/       # lua-5.4.6, tcc, doomgeneric, zlib, libpng, libjpeg
├── toolchain/         # Script de build da toolchain x86_64-liwusos
├── repo/              # Staging do initrd (binários + headers)
├── isodir/            # Staging da ISO
├── assets/            # Logos, wallpapers
├── Dockerfile         # Ambiente cross-compilação (i686-elf-gcc)
├── Makefile           # Build completo do sistema
├── run.sh             # Build + QEMU
├── build.sh           # Build via Docker
├── clean.sh           # Clean via Docker
└── build_app.sh       # Compila app userland individual
```

---

# Como Compilar e Rodar

**Pré-requisitos:** Docker

```bash
# Build + run em QEMU
sudo ./run.sh

# Apenas build
sudo ./build.sh

# Run com output serial
make run-serial

# Run com logs de debug
make run-log

# Compilar um app userland individual
./build_app.sh apps/calc/calc.c
```

O Docker constrói um ambiente com `i686-elf-gcc`, binutils 2.42, GCC 13.2.0, GRUB tools, xorriso, mtools, NASM e QEMU.

**QEMU flags:**
```bash
qemu-system-x86_64 -m 512M \
  -cdrom liwusos.iso \
  -drive id=disk,file=liwus_disk.img,if=none,format=raw \
  -device ahci,id=ahci \
  -device ide-hd,drive=disk,bus=ahci.0 \
  -vga std \
  -serial stdio \
  -net nic,model=rtl8139 \
  -net user,hostfwd=tcp::2222-:2222
```

---

# Como Testar

## Terminal e disco
```
pwd
ls
mkdir TESTE
cd TESTE
touch ARQUIVO.TXT
ls
```

## Editor
```
edit ARQUIVO.TXT
```

## Lua
```
lua hello.lua
```

## Rede
```
ifconfig
ping 10.0.2.2 4
```

## Monitoramento
```
top
free
df
uptime
uname
whoami
```

## Doom
O binário `doomgeneric` e o arquivo `freedoom1.wad` estão no initrd.

## Compilar C dentro do OS
```
tcc hello.c -o hello
./hello
```

---

# Limitações Atuais

- TCP ainda em maturação (wget experimental)
- FAT32 sem suporte a LFN (apenas 8.3)
- Sem suporte a SMP
- Sem sistema de áudio consolidado
- Sem TLS/HTTPS
- USB funcional mas experimental
- POSIX incompleto (programas Linux complexos não portam facilmente)
- Sem gerenciamento de energia

---

# Roadmap

1. **Consolidar rede:** TCP estável, wget confiável, DHCP automático
2. **Consolidar userland:** mais ports, utilitários, melhor editor
3. **Amadurecer USB:** mass storage, mais dispositivos
4. **Evoluir GUI:** LVGL ou continuar LGX nativo com mais widgets
5. **SMP:** suporte a múltiplos cores
6. **Audio:** subsistema de som

---

# FAQ

**O LiwusOS é 32 ou 64 bits?**
x86_64 (64-bit long mode). Suporta binários ELF32 e ELF64 em userland.

**O sistema tem disco persistente?**
Sim. O SDFS (`liwus_disk.img`) mantém dados entre boots. No primeiro boot, copia o initrd para o disco automaticamente.

**Qual a diferença entre initrd e SDFS?**
O initrd é o sistema live/preview (RAM, read-only). O SDFS é o disco persistente montado em `/house/localhost`.

**Roda Doom de verdade?**
Sim. Doomgeneric portado com renderização via syscall. Inclui freedoom1.wad.

**Tem compilador C no sistema?**
Sim. O TCC (Tiny C Compiler) está portado. Compile e rode programas C diretamente no LiwusOS.

**Roda Lua?**
Sim. Lua 5.4 completo, portado como interpretador de scripts do sistema.

**A interface principal é o terminal ou a GUI?**
Ambas rodam simultaneamente. O terminal é o ambiente de comando; o LGX Compositor Espacial é a GUI.

---

# Créditos

- Doomgeneric — base do port de Doom
- Lua 5.4 — interpretador embutido
- TCC — Tiny C Compiler
- Newlib — libc da SDK
- LVGL — toolkit GUI considerado para futuro
- GNU toolchain — GCC, binutils
- GRUB — bootloader
- QEMU — emulador
- OSDev Wiki — referência contínua

---

*Documento atualizado em Julho de 2026. LiwusOS — um OS experimental x86_64 com stack própria.*
