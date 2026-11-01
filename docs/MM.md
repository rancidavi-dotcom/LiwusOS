# Modelo de memória do LiwusOS

Este documento registra o mapa alvo e o estado implementado da Fase 1. Os passos 9a–9c ligam o kernel em `0xffffffff80000000`, usam um direct map separado e removem a identidade depois da transição do BSP para uma stack alta e da inicialização dos APs. O passo 10 amplia o direct map e o PMM até 512 GiB. O passo 11 libera endereços canônicos de userspace, posiciona a stack no topo e reserva uma faixa alta para mmap. A validação QEMU desta revisão usou 4 GiB e provou uma alocação acima de 1 GiB; o limite de 512 GiB é de endereçamento configurado, não um teste físico integral.

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

> **Decisão (Fase 1, passos 9a–11):** a faixa `0x0000000000400000`–`0x0000000040000000` é espaço de usuário livre, e não uma reserva para o modelo antigo de heap/ELF. O loader mantém suporte a imagens ET_EXEC fixas e `brk` delimitado por VMA; PIE ET_DYN executa na base de desenvolvimento `USER_PIE_BASE`. Nenhum código deve tratar a faixa como legado reservado.

## Mapa kernel da metade alta

| Base/faixa | Uso |
|---|---|
| `0xffff800000000000` + físico | direct map, conversão `phys_to_virt`; tamanho derivado do mapa de memória e do limite físico suportado |
| `0xffffc00000000000`–`0xffffffff7fffffff` | região reservada para vmalloc, stacks e mappings de kernel não contíguos |
| `0xffffffff80000000`–fim do kernel | imagem linkada do kernel: text, rodata, data, bss |
| demais endereços altos | reservados até serem atribuídos explicitamente |

O layout exato da imagem é definido pelo linker script. Text é read-only/executable; rodata read-only/NX; data/bss read-write/NX. Boot começa com mapeamento de identidade mínimo para transição e o remove depois de instalar o mapa definitivo. MMIO não usa cache WB e é mapeado com atributos de cache apropriados.

**Estado após 9c–11 e passo 13:** `DIRECT_MAP_BASE` é `0xffff800000000000`; o mapa Multiboot2 estende os aliases a RAM utilizável até 512 GiB, com páginas grandes de 2 MiB e páginas de 4 KiB nas bordas. PMM limita-se às regiões disponíveis e retorna ponteiros direct-map; page tables guardam endereços físicos. A entrada de boot usa identidade temporária, removida depois que APs deixam o trampoline físico e o BSP muda para stack no direct map. MMIO usado pelo kernel (framebuffer, ACPI, APIC, PCI ECAM e AHCI) é acessado por aliases direct-map com atributos de cache explícitos. Userspace admite endereços canônicos abaixo de `USER_ADDR_MAX`; a antiga faixa de identidade não é mais reservada. ELF `ET_DYN` sem `PT_INTERP` carrega com bias determinístico `USER_PIE_BASE`; NXE e W^X estão ativos quando a CPU oferece NX. Em QEMU com 4 GiB, o teste alocou, acessou e liberou um frame acima de 1 GiB.

## Mapa físico e PMM

- A fonte de memória física é o mapa de memória Multiboot2, não uma suposição de RAM contígua.
- O PMM gerencia frames de 4 KiB apenas dentro de regiões disponíveis. Regiões firmware-reserved, kernel, módulos/initrd, bitmap PMM, heap de boot e framebuffer/MMIO ficam reservadas.
- Bitmap/estruturas do PMM ficam em RAM reservada e são acessadas pelo direct map. A implementação futura pode particionar bitmaps para evitar exigir um único intervalo contíguo.
- Endereços físicos acima de 4 GiB são aceitos se a CPU e as page tables os suportarem; o bootloader e os tipos Multiboot2 precisam preservar os 64 bits.
- Frame físico zero e memória de firmware não são alocados. Overflow de `base + length` é rejeitado/limitado com segurança.
- O mapa cobre RAM física instalada e necessária sem tornar MMIO alocável pelo PMM. O direct map de RAM inclui buracos dentro das regiões mapeadas por páginas grandes; MMIO recebe aliases explícitos e não cacheáveis.

## Espaço de processo e VMA

Cada processo possui um `mm` independente com page tables e VMAs ordenadas. O diretório de páginas contém uma lista ligada ordenada de VMA `[start,end)` com proteção, flags, offset e backing; ELF, stack, heap, `mmap`, `munmap` e fork atualizam essa lista. As operações `_in(page_directory_t *)` mapeiam, alteram permissões, removem ou consultam páginas no diretório explícito; o loader também grava a imagem via frame físico do diretório alvo, sem exigir que ele seja o CR3 ativo. Os drivers passam `kernel_directory` explicitamente ao mapear MMIO. Wrappers sem `_in` ainda existem para compatibilidade interna de boot e atuam sobre `current_directory`. O backing de arquivo ainda não está ligado ao VFS. Page tables são materializadas eager, e page fault de user ainda termina a tarefa em vez de consultar VMA para demand paging. O kernel mantém seu mapeamento supervisor em todos os CR3 de processo. A página nula e as guardas de stack não têm PTE presente.

Uma VMA representa `[start, end)`, proteção R/W/X, flags (anônima, arquivo, privada/compartilhada, stack, heap), backing e offset. Page tables representam somente páginas residentes. Page fault verifica VMA e materializa página sob demanda. `mmap`, `munmap`, `mprotect`, `brk`, fork COW e loader atualizam VMAs e page tables por uma API que recebe o `mm` explicitamente.

## Stacks de kernel e de usuário

- Stacks de tarefas de kernel usam slots em `0xffffc00000000000`, com 16–32 KiB contíguos no espaço virtual, frames físicos possivelmente dispersos, uma página de guarda inferior sem mapeamento e faixa não usada superior também sem PTE. O alocador propaga cada mapeamento supervisor para os diretórios existentes e para os criados depois.
- Cada tarefa tem stack de kernel própria; syscall troca para o topo da stack da tarefa. Cada CPU tem uma stack de IRQ separada. #PF e #DF usam stacks IST por CPU; um #PF de ring 3 transfere seu frame para a stack da tarefa antes de encerrar o processo e escalonar.
- `tests/test_kstack.c` verifica PTEs ausentes nas guardas, propagação para outro `mm`, falha real ao escrever na guarda, remoção dos PTEs ao liberar e separação da stack IRQ. `make smap-test` passou com 4 CPUs e SMAP.

## Stack, heap e mmap de usuário

- Stack principal começa perto do topo de userspace (`0x00007fffffff0000` no layout inicial), tem limite e guard page; ainda é materializada de forma eager, sem crescimento por demand paging.
- Stack inicial Linux contém argc, argv, envp e auxv, termina com alinhamento ABI de 16 bytes e inclui AT_RANDOM.
- Heap `brk` é VMA própria iniciada após o segmento de dados do ELF; cresce para cima até colidir com outra VMA ou limite configurado.
- Região mmap é escolhida dinamicamente em área livre alta (normalmente `0x0000600000000000` a `0x00007f0000000000`), sem assumir uma única fronteira fixa.
- PIE carrega em base aleatória ou base determinística de desenvolvimento dentro da faixa baixa/média; bibliotecas e loader podem usar faixa alta.
- VMA de stack, heap e mmap não se sobrepõem. Busca de espaço verifica overflow, alinhamento, limites e substituição de MAP_FIXED antes de alterar estado.

## Proteção e troca de contexto

- EFER.NXE habilita PTE.NX; páginas de dados são NX por padrão e segmentos de código não são graváveis por padrão.
- W^X é imposto em mprotect e no loader após cópia dos segmentos.
- SMEP/SMAP são habilitados somente após checagem CPUID e rotinas de entrada/usercopy corretas. STAC/CLAC delimitam acessos intencionais a userspace.
- Troca de `mm` carrega CR3 e as invalidações locais necessárias. A validação iniciou quatro CPUs, mas o scheduler de user permanece no BSP; não há TLB shootdown remoto para migração de tarefas nem suporte demonstrado a alteração concorrente de `mm` ativo em APs.
- KPTI fica adiado inicialmente; documentar que o kernel compartilha tabelas supervisor no CR3 de usuário e revisar isso conforme o modelo de ameaça.
