# Relatório Técnico de Auditoria — LiwusOS

> Auditoria completa do código-fonte, arquitetura, sistemas, drivers, APIs,
> ferramentas, dependências e estado atual do projeto LiwusOS.
>
> Data da auditoria: Julho 2026

---

## Índice

1. [Resumo Executivo](#1-resumo-executivo)
2. [Arquitetura Geral do Sistema](#2-arquitetura-geral-do-sistema)
3. [Fluxo de Inicialização (Boot → Kernel → Userspace)](#3-fluxo-de-inicialização)
4. [Gerenciamento de Memória](#4-gerenciamento-de-memória)
5. [Sistema de Processos e Tarefas](#5-sistema-de-processos-e-tarefas)
6. [Chamadas de Sistema (Syscalls)](#6-chamadas-de-sistema)
7. [Sistema de Arquivos](#7-sistema-de-arquivos)
8. [Drivers de Hardware](#8-drivers-de-hardware)
9. [Pilha de Rede](#9-pilha-de-rede)
10. [Interface Gráfica e Compositor](#10-interface-gráfica-e-compositor)
11. [API Gráfica de Usuário (SDK)](#11-api-gráfica-de-usuário)
12. [Terminal e Shell](#12-terminal-e-shell)
13. [Aplicativos Incluídos](#13-aplicativos-incluídos)
14. [Bibliotecas Internas e de Terceiros](#14-bibliotecas-internas-e-de-terceiros)
15. [Sistema de Build e Toolchain](#15-sistema-de-build-e-toolchain)
16. [Compatibilidade de Hardware](#16-compatibilidade-de-hardware)
17. [Inconsistências e Problemas Encontrados](#17-inconsistências-e-problemas-encontrados)
18. [Débitos Técnicos](#18-débitos-técnicos)
19. [Diagrama de Arquitetura](#19-diagrama-de-arquitetura)
20. [Estado de Maturidade](#20-estado-de-maturidade)
21. [Sugestões de Melhoria](#21-sugestões-de-melhoria)

---

## 1. Resumo Executivo

O LiwusOS é um sistema operacional hobby para a arquitetura x86_64, escrito
primariamente em C (GNU C99) e Assembly x86 (NASM/GAS). O sistema abrange uma
stack completa: bootloader (GRUB Multiboot2), kernel monolítico higher-half,
gerenciamento de memória (PMM/VMM/heap), multitarefa preemptiva com fork/exec,
sistema de arquivos virtual (VFS) com suporte a initrd, SDFS (persistente) e
FAT32, pilha de rede (Ethernet/ARP/IPv4/ICMP/TCP/UDP/DNS/DHCP), drivers de
hardware (ATA, AHCI, PCI, PS/2, USB, RTL8139, GPU/BGA), um compositor gráfico
com scene graph (LGX), terminal com shell, e apps de usuário em ring-3.

**Estatísticas do código-fonte:**
- ~97 arquivos-fonte C/ASM no kernel (`src/`)
- ~51 arquivos de cabeçalho (`include/`)
- ~28 arquivos no subsistema GUI (`src/kernel/gui/`)
- ~7 arquivos na pilha de rede (`src/net/`)
- ~5 arquivos no sistema de arquivos (`src/fs/`)
- ~15 arquivos de drivers (`src/drivers/`)
- SDK completa com ~150 headers newlib
- Toolchain customizada: binutils 2.42, GCC 14.1.0, newlib 4.4.0
- Bibliotecas de terceiros: Lua 5.4.6, TCC, doomgeneric, zlib, libpng, libjpeg

---

## 2. Arquitetura Geral do Sistema

O LiwusOS é um **kernel monolítico higher-half** com a seguinte organização:

```
┌─────────────────────────────────────────────────────┐
│  GRUB2 (Multiboot2) → carrega kernel.bin + initrd  │
├─────────────────────────────────────────────────────┤
│  Ring 0 — Kernel Monolítico                         │
│  ├── Boot Assembly (boot.s, interrupt.s, process.s) │
│  ├── Core (GDT, IDT, TSS, PMM, VMM, kheap)         │
│  ├── Syscall Handler (int $0x80)                    │
│  ├── Scheduler (round-robin preemptivo)             │
│  ├── Drivers (ATA, AHCI, PCI, PS/2, USB, RTL8139)  │
│  ├── Filesystems (VFS, SDFS, FAT32, initrd, devfs)  │
│  ├── Networking (ARP, IPv4, ICMP, TCP, UDP, DNS)    │
│  ├── GUI Compositor (scene graph, camera, renderer) │
│  ├── Built-in Apps (terminal, settings, explorer)    │
│  └── ELF Loader (ELF32 + ELF64)                     │
├─────────────────────────────────────────────────────┤
│  Ring 3 — User-Space ELF Applications               │
│  ├── crt0.S → main() → _exit()                     │
│  ├── libgloss/syscalls.c (bridge int $0x80)         │
│  ├── newlib (libc.a, libm.a)                        │
│  ├── SDK (libliw.h, liwus_gui.h)                    │
│  └── Apps (Doom, Lua, TCC, calc, view, etc.)        │
├─────────────────────────────────────────────────────┤
│  Storage: initrd.tar (live) + SDFS (persistente)    │
└─────────────────────────────────────────────────────┘
```

**Características arquiteturais:**
- Higher-half: kernel carregado em 0x1000000 (16 MB)
- Paginação 4 níveis x86_64 (PML4 → PDP → PD → PT)
- Compositor gráfico roda como kernel task (ring-0)
- Apps de sistema (terminal, settings, etc.) rodam como kernel tasks (ring-0)
- Apps externos (Doom, Lua, calc, view) rodam em ring-3 via fork/execve
- Syscalls via `int $0x80` (não via instrução `syscall`)

---

## 3. Fluxo de Inicialização

### 3.1 Boot Assembly (`src/boot/boot.s`)

1. GRUB2 carrega `kernel.bin` e módulo `initrd.tar` via Multiboot2
2. Magic number verificado: `0x2BADB002`
3. Monta PML4 com identidade dos primeiros 256 MB via páginas grandes de 2 MB
4. Habilita PAE + Long Mode via MSR EFER
5. Far jump para `entry_64` em modo longo
6. Chama `kernel_main(magic, mbi_addr)`

**Nota:** O boot usa magic do Multiboot2 (`0xe85250d6` no header), mas o kernel
parseia com estrutura Multiboot1 (`multiboot.h`). Funciona porque o GRUB2 entra
em modo de compatibilidade.

### 3.2 Inicialização do Kernel (`src/kernel/kernel.c:kernel_main`)

Ordem de inicialização verificada no código:

| Etapa | Função | Descrição |
|-------|--------|-----------|
| 1 | `init_serial()` | COM1 serial (115200 baud) para debug |
| 2 | Parse Multiboot2 | Memory map, framebuffer, módulos |
| 3 | `init_gdt()` | 9 entradas + TSS |
| 4 | `init_idt()` | 256 entradas, PIC remapeado |
| 5 | `init_fpu()` | Habilita FPU no kernel |
| 6 | `pmm_init()` | Bitmap allocator baseado no memory map |
| 7 | `init_vmm()` | Paginação 4 níveis, identity-map |
| 8 | `kheap_init()` | Free-list allocator |
| 9 | `vmm_map_framebuffer()` | Mapeia VRAM do bootloader (PAT1 write-combining) |
| 10 | `vfs_init()` | VFS unificado |
| 11 | `lgx_init()` | Inicializa compositor LGX |
| 12 | `gui_init()` | Scene graph, event bus, camera, renderer |
| 13 | `vga_init()` | Terminal texto no framebuffer |
| 14 | `init_mouse()` | PS/2 mouse |
| 15 | `pci_init()` | Enumeração PCI |
| 16 | `ahci_init()` / `ata_bmide_init()` | Drivers de disco |
| 17 | `net_init()` | Interface de rede |
| 18 | `tcp_init()` / `udp_init()` / `dns_init()` | Pilha TCP/IP |
| 19 | `usb_init()` | Subsistema USB |
| 20 | Load initrd | Parse do módulo GRUB |
| 21 | `rtl8139_init()` | Driver NIC (se detectado) |
| 22 | Mount SDFS | Disco persistente ou ramdisk fallback (64 MB) |
| 23 | `initrd_copy_to_sdfs()` | First-boot: copia initrd para SDFS |
| 24 | `init_timer(100)` | PIT 8253 a 100 Hz |
| 25 | `init_tasking()` | Scheduler round-robin |
| 26 | `init_syscalls()` | Tabela de syscalls |
| 27 | `usb_start_polling()` | Loop de polling USB |
| 28 | Cria tasks | `lgx_comp` (compositor) + `terminal` (shell) |
| 29 | `sti` + idle loop | Habilita interrupções, HLT em loop |

---

## 4. Gerenciamento de Memória

### 4.1 PMM — Physical Memory Manager (`src/kernel/pmm.c`)

- **Tipo:** Bitmap allocator (1 bit por página de 4 KB)
- **Inicialização:** Marca tudo como usado, depois libera regiões do memory map Multiboot2
- **Suporte:** >4 GB via endereços de 64 bits
- **API:** `pmm_init()`, `pmm_init_region()`, `pmm_alloc_block()`, `pmm_free_block()`
- **Estado:** Funcional, mas contagem de `pmm_used_blocks` pode ficar inconsistente
  se `pmm_free_block()` for chamado mais vezes que `pmm_alloc_block()`

### 4.2 VMM — Virtual Memory Manager (`src/kernel/vmm.c`)

- **Tipo:** Paginação 4 níveis x86_64 (PML4 → PDP → PD → PT)
- **Páginas:** 4 KB, com suporte a páginas grandes de 2 MB (split automático)
- **API:** `vmm_map_page()`, `vmm_copy_directory()`, `vmm_create_directory()`, `sys_brk()`
- **Diretórios por processo:** Sim — `vmm_copy_directory()` copia página por página (sem COW)
- **Problema:** `init_vmm()` identity-map TUDO até `memory_size` (pode ser >1 GB),
  o que causa ~250.000+ iterações. Mapeia também 16 MB de framebuffer em 0xFD000000.

### 4.3 Kernel Heap (`src/kernel/kheap.c`)

- **Tipo:** Bump allocator + free-list simples
- **API:** `kmalloc()`, `kmalloc_a()` (alinhado), `kmalloc_ap()` (alinhado + físico), `kfree()`
- **Estado:** Funcional. `kfree()` adiciona blocos à free-list para reuso.
  Sem coalescing de blocos adjacentes (fragmentação possível).
- **Header por alocação:** 8 bytes (`free_header_t`)

### 4.4 Layout de Memória Virtual

| Faixa | Uso |
|-------|-----|
| 0x00000000 – 0x0FFFFFFF | Identidade-map kernel (primeiros 256 MB) |
| 0x08048000 | Entry point de apps ELF |
| 0x10000000+ | Kernel higher-half |
| 0x40000000+ | User heap (via `sys_brk`) |
| 0xBFFFF000 | User stack (64 KB, cresce para baixo) |
| 0xFD000000 – 0xFDFFFFFF | Framebuffer (16 MB, write-combining) |

---

## 5. Sistema de Processos e Tarefas

### 5.1 Estrutura `task_t` (`include/task.h`)

Cada task contém:
- ID, estado (RUNNING/READY/SLEEPING/ZOMBIE)
- Kernel stack e user stack
- Diretório de página (page_directory_t)
- Heap start/end
- Current working directory
- 16 file descriptors (fd_table)
- Nome da task

### 5.2 Scheduler (`src/kernel/task.c`)

- **Tipo:** Round-robin preemptivo via PIT (IRQ0, 100 Hz)
- **Estrutura:** Lista circular duplamente ligada
- **Context switch:** PUSH_ALL/POP_ALL, troca de CR3, atualização TSS.rsp0
- **`switch_task()`:** Força `int $32` (IRQ0 software)
- **Ctrl+C:** `schedule()` verifica `check_ctrl_c()` e mata foreground task

### 5.3 Fork e Exec

| Função | Estado | Descrição |
|--------|--------|-----------|
| `fork_process()` | Funcional (syscall 14) | Copia page directory completo (sem COW), registros, heap, CWD |
| `create_user_task()` | Funcional | 32-bit compat mode (CS=0x1B, SS=0x23) |
| `create_user_task_64_named()` | Funcional | 64-bit user mode (CS=0x2B, SS=0x33) |
| `sys_execve()` | Funcional (syscall 15) | Carrega ELF32/64, monta stack com argc/argv |
| `sys_waitpid()` | Funcional (syscall 7) | Bloqueia pai, reap zombie, libera kernel stack e PCB |
| `sys_exit_process()` | Funcional (syscall 1) | Marca ZOMBIE, acorda pai, loop infinito |
| `sys_kill_by_pid()` | Funcional (syscall 28) | Marca user task como ZOMBIE |

### 5.4 Problemas Conhecidos

- **Sem COW:** `vmm_copy_directory()` copia todas as páginas fisicamente
- **Sem liberação de page directory:** `sys_waitpid()` tem TODO para liberar via `vmm_free_directory()`
- **Sem prioridades:** Pure round-robin, sem nice values
- **Sem SMP:** Single-core apenas
- **Sem sinais:** Apenas SIGKILL implementado (via `check_ctrl_c()`)

---

## 6. Chamadas de Sistema

**Mecanismo:** `int $0x80` (gate na IDT entry 128, ring-3 acessível com flag 0xEE)

### Tabela Completa de Syscalls (33 implementadas)

| # | Nome | Função Kernel | Estado |
|---|------|---------------|--------|
| 1 | `exit` | `sys_exit_process()` | Funcional |
| 2 | `brk` | `sys_brk()` | Funcional |
| 3 | `read` | `sys_read()` | Funcional (pipe, stdin com termios, files) |
| 4 | `write` | `sys_write()` | Funcional (stdout/stderr, files, pipe) |
| 5 | `open` | `sys_open()` | Funcional (O_CREAT, O_TRUNC, O_APPEND) |
| 6 | `close` | `sys_close()` | Funcional |
| 7 | `waitpid` | `sys_waitpid()` | Funcional |
| 8 | `getticks` | Retorna `timer_ticks` | Funcional |
| 10 | `keyboard_get_event` | Fila de eventos | Funcional |
| 11 | `keyboard_is_pressed` | Estado de tecla | Funcional |
| 14 | `fork` | `fork_process()` | Funcional |
| 15 | `execve` | `sys_execve()` | Funcional |
| 16 | `tcgetattr` | Atributos terminal | Funcional |
| 17 | `tcsetattr` | Configura terminal | Funcional |
| 18 | `ioctl` | TCGETS, TCSETS, TIOCGWINSZ | Funcional |
| 19 | `lseek` | Seek em arquivo | Funcional |
| 20 | `getpid` | PID atual | Funcional |
| 21 | `gettimeofday` | timer_ticks → sec/usec | Funcional |
| 22 | `stat` | Status de arquivo | Funcional |
| 23 | `fstat` | Status via fd | Funcional |
| 24 | `unlink` | Deleta arquivo | Funcional |
| 25 | `mkdir` | Cria diretório | Funcional |
| 26 | `chdir` | Muda diretório | Funcional |
| 27 | `getcwd` | Diretório atual | Funcional |
| 28 | `kill` | Mata processo | Funcional |
| 29 | `rmdir` | Remove diretório | Funcional |
| 30 | `getdents` | Lista diretório | Funcional |
| 31 | `pipe` | Cria pipe (4KB ring buffer) | Funcional |
| 32 | `dup` | Duplica fd | Funcional |
| 33 | `dup2` | Duplica fd para número específico | Funcional |
| 120 | `gui_create_window` | Cria canvas/janela | Funcional (usa float — viola -mno-sse) |
| 121 | `gui_get_buffer` | Obtém framebuffer | Funcional |
| 122 | `gui_refresh` | Atualiza janela | Funcional |
| 123 | `gui_node_move` | Move node | Funcional |
| 124 | `gui_camera_zoom` | Zoom câmera | Funcional |

**Gaps:** Números 9, 12, 13 não utilizados.

### FD Table

- Tabela fixa de 32 file descriptors em `syscall.c`
- fd 0 = stdin (bloqueia no teclado com suporte a termios)
- fd 1 = stdout (vga_putc + serial)
- fd 2 = stderr (mesmo que stdout)
- fd 3+ = arquivos, pipes, sockets

### libgloss Layer (`libgloss/syscalls.c`)

Bridge completo entre newlib e o kernel:
- Implementa: `open`, `close`, `read`, `write`, `lseek`, `fstat`, `stat`, `gettimeofday`, `sbrk`, `kill`, `getpid`, `exit`, `fork`, `execve`, `chdir`, `getcwd`, `mkdir`, `unlink`, `getdents`, `dup2`, `pipe`
- Stubs: `mmap` (retorna ENOMEM), `mprotect` (no-op), `dlopen/dlsym/dlclose`, `fcntl`, `sigaction`, `sysconf` (retorna 4096)

---

## 7. Sistema de Arquivos

### 7.1 VFS (`src/fs/vfs.c`)

- Mount-point tree com resolução de path "best-match"
- `fs_node_t` com 12 function pointers: read, write, open, close, readdir, finddir, create, truncate
- **Mount points:**
  - `/` → initrd (read-only)
  - `/house/localhost` → SDFS (read-write, disco persistente)
  - `/dev` → devfs

### 7.2 Initrd (`src/fs/initrd.c`)

- Formato TAR ustar (carregado como módulo GRUB)
- Parser de headers TAR com suporte a arquivos e diretórios
- Read-only, montado em `/`
- No primeiro boot, copia todo conteúdo para SDFS

### 7.3 SDFS — Simple Disk File System (`src/fs/sdfs.c`)

- **Sistema proprietário** para disco persistente
- Block size: 4096 bytes
- Alocação via bitmap de blocos
- Backends: ATA PIO, BM-IDE DMA, AHCI
- Ramdisk fallback (64 MB) quando disco não disponível
- API completa: create, delete, read, write, mkdir, rmdir, readdir
- First-boot: formata SDFS, copia initrd, cria flag `/.system_installed`
- Tamanho do código: ~40K (arquivo mais extenso do projeto)

### 7.4 FAT32 (`src/fs/fat32.c`)

- Leitura completa com traversal de cluster chain
- Suporte a nomes curtos 8.3
- Mount via `fat32_mount(bus, drive, lba_start)`
- **Write:** Implementado mas com bugs conhecidos (documentados no código)
- Suporte a formatação
- **VLA no kernel:** Usa `cluster_buffer[cluster_size]` — risco de stack overflow

### 7.5 DevFS (`src/fs/devfs.c`)

- `/dev/null`, `/dev/zero`, etc.
- Implementação básica

---

## 8. Drivers de Hardware

### 8.1 Drivers de Armazenamento

| Driver | Arquivo | Estado | Descrição |
|--------|---------|--------|-----------|
| ATA PIO | `ata.c` | Funcional | LBA28, buses primário/secundário, read/write sectors |
| ATA BM-IDE DMA | `ata.c` | Funcional | DMA via PIIX3, ~8 setores por chamada |
| AHCI | `ahci.c` | Funcional | SATA, command list + FIS |
| Initrd | `initrd.c` | Funcional | TAR parser, GRUB module |

### 8.2 Drivers de Entrada

| Driver | Arquivo | Estado | Descrição |
|--------|---------|--------|-----------|
| PS/2 Keyboard | `keyboard.c` | Funcional | Scancode set 1, ABNT2, modificadores, fila de eventos |
| PS/2 Mouse | `mouse.c` | Funcional | Movimento relativo, 3 botões, tracking global |
| USB HID | `usb_hid.c` | Parcial | Teclado e mouse USB via relatórios HID |
| PC Speaker | `pcspkr.c` | Funcional | Beep, melodias, notas musicais C4-B6 |

### 8.3 Drivers de Display

| Driver | Arquivo | Estado | Descrição |
|--------|---------|--------|-----------|
| VGA Text | `vga.c` | Funcional | Modo texto 80x25 + ANSI escape sequences |
| Framebuffer | `vga.c` | Funcional | Linear framebuffer, blit de texto, PSF font |
| BGA/GPU | `gpu.c` | Funcional | Bochs Graphics Adaptor, MTRR write-combining |
| EDID | `edid.c` | Funcional | Parser de informações do monitor |

### 8.4 Drivers de Rede

| Driver | Arquivo | Estado | Descrição |
|--------|---------|--------|-----------|
| RTL8139 | `rtl8139.c` | Funcional | PCI detection, MMIO, TX (4 descritores), RX ring, IRQ 11 |

### 8.5 Drivers de Bus

| Driver | Arquivo | Estado | Descrição |
|--------|---------|--------|-----------|
| PCI | `pci.c` | Funcional | Enumeração 256 buses, config space read/write |
| USB Core | `usb.c` | Parcial | Enumeração USB, device registration, polling |
| UHCI | `uhci.c` | Parcial | USB 1.1 host controller |
| EHCI | `ehci.c` | Parcial | USB 2.0 host controller |

### 8.6 Serial

| Driver | Arquivo | Estado | Descrição |
|--------|---------|--------|-----------|
| COM1 | `serial.c` | Funcional | 115200 baud, TX/RX, usado para debug |

---

## 9. Pilha de Rede

### 9.1 Implementação (`src/net/`)

| Módulo | Arquivo | Estado | Descrição |
|--------|---------|--------|-----------|
| Interface | `net.c` | Funcional | Registro de interfaces, linked list |
| Ethernet/ARP/IPv4/ICMP | `netstack.c` | Funcional | Quadros, resolução MAC, roteamento, ping |
| TCP | `tcp.c` | Parcial | Handshake SYN/ACK, estados, sem retransmissão |
| UDP | `udp.c` | Funcional | Envio/recebimento básico |
| DHCP | `dhcp.c` | Parcial | DISCOVER/OFFER, sem REQUEST formal |
| DNS | `dns.c` | Funcional | Resolução síncrona, sem cache |
| HTTP | `http.c` | Experimental | GET client, tudo no kernel |

### 9.2 Problemas Conhecidos

- **TCP:** Buffer linear sem wrap, sem retransmissão, sem controle de fluxo,
  sem RTT, sem fragmentação IP, window size fixo em 8192
- **DHCP:** Não faz REQUEST formal (DISCOVER → OFFER → IP direto)
- **DNS:** Síncrono e bloqueante, sem cache
- **HTTP:** Implementado no kernel — bug = crash do kernel
- **Memory leaks:** `tcp_send`/`udp_send` usam `kmalloc`/`kfree` mas o `kfree`
  antigo era stub (corrigido na cleanup 2026)

---

## 10. Interface Gráfica e Compositor

### 10.1 Arquitetura do LGX (`src/kernel/gui/`)

O LGX (Liwus Graphics eXtension) é um **compositor com scene graph** rodando
como kernel task. É o subsistema mais elaborado do projeto (~28 arquivos).

**Ordem de inicialização** (`gui_init()`):

1. `scene_graph_init()` — Árvore de cenas com dirty flags
2. `app_registry_init()` — Registro de apps gráficos
3. `theme_engine_init()` — Paleta de cores dark glassmorphism
4. `animation_engine_init()` — Tweening linear
5. `event_bus_create()` — Ring buffer com 64 subscribers
6. `input_manager_create()` — Polling de mouse/teclado
7. `camera_create()` — Pan/zoom com aritmética de ponto fixo
8. `fb_renderer_create()` — Renderer de framebuffer
9. Montagem da Scene (canvas root + apps)
10. `focus_manager_create()` — Keyboard focus
11. `window_manager_create()` — Z-order, bring-to-front
12. `tool_manager_create()` — Pan/Select/Move tools
13. `compositor_create()` — Frame loop principal

### 10.2 Módulos

| Módulo | Arquivos | Descrição |
|--------|----------|-----------|
| Scene Graph | `scene/node.c`, `scene/scene.c` | Árvore com dirty flags, hit-testing, transform accumulation |
| Camera | `scene/camera.c` | Fixed-point pan/zoom/inertia, world↔screen conversion |
| Compositor | `render/compositor.c` | Frame loop: input→events→camera→transforms→draw→present |
| Renderer | `render/renderer.c`, `render/fb_renderer.c` | Abstract vtable + software backend, alpha blending |
| Event Bus | `core/event_bus.c` | Ring buffer, 64 subscribers, prioridades, propagação |
| Input Manager | `input/input_manager.c` | Polling mouse/teclado, posta eventos tipados |
| Theme Engine | `core/theme_engine.c` | 12 cores, paleta dark glassmorphism |
| Animation | `core/animation_engine.c` | Linear tween para x/y/w/h/opacity, 64 slots |
| Layout | `layout/layout_engine.c` | VBOX/HBOX com flex weight, alignment |
| Tools | `input/tools/` | Pan (WASD), Select (LMB), Move (titlebar drag) |
| Window Manager | `window/window_manager.c` | Z-order, bring-to-front, close |
| Focus Manager | `window/focus_manager.c` | Keyboard focus tracking |
| App Registry | `core/app_registry.c` | Navegação por setas |
| Asset Manager | `assets/asset_manager.c` | PSF font loading |

### 10.3 Widgets

| Widget | Arquivo | Estado |
|--------|---------|--------|
| Window Node | `widgets/window_node.c` | Funcional — titlebar, close, drag, callbacks |
| Button | `widgets/button.c` | Funcional — hover/press animation, onclick |
| Label | `widgets/label.c` | Funcional — text rendering via glyphs |
| Panel | `widgets/panel.c` | Funcional — container com bg/border |

### 10.4 Apps GUI do Kernel

| App | Arquivo | Descrição |
|-----|---------|-----------|
| GUI Terminal | `apps/gui_terminal.c` | Emulador 80×24, cursor, scroll, execução de comandos |
| Settings | `apps/gui_settings.c` | System info, Display (EDID), Sound (PC speaker), Network |
| File Explorer | `apps/gui_explorer.c` | Navegador de arquivos |
| Editor | `apps/gui_editor.c` | Editor de texto GUI |
| Calculator | `apps/gui_calculator.c` | Calculadora |
| About | `apps/gui_about.c` | Diálogo "Sobre" |

### 10.5 Características do Compositor

- Canvas Infinito com dot-grid de fundo
- Zoom In/Out (teclas +/-)
- Pan (arrastar fundo)
- Return to Home (H) / Fit to Screen (F)
- Radar HUD (minimap no canto inferior direito)
- Off-screen edge indicators (indicadores âmbar)
- Menu Radial no clique direito
- Boot animation
- Wallpaper (1024x768, gerado por script Python)

---

## 11. API Gráfica de Usuário

### 11.1 SDK Legacy (`libliw.h`)

- `liw_fb_info_t` — info do framebuffer
- `liw_get_fb_info()` / `liw_present_fb()` — renderização direta
- `liw_draw_pixel()` — desenha pixel
- Syscalls via `int $0x80` (12-13)

### 11.2 SDK Scene Graph (`liwus_gui.h`)

- `canvas_create()` / `text_create()` / `button_create()` / `panel_create()`
- `canvas_add()` / `node_add_child()` / `node_move()` / `camera_zoom()`
- Syscalls via `syscall` instruction (120-124)
- **Problema:** GUI syscalls usam `float` no kernel, violando `-mno-sse`

---

## 12. Terminal e Shell

### 12.1 Terminal Kernel (`src/kernel/terminal/`)

**Arquitetura modulada (refatorado):**
- `terminal.c` — Loop principal, leitura de teclado, execução
- `parser.c` — Tokenização de comandos
- `dispatcher.c` — Tabela de dispatch
- `commands.c` — Comandos built-in

**Comandos built-in (~30+):**
`help`, `clear`, `echo`, `ls`, `cd`, `pwd`, `cat`, `mkdir`, `rmdir`, `rm`,
`cp`, `mv`, `touch`, `edit`, `top`, `free`, `df`, `uptime`, `uname`, `whoami`,
`ifconfig`, `ping`, `wget`, `host`, `lua`, `exec`, `lsw`, `format`, `mount`,
`tasklist`, `reboot`, `version`, `meminfo`, `diskinfo`, `ip`, `neofetch`

**Recursos:**
- TAB autocomplete (integração com VFS)
- ANSI escape sequences
- Execução de programas ELF diretamente pelo nome
- Comandos desconhecidos tentam executar como programas

---

## 13. Aplicativos Incluídos

### 13.1 Apps de Usuário (ring-3, ELF)

| App | Arquivo | Descrição |
|-----|---------|-----------|
| hello | `apps/hello/` | Hello World de teste |
| calc | `apps/calc/calc.c` | Calculadora gráfica (usa SDK Scene Graph) |
| doomgeneric | `apps/doomgeneric/` | Port de Doom (doomgeneric backend) |
| doomprobe | `apps/doomprobe/` | Probe de framebuffer gráfico |
| lua | `apps/lua/` | Lua 5.4 interpretador |
| tcc | `apps/tcc/` | Tiny C Compiler (compila C dentro do OS) |
| kilo | `apps/kilo/` | Kilo editor (nano-like) |
| editor_nano | `apps/editor_nano/` | Editor GUI nano-like |
| demo_gui | `apps/demo_gui/` | Demo da API Scene Graph |
| view | `apps/view/` | Visualizador de imagens PNG/JPEG |
| c4/crun | `apps/c4/` | C4 — compilador C em 4 funções |
| test_open | `apps/test_open.c` | Teste mínimo de I/O |

### 13.2 Apps Kernel (ring-0, tasks)

| App | Arquivo | Descrição |
|-----|---------|-----------|
| Terminal | `src/kernel/terminal/` | Shell completo |
| GUI Terminal | `src/kernel/gui/apps/gui_terminal.c` | Terminal gráfico |
| Settings | `src/kernel/gui/apps/gui_settings.c` | Painel de configurações |
| Explorer | `src/kernel/gui/apps/gui_explorer.c` | Navegador de arquivos |
| Editor | `src/kernel/gui/apps/gui_editor.c` | Editor de texto GUI |
| Calculator | `src/kernel/gui/apps/gui_calculator.c` | Calculadora GUI |
| About | `src/kernel/gui/apps/gui_about.c` | Diálogo "Sobre" |

---

## 14. Bibliotecas Internas e de Terceiros

### 14.1 Bibliotecas do Kernel

| Biblioteca | Local | Descrição |
|------------|-------|-----------|
| string.c | `src/kernel/string.c` | libc strings (strlen, strcmp, memcpy, memset, itoa, etc.) |
| fast_memcpy | `src/kernel/fast_memcpy.s` | SSE2-optimized memcpy (non-temporal stores para VRAM) |

### 14.2 SDK (pré-compilada)

| Biblioteca | Arquivo | Descrição |
|------------|---------|-----------|
| libc.a | `sdk/lib/libc.a` | Newlib C standard library |
| libm.a | `sdk/lib/libm.a` | Newlib math library |
| libgloss.a | `sdk/lib/libgloss.a` | LiwusOS syscall layer |
| libz.a | `sdk/lib/libz.a` | zlib compression |
| libpng.a | `sdk/lib/libpng.a` | PNG image support |
| libjpeg.a | `sdk/lib/libjpeg.a` | JPEG image support |
| libliwus_gui.a | `sdk/lib/libliwus_gui.a` | GUI widget library |
| liblgx.a | `sdk/lib/liblgx.a` | LGX graphics library |

### 14.3 Third-Party (`third_party/`)

| Biblioteca | Versão | Descrição |
|------------|--------|-----------|
| Lua | 5.4.6 | Interpretador de scripting |
| TCC | — | Tiny C Compiler |
| doomgeneric | — | Doom source port |
| zlib | — | Compression library |
| libpng | — | PNG support |
| libjpeg | — | JPEG support |

### 14.4 Ferramentas SDK (`sdk/tools/`)

| Ferramenta | Linguagem | Descrição |
|------------|-----------|-----------|
| liw-builder | C | Empacotador de LIW (ELF + manifest + resources) |
| img-gen | C | Gerador de imagens de teste |
| img2c.py | Python | Conversor BMP → C header |
| gen_wallpaper.py | Python | Gerador de wallpaper gradient |
| gen_ui_assets.py | Python | Gerador de assets UI (botoes, icones, etc.) |
| convert_wallpaper.py | Python | Conversor de imagens para wallpaper |

### 14.5 Formato LIW (`include/liw_format.h`)

```c
typedef struct {
    uint32_t magic;            // 0x5845574C ("LWEX")
    uint32_t version;          // 1
    uint32_t flags;            // 0
    uint32_t entry_offset;     // ELF offset
    uint32_t entry_size;       // ELF size
    uint32_t manifest_offset;  // JSON manifest offset
    uint32_t manifest_size;    // JSON manifest size
    uint32_t resources_offset; // Resources bundle offset
    uint32_t resources_size;   // Resources bundle size
    uint8_t padding[32];       // Reserved
} liw_header_t;
```

---

## 15. Sistema de Build e Toolchain

### 15.1 Docker Build (`Dockerfile`)

- Base: `debian:bookworm`
- Cross-compiler: `i686-elf-gcc` (compilado do fonte)
  - Binutils 2.42
  - GCC 13.2.0 (C only, freestanding)
- Ferramentas: NASM, GRUB tools, xorriso, mtools, QEMU

### 15.2 Makefile (230 linhas)

**Targets principais:**
- `all` — Build completo (libs, apps, ISO)
- `kernel.bin` — Linkage do kernel com `linker.ld`
- `liwusos.iso` — ISO bootável via GRUB-mkrescue
- `zlib`, `libpng`, `libjpeg` — Cross-compilação das libs
- `run` / `run-serial` / `run-log` — QEMU launch

**Flags do kernel:**
```
-std=gnu99 -ffreestanding -O2 -Wall -Wextra
-m64 -mno-red-zone -mcmodel=large -mno-sse -mno-sse2 -mno-mmx
-fno-pie -fno-pic
```

**Flags de user-space:**
```
-std=gnu99 -ffreestanding -O2 -Wall -Wextra
-m64 -mno-red-zone -fno-pie -fno-pic
-nostdlib -static
```

### 15.3 Toolchain Customizada (`toolchain/`)

Script `build-x86_64-liwusos-toolchain.sh` (164 linhas):
- Patches config.sub para reconhecer `liwusos*`
- Cria camada syscall newlib (`newlib/libc/sys/liwus/`)
- Cria GCC machine config (`gcc/config/liwusos.h`)
- 5 estágios: Binutils → GCC stage1 → Newlib → libgloss → GCC final
- Target: `x86_64-liwusos`
- Prefix: `/opt/liwusos-toolchain`

### 15.4 Scripts Auxiliares

| Script | Descrição |
|--------|-----------|
| `build.sh` | Build via Docker |
| `build_app.sh` | Compila app userland individual |
| `run.sh` | Build + run em QEMU |
| `clean.sh` | Limpa artefatos de build |

### 15.5 QEMU Flags

```bash
qemu-system-x86_64 \
  -m 512M \
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

## 16. Compatibilidade de Hardware

| Hardware | Compatibilidade | Notas |
|----------|-----------------|-------|
| CPU x86_64 | Requerido | Long mode (64-bit) |
| QEMU | Primário | Target oficial de desenvolvimento |
| Bochs | Parcial | BGA/VBE framebuffer |
| VBox | Parcial | Ports QEMU/VBox para shutdown |
| RAM | 512 MB recomendado | Mínimo funcional com 128 MB |
| Disco | IDE/AHCI/SATA | ATA PIO, BM-IDE DMA, AHCI |
| Rede | RTL8139 | Driver funcional |
| Teclado | PS/2 + USB HID | ABNT2, scancode set 1 |
| Mouse | PS/2 + USB HID | Relativo, 3 botões |
| Monitor | Qualquer | VGA text + framebuffer |
| GPU | VGA std (QEMU) | Sem aceleração 3D |
| USB | UHCI/EHCI | Parcial (sem hubs, sem hot-plug) |
| WiFi | Não suportado | Stub removido |
| Áudio | PC speaker apenas | Sem driver de áudio real |
| SMP | Não suportado | Single-core |

---

## 17. Inconsistências e Problemas Encontrados

### 17.1 Documentação vs Realidade

| Item | README Diz | Realidade no Código |
|------|------------|---------------------|
| VirtIO | "VirtIO" listado como driver | Stub removido na cleanup 2026 |
| Serial baud | "38400 baud" | Código inicializa com 115200 baud |
| GPU driver | "VirtIO GPU init" | Na verdade é BGA/VBE (Bochs Graphics Adaptor) |
| TCP | "em progresso" | Implementado mas com bugs severos (buffer sem wrap) |
| Apps listadas | Inclui "browser", "installer" | Removidos na cleanup 2026 (código morto) |
| LGX descrito como "Vulkan-like" | No README anterior | Falso — é framebuffer com alpha blending |
| 33 syscalls | README diz "20+" | São 33 (incluindo GUI) — README subestima |

### 17.2 Headers Ausentes

- `gui.h` referenciado em `editor.h`, `explorer.h`, `launcher.h`, `settings.h`
  mas não existe. A GUI foi reestruturada para `gui_main.h` mas headers antigos
  não foram atualizados. Estes headers são usados apenas por apps removidos.

### 17.3 Float no Kernel

- GUI syscalls 120-124 usam `float` casts em `syscall.c`
- `camera.c` usa aritmética de ponto fixo corretamente (`CAMERA_ZOOM_SCALE=1024`)
- Mas a transição float↔fixed nos syscalls viola `-mno-sse`

### 17.4 Multiboot Inconsistência

- `boot.s` usa header Multiboot2 (`0xe85250d6`)
- `multiboot.h` define estrutura Multiboot1 (`multiboot_info`)
- Funciona por modo de compatibilidade do GRUB2

### 17.5 Monolitismo Extremo

- Terminal, compositor, launcher, editor, settings, explorer, calculator —
  todos rodam em ring-0 (kernel tasks)
- O kernel TEM suporte a ring-3 (`create_user_task`, `fork_process`, `sys_execve`)
  mas as apps do sistema não usam isso

### 17.6 Nomenclatura Mista

- Comentários e mensagens misturam português e inglês
- Ex: `"FAT32: Falha ao ler o setor de boot."` vs `"Filesystem nao e FAT32."`

---

## 18. Débitos Técnicos

| # | Débito | Gravidade | Descrição |
|---|--------|-----------|-----------|
| 1 | Apps em ring-0 | Crítico | Apps do sistema (terminal, settings, etc.) rodam em kernel mode |
| 2 | Sem COW no fork | Alto | `vmm_copy_directory()` copia todas as páginas fisicamente |
| 3 | TCP buffer linear | Alto | Sem circular buffer, sem wrap, sem retransmissão |
| 4 | Sem liberação de page dir | Alto | `sys_waitpid()` não libera page directory do zombie |
| 5 | FD table fixa (32) | Médio | Sem growth dinâmico, limita apps complexos |
| 6 | Sem prioridades de scheduler | Médio | Round-robin puro, sem nice/preemption priority |
| 7 | VMM identity-map excessivo | Médio | `init_vmm()` mapeia tudo até memory_size |
| 8 | HTTP no kernel | Médio | Bug no parser = crash do kernel |
| 9 | USB enumeração hardcodada | Médio | Chama EHCI sem verificar tipo de controlador |
| 10 | Float no kernel (GUI syscalls) | Médio | Viola `-mno-sse` |
| 11 | FAT32 write bugado | Médio | Write functions existem mas são instáveis |
| 12 | VLA no kernel (FAT32) | Médio | `cluster_buffer[cluster_size]` — stack overflow risk |
| 13 | Sem SMP | Baixo | Single-core, sem suporte a múltiplos cores |
| 14 | Sem áudio | Baixo | Apenas PC speaker, sem driver de áudio real |
| 15 | Sem sinais POSIX | Baixo | Apenas SIGKILL, sem handlers |
| 16 | Sem validação de ponteiros | Alto | Syscalls não validam endereços de user space |
| 17 | Nomenclatura mista | Baixo | Português/Inglês混在 |

---

## 19. Diagrama de Arquitetura

```
┌──────────────────────────────────────────────────────────────────┐
│                        GRUB2 (Multiboot2)                        │
│                   kernel.bin + initrd.tar + font.psf             │
└──────────────────────────┬───────────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────────┐
│                      boot.s (Assembly)                           │
│  PML4 → PAE → Long Mode → entry_64 → kernel_main()             │
└──────────────────────────┬───────────────────────────────────────┘
                           │
┌──────────────────────────▼───────────────────────────────────────┐
│                     kernel_main (C)                               │
│                                                                  │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌──────────┐  │
│  │    GDT      │ │    IDT      │ │    PMM      │ │   VMM    │  │
│  │ 9 entries   │ │ 256 entries │ │ Bitmap      │ │ 4-level  │  │
│  │ + TSS       │ │ PIC remap   │ │ alloc/free  │ │ paging   │  │
│  └─────────────┘ └─────────────┘ └─────────────┘ └──────────┘  │
│                                                                  │
│  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌──────────┐  │
│  │   kheap     │ │   Timer     │ │   Tasking   │ │ Syscalls │  │
│  │ free-list   │ │ PIT 100Hz   │ │ round-robin │ │ int $80  │  │
│  │ kmalloc/kf  │ │ ticks       │ │ fork/exec   │ │ 33 calls │  │
│  └─────────────┘ └─────────────┘ └─────────────┘ └──────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                    Drivers                                │   │
│  │  ATA PIO │ ATA DMA │ AHCI │ PCI │ PS/2 │ USB │ RTL8139  │   │
│  │  Serial  │ VGA/FB  │ BGA  │ EDID│ PCSPKR│ UHCI│ EHCI    │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                  Filesystems                              │   │
│  │  VFS ─┬─ initrd (/, read-only)                           │   │
│  │       ├─ SDFS (/house/localhost, read-write)              │   │
│  │       ├─ FAT32 (read, write buggy)                       │   │
│  │       └─ devfs (/dev)                                    │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                  Networking                               │   │
│  │  RTL8139 → Ethernet → ARP → IPv4 → ICMP │ TCP │ UDP    │   │
│  │                                        │ DNS │ DHCP │HTTP│   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              LGX Compositor (kernel task)                 │   │
│  │  Scene Graph → Event Bus → Camera → Layout → Renderer    │   │
│  │  Widgets: Window, Button, Label, Panel                   │   │
│  │  Apps: Terminal, Settings, Explorer, Editor, Calc, About │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │              Terminal (kernel task)                        │   │
│  │  Shell: ~30 built-in commands, TAB autocomplete          │   │
│  │  Parser → Dispatcher → Commands                           │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
└──────────────────────────┬───────────────────────────────────────┘
                           │ int $0x80
┌──────────────────────────▼───────────────────────────────────────┐
│                   Ring 3 — User Space                            │
│  crt0.S → main() → _exit()                                     │
│  libgloss/syscalls.c (newlib bridge)                            │
│  newlib (libc.a, libm.a)                                        │
│  Apps: Doom │ Lua 5.4 │ TCC │ Calc │ View │ Kilo │ demo_gui   │
│  SDK: libliw.h │ liwus_gui.h │ libz │ libpng │ libjpeg         │
└──────────────────────────────────────────────────────────────────┘
```

---

## 20. Estado de Maturidade

### Classificação: **Hobby OS — Estágio Pre-Alpha Avançado**

| Aspecto | Maturidade | Nota |
|---------|------------|------|
| Boot | Estável | GRUB Multiboot2, long mode, funciona em QEMU |
| Kernel Core | Funcional | GDT/IDT/PMM/VMM/heap — funciona mas com limitações |
| Scheduler | Funcional | Round-robin preemptivo, sem prioridades |
| Syscalls | Funcional | 33 syscalls, cobre POSIX básico |
| Filesystem | Funcional | SDFS persistente + FAT32 read + initrd |
| Drivers | Funcional | ATA, PCI, PS/2, RTL8139 —USB parcial |
| Networking | Parcial | ARP/ICMP funcional, TCP/UDP instáveis |
| GUI | Funcional | Compositor com scene graph, widgets, apps |
| SDK | Funcional | newlib + libs gráficas + ferramentas |
| Apps | Funcional | Doom, Lua, TCC funcionam em ring-3 |
| Segurança | Fraca | Tudo em ring-0, sem validação de ponteiros |
| Estabilidade | Instável | Crashes possíveis em apps kernel |

### O que FUNCIONA de ponta a ponta:

1. Boot → long mode → kernel init → VFS → SDFS mount → first-boot install
2. Timer-driven round-robin scheduler com fork/exec/waitpid
3. Terminal com ~30 comandos built-in
4. GUI compositor com scene graph, camera, event bus, widgets
5. GUI terminal 80×24 dentro do compositor
6. Settings com system info e EDID
7. User-space ELF loading via crt0 → main → _exit
8. Doom roda com renderização via syscall
9. Lua 5.4 interpreta scripts
10. TCC compila C dentro do OS
11. Ping funcional via RTL8139

---

## 21. Sugestões de Melhoria

### Prioridade Crítica

1. **Migrar apps do sistema para ring-3:** Terminal, settings, explorer, editor
   devem rodar como user-space ELF, não kernel tasks
2. **Adicionar validação de ponteiros em syscalls:** Verificar que buffers de user
   space não apontam para memória do kernel
3. **Implementar circular buffer no TCP:** Substituir buffer linear por ring buffer
   adequado

### Prioridade Alta

4. **Implementar COW no fork:** `vmm_copy_directory()` deve marcar páginas como
   read-only e copiar apenas na escrita
5. **Liberar page directory no exit:** Completar o TODO em `sys_waitpid()`
6. **Remover identity-map excessivo:** `init_vmm()` deve mapear apenas o necessário
7. **Corrigir float no kernel:** Mover cálculos de ponto flutuante para integer nos
   GUI syscalls

### Prioridade Média

8. **Expandir FD table:** De 32 para dinâmico ou pelo menos 256
9. **Implementar prioridades no scheduler:** Nice values ou priority classes
10. **Corrigir FAT32 write:** Resolver bugs na alocação de clusters
11. **Corrigir VLA no FAT32:** Usar kmalloc em vez de VLA para cluster_buffer
12. **Corrigir USB enumeração:** Verificar tipo de controlador antes de chamar

### Prioridade Baixa

13. **Padronizar idioma:** Escolher inglês ou português para comentários/mensagens
14. **Remover dead code:** Limpar headers órfãos e código não utilizado
15. **SMP:** Considerar suporte a múltiplos cores (futuro)
16. **Áudio:** Implementar driver de áudio (AC97 ou HDA)

---

*Relatório gerado em Julho de 2026 por auditoria completa do código-fonte.*
*Baseado exclusivamente no código implementado, não em documentação histórica.*
