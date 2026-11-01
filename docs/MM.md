# Modelo de memória do LiwusOS

Este documento registra o mapa alvo e o estado implementado da Fase 1. Os passos 9a–9c ligam o kernel em `0xffffffff80000000`, usam um direct map separado e removem a identidade depois da transição do BSP para uma stack alta e da inicialização dos APs. O passo 10 amplia o direct map e o PMM até 512 GiB. O passo 11 libera endereços canônicos de userspace, posiciona a stack no topo e reserva uma faixa alta para mmap.

## Mapa virtual x86-64 alvo

O mapa usa endereços canônicos de 48 bits (LA57 desativado). A metade baixa é userspace; a metade alta pertence ao kernel. As faixas são reservadas por função e devem ser validadas contra VMAs reais antes de mapear.

| Faixa virtual | Uso |
|---|---|
| `0x0000000000000000`–`0x0000000000000fff` | página nula, permanentemente inválida |
| `0x0000000000001000`–`0x00000000003fffff` | início livre do userspace |
| `0x0000000000400000`–`0x0000000040000000` | espaço de usuário livre; sem reserva para heap ou imagem ELF legada |
| `0x0000000040000000`–`0x0000555555554000` | espaço livre para mappings de baixa/média faixa |
| em torno de `0x0000555555554000` | base sugerida para PIE ET_DYN; ASLR futuro escolhe entre candidatos válidos |
| `0x0000600000000000`–`0x00007f0000000000` | faixa de mmap de bibliotecas e arquivos |
| `0x00007f0000000000`–`0x00007fff00000000` | mappings altos e bibliotecas, sujeitos à busca por VMA livre |
| abaixo de `0x00007fffffff0000` | stack principal; limite exato configurável, alinhado e com guard page |
| `0x00007fffffff0000`–`0x00007ffffffff000` | guardas/reserva superior; não mapear por padrão |
| `0x0000800000000000`–`0xffff7fffffffffff` | não canônico; inválido |
| `0xffff800000000000`–`0xffffffffffffffff` | metade alta do kernel |

O limite de user virtual é exclusivo: `USER_ADDR_MAX = 0x0000800000000000`. Todo início e fim deve ser canônico, sem overflow e fora das regiões reservadas. O teto de endereço virtual não implica que exista RAM física no mesmo endereço.

> **Decisão (Fase 1, passo 9a):** a faixa `0x0000000000400000`–`0x0000000040000000` é espaço de usuário livre, e não uma reserva para o modelo antigo de heap/ELF. O loader atual ainda carrega imagens fixas e mantém `brk` simples nessa região porque o kernel ainda não tem VMAs. Nenhum código novo deve tratar essa faixa como legado.

## Mapa kernel da metade alta

| Base/faixa | Uso |
|---|---|
| `0xffff800000000000` + físico | direct map, conversão `phys_to_virt`; tamanho derivado do mapa de memória e do limite físico suportado |
| `0xffffc00000000000`–`0xffffffff7fffffff` | região reservada para vmalloc, stacks e mappings de kernel não contíguos |
| `0xffffffff80000000`–fim do kernel | imagem linkada do kernel: text, rodata, data, bss |
| demais endereços altos | reservados até serem atribuídos explicitamente |

O layout exato da imagem é definido pelo linker script. Text é read-only/executable; rodata read-only/NX; data/bss read-write/NX. Boot começa com mapeamento de identidade mínimo para transição e o remove depois de instalar o mapa definitivo. MMIO não usa cache WB e é mapeado com atributos de cache apropriados.

**Estado após 9c–11:** `DIRECT_MAP_BASE` é `0xffff800000000000`; o mapa Multiboot2 estende os aliases a RAM utilizável até 512 GiB, com páginas grandes de 2 MiB e páginas de 4 KiB nas bordas. PMM limita-se às regiões disponíveis e retorna ponteiros direct-map; page tables guardam endereços físicos. A entrada de boot usa identidade temporária, removida depois que APs deixam o trampoline físico e o BSP muda para stack no direct map. MMIO usado pelo kernel (framebuffer, ACPI, APIC, PCI ECAM e AHCI) é acessado por aliases direct-map com atributos de cache explícitos. Userspace admite endereços canônicos abaixo de `USER_ADDR_MAX`; a antiga faixa de identidade não é mais reservada. O loader ainda não carrega ET_DYN/PIE.

## Mapa físico e PMM

- A fonte de memória física é o mapa de memória Multiboot2, não uma suposição de RAM contígua.
- O PMM gerencia frames de 4 KiB apenas dentro de regiões disponíveis. Regiões firmware-reserved, kernel, módulos/initrd, bitmap PMM, heap de boot e framebuffer/MMIO ficam reservadas.
- Bitmap/estruturas do PMM ficam em RAM reservada e são acessadas pelo direct map. A implementação futura pode particionar bitmaps para evitar exigir um único intervalo contíguo.
- Endereços físicos acima de 4 GiB são aceitos se a CPU e as page tables os suportarem; o bootloader e os tipos Multiboot2 precisam preservar os 64 bits.
- Frame físico zero e memória de firmware não são alocados. Overflow de `base + length` é rejeitado/limitado com segurança.
- O mapa cobre RAM física instalada e necessária sem tornar MMIO alocável pelo PMM. O direct map de RAM inclui buracos dentro das regiões mapeadas por páginas grandes; MMIO recebe aliases explícitos e não cacheáveis.

## Espaço de processo e VMA

Cada processo possui um `mm` independente com page tables e VMAs ordenadas. O kernel mantém seu mapeamento em todos os CR3 de processo conforme necessário; páginas do kernel são supervisor-only. Páginas de usuário têm U/S e permissões coerentes com a VMA. A página nula e a página de guarda nunca têm PTE presente.

Uma VMA representa `[start, end)`, proteção R/W/X, flags (anônima, arquivo, privada/compartilhada, stack, heap), backing e offset. Page tables representam somente páginas residentes. Page fault verifica VMA e materializa página sob demanda. `mmap`, `munmap`, `mprotect`, `brk`, fork COW e loader atualizam VMAs e page tables por uma API que recebe o `mm` explicitamente.

## Stack, heap e mmap de usuário

- Stack principal começa perto do topo de userspace (`0x00007fffffff0000` no layout inicial), cresce para baixo por demand paging e tem limite e guard page.
- Stack inicial Linux contém argc, argv, envp e auxv, termina com alinhamento ABI de 16 bytes e inclui AT_RANDOM.
- Heap `brk` é VMA própria iniciada após o segmento de dados do ELF; cresce para cima até colidir com outra VMA ou limite configurado.
- Região mmap é escolhida dinamicamente em área livre alta (normalmente `0x0000600000000000` a `0x00007f0000000000`), sem assumir uma única fronteira fixa.
- PIE carrega em base aleatória ou base determinística de desenvolvimento dentro da faixa baixa/média; bibliotecas e loader podem usar faixa alta.
- VMA de stack, heap e mmap não se sobrepõem. Busca de espaço verifica overflow, alinhamento, limites e substituição de MAP_FIXED antes de alterar estado.

## Proteção e troca de contexto

- EFER.NXE habilita PTE.NX; páginas de dados são NX por padrão e segmentos de código não são graváveis por padrão.
- W^X é imposto em mprotect e no loader após cópia dos segmentos.
- SMEP/SMAP são habilitados somente após checagem CPUID e rotinas de entrada/usercopy corretas. STAC/CLAC delimitam acessos intencionais a userspace.
- Troca de `mm` carrega CR3 e executa as invalidações necessárias. Shootdown SMP é necessário antes de habilitar uso geral de múltiplos CPUs.
- KPTI fica adiado inicialmente; documentar que o kernel compartilha tabelas supervisor no CR3 de usuário e revisar isso conforme o modelo de ameaça.
