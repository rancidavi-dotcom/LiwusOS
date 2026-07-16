# ANÁLISE CRÍTICA COMPLETA DO LIWUSOS

> **Aviso**: Esta é uma análise brutalmente honesta de todos os problemas, deficiências,
> inconsistências e decisões de design questionáveis neste projeto. Nada aqui é pessoal
> — o objetivo é melhorar o código.

---

## ÍNDICE

1. [Problemas Arquiteturais Graves](#1-problemas-arquiteturais-graves)
2. [Sistema de Memória Quebrado](#2-sistema-de-memória-quebrado)
3. [VFS — Virtual File System Incompleto](#3-vfs--virtual-file-system-incompleto)
4. [Rede — Pilha TCP/IP Fake/Inconsistente](#4-rede--pilha-tcpip-fakeinconsistente)
5. [Drivers — Problemas e Stubs Remanescentes](#5-drivers--problemas-e-stubs-remanescentes)
6. [Compositor/GUI — Overengineering Extremo](#6-compositorgui--overengineering-extremo)
7. [Syscalls — Tabela Incompleta e Inconsistente](#7-syscalls--tabela-incompleta-e-inconsistente)
8. [Terminal — Monstro Monolítico de 2100+ Linhas](#8-terminal--monstro-monolítico-de-2100-linhas)
9. [Task/Scheduling — Problemas Graves](#9-taskscheduling--problemas-graves)
10. [Segurança — Zero](#10-segurança--zero)
11. [Build System — Overcomplicated](#11-build-system--overcomplicated)
12. [USB Stack — Incompleta e Perigosa](#12-usb-stack--incompleta-e-perigosa)
13. [FAT32 — Write Bugado](#13-fat32--write-bugado)
14. [Bugs Específicos no Código](#14-bugs-específicos-no-código)
15. [Problemas de Nomenclatura e Organização](#15-problemas-de-nomenclatura-e-organização)
16. [Documentação vs Realidade](#16-documentação-vs-realidade)
17. [Resumo Final](#17-resumo-final)
18. [Mudanças Realizadas (Cleanup 2026)](#18-mudanças-realizadas-cleanup-2026)

---

## 1. Problemas Arquiteturais Graves

### 1.1 Monólito Extremo

**Tudo roda em ring 0 (kernel mode)**: terminal, compositor, launcher, editor de texto,
calculadora, explorador de arquivos, settings, instalador, desinstalador, leitor de livros,
navegador web, etc.

```
src/apps/terminal.c     → kernel task (ring 0)
src/apps/launcher.c     → kernel task (ring 0)
src/apps/editor.c       → kernel task (ring 0)
src/apps/browser.c      → kernel task (ring 0)
src/apps/calculator.c   → kernel task (ring 0)
src/apps/settings.c     → kernel task (ring 0)
src/apps/installer.c    → kernel task (ring 0)
...
```

Isso significa que QUALQUER bug nessas aplicações derruba o sistema inteiro. Não há
proteção de memória entre elas.

O sistema TEM suporte a user mode (ring 3) com `create_user_task()`, `fork_process()`,
e `sys_execve()`, mas as APPS PRÓPRIAS DO SISTEMA não usam isso — são todas kernel tasks.

### 1.2 "Wayland Client Simulado no Kernel"

Comentário no terminal.c:
```c
/* Terminal agora é um Wayland Client (simulado no Kernel) */
```

Não há Wayland. Não há protocolo Wayland real. É tudo struct em memória compartilhada
entre kernel tasks.

### 1.3 Inconsistência Kernel/Userspace

- `fork_process()` existe em `task.c` mas **não tem syscall number** — não pode ser
  chamado por programas de user mode
- `sys_execve()` existe mas **não está no switch/case do syscall_handler** — outro furo
- Programas ELF (hello, lua, doom) usam syscalls, mas as apps do sistema (terminal,
  launcher, etc.) chamam funções do kernel **diretamente** como se fossem libc

---

## 2. Sistema de Memória Quebrado

### 2.1 Bump Allocator — Agora com Free List

`kheap.c` foi reescrito com um allocator baseado em free list:

```c
typedef struct free_header {
    uint32_t size;
    struct free_header *next;
} free_header_t;

void *kmalloc(size_t size) {
    // Procura na free list primeiro
    while (curr) {
        if (curr->size >= total) {
            // Reuso do bloco
            return (void*)((uint32_t)curr + HEADER_SIZE);
        }
    }
    // Fallback: bump allocator
    header->size = total;
    kheap_current += total;
    return (void*)((uint32_t)header + HEADER_SIZE);
}

void kfree(void *ptr) {
    // Adiciona à free list
    header->next = free_list;
    free_list = header;
    // Se for page-aligned, libera PMM também
}
```

Mudanças:
- `kfree` agora adiciona o bloco à free list em vez de ignorar
- `kmalloc` busca na free list antes de alocar novo espaço
- Blocos page-aligned (≥4096) também liberam as páginas físicas via `pmm_free_block`
- Cada alocação tem um header `free_header_t` de 8 bytes antes do ponteiro retornado
- Ainda sem coalescing de blocos adjacentes — fragmentação é possível, mas o
  reuso imediato de blocos recentemente liberados funciona

### 2.2 PMM — Contagem de Blocos Errada

```c
void pmm_init(uint32_t start_addr, uint32_t size) {
    pmm_used_blocks = pmm_max_blocks;           // Começa em total
    for (uint32_t i = 0; i < pmm_max_blocks / 32; i++)
        pmm_bitmap[i] = 0xFFFFFFFF;              // Tudo ocupado
}

void pmm_init_region(uint32_t base, uint32_t size) {
    for (; blocks > 0; blocks--) {
        bitmap_unset(align++);
        pmm_used_blocks--;                        // Decrementa (libertando)
    }
}

void pmm_free_block(void* addr) {
    bitmap_unset(block);
    pmm_used_blocks--;                            // Decrementa DE NOVO
}
```

O problema: `pmm_used_blocks` começa em `pmm_max_blocks`. Cada `pmm_init_region`
decrementa (libera N blocos). Cada `pmm_alloc_block` incrementa. Cada `pmm_free_block`
decrementa. Até aí ok se balanceado.

Mas `pmm_get_used_memory` retorna `pmm_used_blocks * BLOCK_SIZE` — e essa contagem
pode ficar negativa ou inconsistente se `pmm_free_block` for chamado mais vezes que
`pmm_alloc_block`.

### 2.3 VMM — Mapeamento Massivo em init_vmm

```c
void init_vmm(uint32_t memory_size) {
    for (uint32_t i = 0; i < memory_size; i += 4096) {
        // identity-map TUDO — até 4GB
    }
    // Depois mapeia 16MB de framebuffer
    for (int i = 0; i < 16 * 1024 * 1024; i += 4096) {
        // mais 4096 páginas
    }
}
```

Com `memory_size = 4GB` (4294967296 bytes), são **1.048.576 iterações** no primeiro
loop. Cada iteração aloca page tables, memset, etc. Isso é extremamente lento e
desnecessário — a maioria dos sistemas mapeia APENAS o que precisa.

### 2.4 sys_brk Bug

```c
uint32_t sys_brk(uint32_t addr) {
    if (addr == 0 || addr < current_task->heap_start) return current_task->heap_end;
    if (addr > current_task->heap_end) {
        /* ... mapeia páginas ... */
        current_task->heap_end = end;   // deveria ser 'addr'
    }
    return current_task->heap_end;
}
```

Na linha 146: `current_task->heap_end = end;` — deveria ser `current_task->heap_end = addr`.
(end é a variável global do linker, não o endereço solicitado). Inconsistente.

---

## 3. VFS — Virtual File System Incompleto

### 3.1 O PLANO vs Realidade

`PLANO_VFS.md` descreve um VFS sofisticado com:
- Mount points em `/bin/`, `/house/localhost/`, `/dev/`
- Path resolution hierárquico
- Initrd montado em `/` (read-only), SDFS em `/house/` (read-write)

O código real em `src/fs/vfs.c` (121 linhas) IMPLEMENTA parcialmente isso, mas:
- `vfs_open()` resolve paths procurando o mount point mais específico e navegando
  com `finddir_fs` — mas a resolução é frágil
- `fs_root` global é declarado mas **nunca atribuído** a lugar nenhum
- A função `vfs_open` retorna NULL se não achar mount point — sem fallback
- Não há suporte a `..`, `.`, paths relativos, symlinks, etc.

### 3.2 Inconsistências

- Initrd é montado em `/` via `vfs_mount("/", initrd_root);` (kernel.c:208)
- SDFS é montado em `/house/localhost` (kernel.c:235)
- Mas o terminal usa `terminal_cwd = "/house/localhost"` hardcoded

---

## 4. Rede — Pilha TCP/IP Fake/Inconsistente

### 4.1 TCP — Implementação com Múltiplos Problemas

**`tcp.c` (358 linhas):**

- `send_tcp_packet` aloca com `kmalloc`, envia, e chama `kfree` — mas `kfree` é
  **stub vazio**, então vaza memória a CADA pacote enviado
- Buffer de recebimento usa `recv_write_ptr` e `recv_read_ptr` como **índices
  lineares crescentes** — nunca wrappeam, só resetam quando write == read
- Se `recv_write_ptr` chegar no fim do buffer (32768 bytes), wrap não acontece
  — o código tenta copiar no fim e se não couber, verifica se `current_usage == 0`
  e reseta. Mas se houver dados não lidos e o buffer encostar no fim, o pacote
  é **dropado silenciosamente**
- **Não há retransmissão** real — apenas timeout de 1000 ticks para o handshake
- **Não há controle de fluxo** — window size é fixo em 8192
- **Não há cálculo de RTT**
- **Não há fragmentação IP** — pacotes > 1400 bytes são descartados
  (`if (len > 1400) return; // Fragment not supported`)
- **Não há suporte a TCP options** (MSS, window scaling, SACK, etc.)
- Uma única thread/task lida com todos os pacotes TCP — sem escalabilidade
- O `tcp_listen()` cria um socket que é **mutado** quando uma conexão chega —
  impossível aceitar múltiplas conexões no mesmo port!

### 4.2 UDP — Funcional mas Frágil

- Callback registration limitado a 16 entradas
- Checksum UDP é 0 (opcional no UDP, mas desabilitado)
- `udp_send` aloca com `kmalloc` e chama `kfree` — mesmo problema de leak

### 4.3 DHCP — Minimalista

```c
void dhcp_discover() {
    // Envia DISCOVER
    // Espera 500 ticks (5 segundos a 100Hz)
    // Se receber OFFER, seta IP e gateway
}
```

Só aceita OFFER (type 2) e ACK (type 5). **Não faz REQUEST formal** — apenas seta
o IP no OFFER. O fluxo DHCP correto é: DISCOVER → OFFER → REQUEST → ACK.

### 4.4 DNS — Funcional mas Síncrono

```c
uint32_t dns_resolve(const char *hostname) {
    // Envia query UDP
    // ESPERA ATÉ 500 TICKS (5s) polling
    while (!dns_ready && (timer_ticks - start) < 500) {
        switch_task();
    }
    return resolved_ip;
}
```

Bloqueante, sem cache, sem múltiplas queries concorrentes.

### 4.5 HTTP — Tudo no Kernel

```c
int http_get(const char *host, uint16_t port, const char *path,
             char *response, uint32_t max_len) {
    uint32_t ip = net_resolve_host(host);  // DNS no kernel
    tcp_socket_t *sock = tcp_connect(ip, port);  // TCP no kernel
    tcp_send(sock, request, len);  // Send no kernel
    tcp_receive(sock, ...);  // Receive no kernel
    tcp_close(sock);  // Close no kernel
}
```

Um cliente HTTP completo dentro do kernel. Se houver um bug no parser de resposta,
é um crash do kernel. Se o servidor demorar, o kernel fica preso.

---

## 5. Drivers — Problemas e Stubs Remanescentes

### 5.1 USB — UHCI e EHCI Parcialmente Implementados

- `usb.c` implementa enumeração USB (get descriptor, set address, set config, parsing
  de interfaces)
- `uhci.c` e `ehci.c` implementam transferências de controle e interrupção
- `usb_hid.c` processa relatórios de teclado e mouse USB

Mas:
- `usb.c` chama `ehci_send_control` sem verificar se o tipo é EHCI primeiro
- A task `usb_poll_task` faz polling de dispositivos USB com um loop que rearma
  QHs do EHCI — mas o código para rearmar é extremamente simplificado
- Não há suporte a USB 3.0 (xHCI)
- Não há suporte a hubs USB
- Não há hot-plug detection real

### 5.2 ATA/BM-IDE — Provavelmente Funcionais

Os drivers ATA PIO e BM-IDE DMA parecem razoáveis. Mas:
- `ata_bmide_init()` é chamado mesmo em LIVECD mode ("inofensivo" segundo o comentário)
- ATA PIO usa polling busy — sem DMA a performance é terrível

### 5.3 GPU (BGA) — Driver Real

O driver Bochs Graphics Adaptor (BGA) em `gpu.c` é funcional e opera diretamente
via MMIO. Não depende de camadas intermediárias.

---

## 6. Compositor/GUI — Overengineering Extremo

### 6.1 Compositor "Wayland" no Kernel

`compositor.c` (~450 linhas) implementa:
- Gerenciamento de superfícies com Z-order (linked list dupla)
- Window frames estilo macOS (bolinhas vermelha/amarela/verde)
- Drag de janelas, redimensionamento, maximizar, minimizar, fechar
- Tráfego de luzes (hover detection)
- Widget rendering básico

Problemas:
- **Tudo no kernel**: Se uma aplicação trava, o compositor (e o sistema) travam
- **Mouse tracking global**: O compositor usa `get_mouse_x()`/`get_mouse_y()` globais
- **Redesenho completo a cada frame**: `compositor_repaint()` redesenha TUDO (todas
  as superfícies, todos os widgets, todos os frames) — sem dirty rects
- **Janelas não são processos separados**: São structs na memória do kernel, não há
  IPC entre aplicação e compositor
- **LGX removido**: A camada Vulkan-like foi removida, agora o compositor desenha
  diretamente com draw_rect/draw_string — sem abstração intermediária

### 6.2 Launcher, Panel, Dock

- `launcher.c`: Menu de apps
- `panel.c`: Barra de tarefas / status bar
- `dock.c`: Dock desktop

Todos são kernel tasks que desenham no framebuffer. Todos compartilham o mesmo espaço
de endereçamento. Qualquer bug de desenho corrompe o framebuffer e crasha o sistema.

### 6.3 "Efeitos" Desnecessários

```c
void draw_box_shadow(int x, int y, int w, int h, int r, int blur, uint32_t color) {
    (void)x; (void)y; (void)w; (void)h;
    (void)r; (void)blur; (void)color;
    // ← NÃO FAZ NADA
}
```

Função declarada, documentada, com parâmetros, mas implementação vazia.

---

## 7. Syscalls — Tabela Incompleta e Inconsistente

### 7.1 Números de Syscall Mal Organizados

```c
switch (call_num) {
    case 1: sys_exit(...); break;
    case 2: sys_brk(...); break;
    case 3: sys_read(...); break;
    case 4: sys_write(...); break;
    case 5: sys_open(...); break;
    case 6: sys_close(...); break;
    case 7: sys_waitpid(...); break;
    case 8: timer_ticks; break;
    case 10: keyboard_get_event(...); break;
    case 11: keyboard_is_pressed(...); break;
    case 12: get_framebuffer_info(...); break;
    case 13: present_frame / refresh; break;
    case 14: fork_process(regs); break;     // ← ADICIONADO
    case 15: do_execve(regs, ...); break;   // ← ADICIONADO
    case 19: sys_lseek(...); break;
    case 20: sdfs_write_file(...); break;
    default: break;
}
```

Case 45 (brk duplicado) removido. Gaps 14-18 parcialmente preenchidos (14 e 15).
Ainda faltam: 16 (getpid), 17, 18.

### 7.2 Syscalls Faltando (Declaradas mas Sem Handler)

- ~~`execve` — código existe em `sys_execve()` mas **não está no switch**~~ → **OK (syscall 15)**
- ~~`fork` — `fork_process()` existe em `task.c` mas **não tem número**~~ → **OK (syscall 14)**
- `getpid` — não existe
- `openpty`, `ioctl`, `stat`, `fstat`, `dup2`, `pipe`, `kill` — não existem
- `gethostbyname`, `socket`, `connect`, `send`, `recv`, `shutdown` — não existem

### 7.3 Layout de FD Problemático

```c
static kfile_t fd_table[32];  // Apenas 32 file descriptors
```

E o código procura a partir de fd=3 (pula 0, 1, 2). Mas fd 0 (stdin) é tratado
especialmente em `sys_read` bloqueando no teclado. fd 1 e 2 (stdout/stderr) vão
para o terminal. Isso é hardcoded.

**Não há suporte a `dup2` ou redirecionamento de FD.**

---

## 8. Terminal — Monstro Monolítico de 2100+ Linhas

### 8.1 O Que o Terminal Faz?

Um arquivo `src/apps/terminal.c` de **2100+ linhas** que contém:
- Shell com mais de 30 comandos built-in
- Editor de texto (modo editor)
- Comando `top` (task manager visual)
- Modo console (saída do kernel)
- Suporte a scrollback
- Suporte a Lua (executa scripts com `dofile`)
- Path resolution com autocomplete
- ANSI-like cursor
- E muito mais

### 8.2 Problemas

- **Monolítico**: Uma única função gerencia shell, editor, e visualização
- **Buffer de texto fixo**: `output_text[8192]` — mensagens são truncadas
- **Buffer de editor fixo**: `editor_buffer[8192]` — arquivos maiores que 8KB
  são cortados
- **Mistura de responsabilidades**: Shell e editor no mesmo arquivo com variáveis
  globais compartilhadas
- **Não há separação entre UI e lógica**: Tudo misturado

---

## 9. Task/Scheduling — Problemas Graves

### 9.1 Round-Robin sem Prioridade

O scheduler é round-robin simples sem prioridades. Uma task que nunca yield
(loop infinito sem `switch_task()`) monopoliza a CPU.

### 9.2 switch_task() é um INT 32

```c
void switch_task() { asm volatile("int $32"); }
```

Isso força uma interrupção de timer. Mas se o timer estiver rodando a 100Hz,
chamar `int $32` adianta o scheduling sem esperar o tick. Funciona, mas é
um pouco hacky — especialmente se chamado em loops apertados (como no waitpid).

### 9.3 ZOMBIE Tasks — Agora com Cleanup

```c
void sys_exit_process(int status) {
    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = status;
    current_task->parent->state = TASK_RUNNING; // Acorda o pai
    while (1) switch_task();
}
```

Antes: ZOMBIE tasks **nunca** liberavam memória — leak garantido.

Agora: O `sys_waitpid` coleta a task ZOMBIE, libera o kernel stack (`kfree(stack_base)`)
e o PCB (`kfree(t)`). A task é removida da lista antes do cleanup.

Ainda pendente: liberar o page directory da task (requer `vmm_free_directory`).

### 9.4 fork_process — Melhorado

- ~~`fork_process` existe mas não tem syscall number~~ → **OK (syscall 14)**
- `fork_process` agora seta `new_task->user_mode = parent->user_mode` (corrigido)
- Cria uma cópia completa do page directory (copia página por página com `kmalloc_a`)
- Mas o fork é chamado de dentro do `irq_handler` context — e o `kmalloc_a` usa
  o heap bump que não é thread-safe
- A função `vmm_copy_directory` copia página por página fisicamente, sem COW
  (Copy on Write) — ou seja, fork duplica toda a memória do processo

### 9.5 ZOMBIE Tasks Agora Liberam Memória

O `sys_waitpid` agora faz cleanup real da task ZOMBIE:
- Kernel stack liberado via `kfree(stack_base)`
- PCB (task_t) liberado via `kfree(t)`
- Task removida da lista antes do cleanup
- Ainda pendente: liberar page directory (requer `vmm_free_directory`)

---

## 10. Segurança — Zero

### 10.1 Tudo no Kernel (Ring 0)

- Terminal, editor, launcher, browser, calculator — rodam em ring 0
- Qualquer buffer overflow nessas apps corrompe o kernel
- Qualquer dereferência de ponteiro inválida causa page fault que derruba o sistema

### 10.2 liwshd Removido

O servidor de shell remoto (`liwshd`) foi removido — ele rodava na porta 2222
sem autenticação, aceitando comandos arbitrários no kernel.

### 10.3 Nenhuma Validação de Ponteiros em User Mode

Em `syscall_handler`:
```c
case 3: regs->eax = sys_read(regs->ebx, (void *)regs->ecx, regs->edx); break;
```

Nunca verifica se `(void *)regs->ecx` (o buffer) é um endereço válido de user space.
Uma aplicação de user mode poderia passar qualquer endereço de kernel e ler memória
arbitrária.

### 10.4 graphics_exclusive Completamente Fake

```c
static int graphics_exclusive_owner = -1;

int graphics_exclusive_active(void) {
    return graphics_exclusive_owner >= 0;
}
```

Um "mutex" que é só uma variável global. Qualquer task pode chamar
`graphics_exclusive_release(-1)` para liberar o lock de outro.

---

## 11. Build System — Overcomplicated

### 11.1 Makefile Gigante

O Makefile tem 232 linhas com:
- Regras para kernel, apps, SDK, libs third-party
- Build de zlib, libpng, libjpeg para user apps
- Criação de ISO com grub-mkrescue
- Ferramentas SDK (liw-builder, img-gen)

Dependências:
- `i686-elf-gcc` (cross-compiler)
- `grub-mkrescue` (GRUB)
- `objcopy`
- `tar`
- Docker opcional

### 11.2 LVGL Removido

O LVGL (biblioteca gráfica third_party) e seus stubs foram removidos do build.
O Makefile foi simplificado, eliminando centenas de arquivos compilados sem
utilidade.

---

## 12. USB Stack — Incompleta e Perigosa

### 12.1 Enumeração

`usb.c` implementa enumeração USB padrão:
1. Get device descriptor (8 bytes primeiro)
2. Set address
3. Get full descriptor
4. Get config descriptor
5. Set configuration
6. Parse interfaces (HID keyboard/mouse)

Mas:
- **Linha 90**: `ehci_send_control(...)` — chama EHCI diretamente SEM verificar
  o tipo de controlador. Se o dispositivo estiver em UHCI, vai falhar.
- A função `usb_enumerate` recebe `type` (1=UHCI, 2=EHCI) mas em vários lugares
  ignora e chama EHCI diretamente.

### 12.2 Polling Task

```c
void usb_poll_task() {
    while (1) {
        usb_device_t *dev = usb_devices;
        while (dev) {
            if (dev->qh) {
                ehci_qh_t *qh = (ehci_qh_t *)dev->qh;
                if (!(qh->overlay.token & (1 << 7))) {
                    // Processa dado, re-arma QH
                }
            }
            dev = dev->next;
        }
        for(int i=0; i<10; i++) switch_task();
    }
}
```

Polling infinito de todos os dispositivos USB. Não há interrupções, não há
notificação — é CPU pura verificando se o hardware terminou.

---

## 13. FAT32 — Write Bugado

### 13.1 O Código Existe mas é Problemático

O `fat32.c` tem 1470 linhas e implementa:
- Mount, Read (cluster chain traversal), ReadDir / FindDir
- Format (com progresso)
- Write cluster allocation, Write FAT entries, Write file data

Problemas conhecidos (mencionados no próprio código):
- Write functions exist but are "buggy"
- Cluster chain management problemático
- `fat32_format` usa `kmalloc` para buffers de 512 bytes — sem verificação de falha

### 13.2 VLA no Kernel

```c
uint8_t cluster_buffer[cluster_size];  // ← Variable Length Array no kernel!
```

VLAs em kernel são perigosos — se `cluster_size` for grande (ex: 64KB), isso
estoura a stack do kernel.

---

## 14. Bugs Específicos no Código

### 14.1 Código Morto e Inconsistências

- `ensure_disk_ready()` definida mas não chamada (comentário "disable for LIVECD")
- `itoa` declarada como `extern` em vários lugares, implementada em `string.c`
- `serial_print_hex` — formatação de IP repetida em múltiplos lugares
- `syscall.c_tail` — arquivo órfão (não usado por ninguém)

### 14.2 VMM — Bug de Page Table

Em `vmm_map_page`:
```c
if (!dir->tablesVirtual[pd_index]) {
    // aloca nova page table
} else {
    pd[pd_index] |= pd_flags;  // MODIFICA entrada existente
}
```

No branch `else`, modifica a entrada do page directory adicionando flags — mas
se a entrada já existia com flags diferentes, pode corromper o endereço físico.

### 14.3 vmm_create_directory usa pd não mapeado

```c
page_directory_t *vmm_create_directory() {
    dir->physicalAddr = (uint32_t)kmalloc_ap(4096, &phys_addr);
    memset((void *)dir->physicalAddr, 0, 4096);  // ← acesso a endereço físico!
    for (int i = 0; i < 1024; i++) {
        if (kernel_directory->tablesVirtual[i]) {
            dir->tablesVirtual[i] = kernel_directory->tablesVirtual[i];
            uint32_t *pd = (uint32_t *)dir->physicalAddr;
            uint32_t *k_pd = (uint32_t *)kernel_directory->physicalAddr;
            pd[i] = k_pd[i];
        }
    }
}
```

Acesso a `dir->physicalAddr` via `(uint32_t *)dir->physicalAddr` — isso assume
que o endereço físico da page table está mapeado no espaço virtual atual.

---

## 15. Problemas de Nomenclatura e Organização

### 15.1 Mistura Português/Inglês

```c
// kernel.c
bool is_live_mode = true;
memory_size = 0;

// vmm.c
void init_vmm(uint32_t memory_size)

// fat32.c
draw_string(10, 100, "FAT32: Falha ao ler o setor de boot.", 0xFF0000);
```

Nomes de variáveis, funções, comentários, mensagens — mistura português e inglês
constantemente, às vezes na mesma linha.

### 15.2 Organização Confusa de Diretórios

```
src/
├── kernel/       ← kernel core, MAS TAMBÉM string.c
├── boot/         ← boot.s, interrupt.s, linker.ld, test.s
├── drivers/      ← drivers, MAS TAMBÉM video.c (core rendering)
├── fs/           ← filesystems
├── net/          ← networking stack
├── gui/          ← compositor
└── apps/         ← kernel apps (14 arquivos)
```

`string.c` (implementação de string.h) está em `src/kernel/` — deveria estar
em `libc/` ou `sdk/`.

### 15.3 Headers Inflados

`include/` tem muitos arquivos .h para um kernel relativamente simples.

---

## 16. Documentação vs Realidade

### 16.1 README.md

O README provavelmente descreve um SO incrível com:
- "Microkernel architecture" (é monólito)
- "Wayland compositor" (é falso)
- "Full TCP/IP stack" (é implementação frágil)
- "WiFi support" (removido — era stub)
- "USB HID support" (implementação parcial)

### 16.2 KERNEL_DEBUG_REPORT.md

Relatório de 552 linhas que analisa o page fault handler, VMM, e FAT32.

### 16.3 PLANO_VFS.md

Plano descrevendo um VFS que está PARCIALMENTE implementado.
Metade dos itens do plano não foram feitos ("Next Steps" com checkboxes vazios).

---

## 17. Resumo Final

### Problemas Críticos (Precisam Ser Resolvidos AGORA)

| # | Problema | Gravidade | Status |
|---|---------|-----------|--------|
| 1 | ~~`kfree()` é stub vazio — memory leak em todo lugar~~ | ~~CRÍTICO~~ | ✅ **RESOLVIDO** |
| 2 | Apps do sistema rodam em ring 0 | **CRÍTICO** | Parcial (fork/execve existem, apps ainda em ring 0) |
| 3 | TCP buffer linear sem wrap | **ALTO** | Pendente |
| 4 | ~~Task ZOMBIE nunca liberam memória~~ | ~~ALTO~~ | ✅ **RESOLVIDO** |
| 5 | PMM `pmm_used_blocks` inconsistente | **MÉDIO** | Não confirmado (contagem balanceada) |
| 6 | VMM mapeia memória demais na inicialização | **MÉDIO** | Pendente |
| 7 | USB enumeração chama EHCI sem verificar tipo | **MÉDIO** | Pendente |

### O Que Está OK (Honestamente)

- **Boot sequence**: boot.s → kernel_main é clara e funcional
- **GDT/IDT**: Configuração correta de segmentos e interrupções
- **Keyboard/mouse PS/2**: Drivers implementados corretamente com ABNT2
- **initrd/tar**: Parser funcional
- **SDFS**: Parece funcional para leitura/escrita
- **ATA PIO**: Driver ATA parece correto
- **PCI scanning**: Funcional
- **ELF loader**: Parece correto
- **GPU BGA**: Driver funcional sem abstrações desnecessárias

### Diagnóstico Final

O LiwusOS é um projeto ambicioso que tenta abraçar o mundo: kernel próprio,
TCP/IP stack, GUI com compositor Wayland-like, USB, sistema de arquivos,
suporte a Lua, port de Doom, etc.

**O problema fundamental é que o projeto tenta fazer TUDO ao mesmo tempo e
nada está completo ou robusto.** A arquitetura é frágil (tudo no kernel),
muitos subsistemas são incompletos, e há inconsistências graves.

O código que existe parece ter sido escrito por alguém com conhecimento de
sistemas operacionais (as estruturas de paginação, GDT, interrupções estão
corretas), mas que se empolgou adicionando features demais antes das bases
estarem sólidas.

### Recomendações (Atualizado)

1. ✅ ~~**Consertar o `kfree`** — implementado free list allocator~~
2. ~~**Mover apps do sistema para user mode** — fork/execve syscalls adicionadas,~~ apps ainda precisam ser migradas
3. **Simplificar a API gráfica** — já foi removido o LGX fake, manter assim
4. **Fazer o VMM mapear apenas o necessário** — não identity-map 4GB
5. **Consertar o TCP buffer** — implementar circular buffer de verdade
6. **Adicionar validação de ponteiros em syscalls**
7. **Cobrir com testes** — pelo menos unit tests para PMM, VFS, TCP
8. **Padronizar idioma** — escolher português ou inglês e manter
9. ✅ ~~**Implementar `fork` syscall number**~~

---

## 18. Mudanças Realizadas (Cleanup 2026)

### Removido (Código Inútil)

| Arquivo | Linhas | Motivo |
|---------|--------|--------|
| `src/drivers/wifi.c` | 51 | Stub completo — não enviava nem recebia |
| `src/drivers/virtio.c` | 340 | Implementado mas desabilitado (timeout) |
| `src/gui/lvgl_stubs.c` | 38 | Stubs para LVGL que não era usado |
| `src/gui/lvgl_shell.c` | 394 | Shell LVGL ligando a stubs |
| `src/gui/lgx.c` | 843 | API Vulkan-like falsa sobre framebuffer |
| `src/kernel/liw.c` | 36 | Package manager fake |
| `src/kernel/libliw.c` | 58 | Biblioteca liw fake |
| `src/kernel/linker_fix.c` | 24 | Gambiarra de linker (movido para kernel.c) |
| `src/net/liwshd.c` | 73 | Shell remoto na porta 2222 sem auth |
| `src/apps/test_posix.c` | ~60 | Teste que usava libliw.h deletado |
| `include/lgx*.h` (8 arquivos) | ~400 | Headers Vulkan-like |
| `include/wifi.h` | ~20 | Header wifi stub |
| `include/liw*.h` (2 arquivos) | ~40 | Headers package manager |
| `include/virtio.h` | ~50 | Header VirtIO |
| `include/lvgl_shell.h` | ~20 | Header shell LVGL |
| `include/lv_conf.h` | ~100 | Config LVGL |
| `third_party/lvgl/` | milhares | Biblioteca gráfica compilada mas não usada |

### Modificado

| Arquivo | Mudança |
|---------|---------|
| `src/kernel/kernel.c` | graphics_exclusive movido para cá; removido liwshd, wifi, dns duplicado, dhcp |
| `src/gui/compositor.c` | Removida toda integração LGX (externs, init, command buffers, submit, present) |
| `src/apps/terminal.c` | Removido lvgl_shell e liwshd mirror |
| `src/apps/settings.c` | Removida seção WiFi (stub) |
| `src/apps/dock.c` | Removidos botões Web, Instalar; layout simplificado |
| `src/gui/panel.c` | Removidos botões Web, Book |
| `src/apps/launcher.c` | Removidos Book Reader, Web Browser |
| `src/drivers/pci.c` | Removido include de virtio.h |
| `include/compositor.h` | graphics_exclusive_active declarado como extern |
| `Makefile` | Removido LVGL (CFLAGS, SRCS, regras de compilação) |

### Adicionado (Syscalls fork/execve)

| Arquivo | Descrição |
|---------|-----------|
| `include/syscall_nums.h` | Header com defines para todos os números de syscall (SYS_FORK=14, SYS_EXECVE=15, etc.) |
| `src/kernel/syscall.c:case 14` | `regs->eax = fork_process(regs)` — fork syscall |
| `src/kernel/syscall.c:case 15` | `do_execve(regs, filename, argv, envp)` — execve syscall que carrega ELF, monta stack e altera regs->eip/useresp |
| `sdk/libc/syscall.c:fork()` | Implementação real de `fork()` via syscall 14 (antes era stub `return -1`) |
| `sdk/libc/syscall.c:execve()` | Implementação real de `execve()` via syscall 15 |

### Removido

| Item | Motivo |
|------|--------|
| `case 45: sys_brk(...)` | brk duplicado (já existe no case 2) |
| `src/apps/book.c` | App kernel nunca iniciada (código morto) |
| `src/apps/browser.c` | App kernel nunca iniciada (código morto) |
| `src/apps/installer.c` | App kernel nunca iniciada (código morto) |
| `src/apps/uninstaller.c` | App kernel nunca iniciada (código morto) |
| `src/apps/welcome.c` | App kernel nunca iniciada (código morto) |
| `include/book.h`, `browser.h`, `installer.h`, `welcome.h` | Headers órfãos |

### Adicionado

| Item | Descrição |
|------|-----------|
| `apps/calc/calc.c` | Calculadora gráfica em user mode (ELF), usa framebuffer via syscalls + font VGA |
| `repo/calc` | ELF copiado para o initrd, executável via `exec calc`, botão no dock/launcher |
| Botão "Calc" no dock | Abre `calc` via `launch_initrd_program("calc")` |
| Botão "Calculadora" no launcher | Abre `calc` via `launch_program("calc")` |
| Comando `calc` no terminal | Fallback: comandos desconhecidos tentam executar como programa |

### Corrigido

| Item | Correção |
|------|----------|
| `fork_process` em `task.c` | Adicionado `new_task->user_mode = parent->user_mode` para preservar modo do processo pai |
| `kfree` stub vazio | Implementado free list allocator — `kfree` agora adiciona à free list; `kmalloc` reusa blocos |
| `kfree` de page-aligned | Blocos page-aligned também liberam páginas físicas via `pmm_free_block` |
| Task ZOMBIE cleanup | `sys_waitpid` agora libera kernel stack e PCB da task coletada |
| `task_t` struct | Adicionado `kernel_stack_size` para permitir cleanup correto do kernel stack |
| Terminal `else` | Comandos desconhecidos agora tentam executar via `launch_initrd_program` antes de falhar |

---

*Análise gerada em junho de 2026. Total de problemas catalogados: ~55.*

*Análise gerada em junho de 2026. Total de problemas catalogados: ~55.*
