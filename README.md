# LiwusOS

LiwusOS e um sistema operacional experimental escrito em C para arquitetura x86 de 32 bits, com foco em:

- entender o sistema inteiro de ponta a ponta
- controlar o boot, kernel, drivers, userland e SDK no mesmo repositório
- provar capacidade real com ports concretos, como Doom e Lua
- evoluir com honestidade tecnica, sem fingir que algo existe quando ainda nao existe

Este README foi escrito para ser um documento de referencia completo do projeto. A ideia aqui nao e fazer marketing do sistema, e sim explicar:

- o que o LiwusOS realmente faz hoje
- como ele e organizado internamente
- como compilar, rodar e depurar
- quais subsistemas ja existem
- quais partes ainda estao incompletas
- quais decisoes arquiteturais foram tomadas
- quais caminhos existem para evolucao futura

Se voce quer apenas rodar o sistema, ha uma secao rapida mais abaixo.
Se voce quer entender o projeto inteiro, este README foi pensado para isso.

---

# Sumario

1. Visao Geral
2. Estado Atual do Projeto
3. Filosofia do LiwusOS
4. O que o LiwusOS ja prova na pratica
5. O que ele ainda nao prova
6. Caracteristicas Principais
7. Arquitetura Geral
8. Fluxo de Boot
9. Estrutura do Kernel
10. Gerenciamento de CPU e Privilegios
11. GDT, IDT e Interrupcoes
12. Syscalls
13. Gerenciamento de Memoria
14. PMM
15. VMM
16. Heap do Kernel
17. Userland
18. Tasks, Scheduler e Context Switch
19. Execucao de Programas
20. Loader ELF
21. Processos e Modelo de Execucao
22. Sistema de Arquivos
23. Initrd
24. FAT32
25. Disco Persistente
26. Drivers
27. Driver ATA
28. Driver de Video
29. Driver de Teclado
30. Driver de Mouse
31. Serial
32. PCI
33. RTL8139
34. VirtIO e estado atual
35. Pilha de Rede
36. Ethernet
37. ARP
38. IPv4
39. ICMP
40. TCP
41. HTTP
42. Terminal como Ambiente Principal
43. Shell Interna do LiwusOS
44. Comandos Ja Disponiveis
45. Editor em modo texto
46. `top`
47. Lua como linguagem de script do sistema
48. Doom como marco tecnico
49. Apps do projeto
50. GUI antiga, GUI atual e caminho futuro
51. Estado do LVGL
52. SDK e Toolchain
53. Layout do Repositorio
54. Como Compilar
55. Como Rodar
56. Como Testar subsistemas
57. Como depurar pelo log do QEMU
58. Limitacoes atuais
59. Roadmap recomendado
60. Contribuicao
61. FAQ tecnico
62. Glossario
63. Creditos

---

# Visao Geral

LiwusOS e um sistema operacional own-stack.

Isso significa que o repositorio tenta cobrir o caminho inteiro:

- boot
- kernel
- gerenciamento de memoria
- arquivos
- drivers
- rede
- userland
- SDK
- ports de programas reais

Ele nao e um microkernel, nao e um sistema que roda sobre Linux, nao e uma distro, e nao e um projeto apenas de UI. Ele e um OS experimental de verdade, com kernel proprio, boot proprio e APIs proprias.

Ao mesmo tempo, o LiwusOS ainda e um projeto em evolucao.

Ele nao tenta fingir maturidade de Linux, BSD ou Windows. O valor dele esta em:

- ser legivel
- ser hackavel
- ser extensivel
- ser suficientemente real para sustentar ports e ferramentas uteis

Hoje, o LiwusOS esta num ponto interessante:

- ele ja roda Doom
- ja roda Lua
- ja monta FAT32
- ja tem shell interativa
- ja tem ping real com placa RTL8139
- ja suporta execucao de programas em user mode

Isso o coloca num patamar acima de um kernel de brinquedo que apenas imprime texto.

---

# Estado Atual do Projeto

O estado atual mais importante para entender o repositiorio hoje e:

- o boot padrao esta em modo terminal-only
- a GUI antiga continua no codigo, mas nao e o caminho principal de execucao neste momento
- o sistema esta sendo consolidado primeiro em cima de terminal, disco, rede, shell, Lua e userland

Isso foi uma decisao pratica.

Durante o desenvolvimento, houve varias iteracoes em GUI propria, compositor, taskbar, Explorer, janelas, LGX e tentativas com LVGL. Essas experiencias ensinaram bastante, mas tambem introduziram complexidade visual e bugs que atrapalhavam o amadurecimento do nucleo do sistema.

Hoje, o LiwusOS privilegia:

- estabilidade
- simplicidade de execucao
- depuracao clara
- produtividade no core do OS

Por isso, o terminal virou a interface principal de uso do sistema.

O resultado e um sistema menos "bonito" visualmente, mas bem mais facil de evoluir com consistencia.

---

# Filosofia do LiwusOS

Alguns principios guiam o projeto:

## 1. Honestidade tecnica

Algo so entra como "funciona" quando funciona de verdade.

Exemplos:

- `ping` so passou a ser tratado como pronto quando houve resposta ICMP real
- Doom so foi considerado "portado" quando realmente abriu e renderizou
- FAT32 so passou a ser "persistente" quando montou de verdade entre boots

## 2. Verticalidade

O projeto prefere entender a stack inteira:

- bootloader
- interrupcoes
- paginação
- syscalls
- userland
- apps

Isso tem custo, mas produz compreensao muito mais profunda.

## 3. Pragmatismo

Nem toda camada precisa ser reinventada ao mesmo tempo.

Exemplo:

- foi considerado migrar tudo para LVGL
- depois foi decidido reduzir a GUI e priorizar terminal
- isso nao significa abandonar a GUI para sempre
- significa escolher a ordem de complexidade

## 4. Ports reais como testes de integracao

Trazer software de verdade para dentro do LiwusOS e uma forma excelente de validar o sistema.

Exemplos claros no projeto:

- Doomgeneric
- Lua

Esses ports pressionam varias camadas ao mesmo tempo:

- memoria
- arquivos
- stdout
- teclado
- loader
- syscalls
- tempo
- framebuffer

## 5. Evoluir o sistema antes da aparencia

Uma GUI bonita sem base robusta vira um peso.

Um terminal forte com:

- disco
- shell
- editor
- rede
- scripts

cria a base certa para voltar a GUI depois com mais qualidade.

---

# O que o LiwusOS ja prova na pratica

Hoje o LiwusOS ja prova, na pratica, varias capacidades reais.

## Boota por GRUB

O kernel e carregado como multiboot e entra corretamente no caminho de inicializacao.

## Inicializa memoria

O sistema faz setup de:

- memoria fisica
- paginação
- heap do kernel

## Instala GDT e IDT

O kernel sobe a configuracao basica de CPU para:

- segmentos
- interrupcoes
- excecoes
- syscalls

## Executa userland

O LiwusOS nao e apenas kernel monolitico sem apps.

Ele ja executa codigo em userland com:

- loader ELF
- stack de usuario
- syscalls

## Monta initrd

O sistema usa um `initrd.tar` carregado no boot para empacotar programas e recursos iniciais.

## Monta FAT32 persistente

Existe um disco virtual persistente em FAT32 que pode armazenar dados entre boots.

## Roda Doom

Esse e um marco classico por um motivo.

Rodar Doom implica que varias camadas estao funcionando juntas.

## Roda Lua

Lua esta portada e pode ser usada como linguagem de script do sistema.

## Tem shell interativa

Existe um ambiente de linha de comando funcional com comandos uteis.

## Tem rede Ethernet real

Com a placa emulada `RTL8139` no QEMU, o sistema consegue:

- subir interface
- resolver ARP
- enviar ICMP
- receber resposta do gateway do QEMU

---

# O que ele ainda nao prova

Para manter o documento honesto, tambem vale deixar claro o que o LiwusOS ainda nao prova.

Ele ainda nao prova:

- seguranca forte
- isolamento robusto contra apps maliciosos
- maturidade POSIX ampla
- compatibilidade com software complexo estilo Linux desktop
- TLS/HTTPS completo
- stack TCP madura
- multiprocessamento SMP
- audio de sistema maduro
- gerenciamento de energia
- GUI final consolidada

O projeto esta num estagio forte para um hobby OS serio, mas ainda longe de um sistema operacional pronto para uso geral.

---

# Caracteristicas Principais

Resumo rapido das capacidades atuais mais relevantes:

- kernel x86 32-bit
- boot por GRUB multiboot
- GDT, IDT, ISR e syscalls
- PMM, VMM e heap do kernel
- tasking e scheduler cooperando com timer
- userland com ELF loader
- initrd em tar
- FAT32 persistente em disco virtual
- terminal como interface principal
- shell interna
- editor em texto
- `top`
- Lua portada
- Doomgeneric portado
- rede via RTL8139
- `ifconfig`
- `ping`
- `wget` experimental
- SDK com libc propria

---

# Arquitetura Geral

Em alto nivel, o sistema pode ser pensado em camadas:

1. Boot
2. Kernel core
3. Drivers
4. Filesystems
5. Rede
6. Userland
7. SDK e tools

Um fluxo simplificado:

1. GRUB carrega `kernel.bin` e `initrd.tar`
2. kernel entra no modo protegido e inicializa subsistemas basicos
3. memoria e interrupcoes sao configuradas
4. drivers essenciais sobem
5. initrd e montado
6. FAT32 e montado no disco virtual
7. tasking e syscalls ficam prontas
8. terminal entra como interface principal
9. usuario executa comandos e programas

---

# Fluxo de Boot

O boot do LiwusOS passa por um caminho razoavelmente classico de OS x86.

## Etapa 1: GRUB

O projeto usa GRUB para boot.

Isso evita ter que manter um bootloader custom muito cedo no projeto e permite focar na parte interessante:

- kernel
- memoria
- drivers
- userland

O arquivo de configuracao de boot fica em:

- `src/boot/grub.cfg`

## Etapa 2: entrada do kernel

Arquivos importantes:

- `src/boot/boot.s`
- `src/boot/interrupt.s`
- `src/boot/linker.ld`

Esses arquivos fazem o minimo necessario para posicionar o kernel corretamente e entregar o controle ao codigo C principal.

## Etapa 3: inicializacao do kernel

O kernel sobe a partir de:

- `src/kernel/kernel.c`

Esse e o coracao do startup.

Ali o sistema:

- imprime banner inicial
- inicializa video
- sobe GDT
- sobe IDT
- prepara memoria fisica e virtual
- prepara heap
- monta initrd
- sobe disco e FAT32
- inicializa timer, tasking e syscalls
- inicializa teclado, mouse e rede
- entra no ambiente principal

No estado atual, esse ambiente principal e o terminal.

---

# Estrutura do Kernel

O kernel do LiwusOS e majoritariamente escrito em C, com pequenas partes em assembly.

Os modulos principais do kernel ficam em:

- `src/kernel`

Arquivos de destaque:

- `kernel.c`
- `gdt.c`
- `idt.c`
- `isr.c`
- `pmm.c`
- `vmm.c`
- `kheap.c`
- `task.c`
- `timer.c`
- `syscall.c`
- `elf.c`
- `string.c`

Cada um cobre um bloco importante da infraestrutura do sistema.

---

# Gerenciamento de CPU e Privilegios

O LiwusOS trabalha com a separacao tradicional:

- kernel mode
- user mode

Essa separacao permite:

- proteger parte da memoria
- manter syscalls como ponto de entrada controlado
- executar programas de usuario de forma mais proxima do que sistemas reais fazem

A transicao entre userland e kernel acontece via syscall.

---

# GDT, IDT e Interrupcoes

## GDT

A Global Descriptor Table define segmentos usados pelo sistema.

Arquivos:

- `include/gdt.h`
- `src/kernel/gdt.c`

## IDT

A Interrupt Descriptor Table define os handlers para:

- excecoes da CPU
- IRQs
- syscall interrupt

Arquivos:

- `include/idt.h`
- `src/kernel/idt.c`
- `src/kernel/isr.c`

## Excecoes

O sistema imprime logs de excecao como:

- `CPU exception: 0x0000000E ...`

Isso foi extremamente importante durante o desenvolvimento.

Boa parte da estabilizacao do OS aconteceu lendo esses faults e corrigindo:

- paginação
- permissao de paginas
- writes em framebuffer
- stacks
- path de scheduler

## IRQs

O timer e outros dispositivos usam interrupcoes para movimentar o sistema.

Sem isso:

- nao ha preempcao
- nao ha agendamento periodico
- nao ha rede responsiva

---

# Syscalls

As syscalls ficam no centro da relacao entre kernel e userland.

Arquivos:

- `include/syscall.h`
- `src/kernel/syscall.c`
- `src/kernel/syscall_stub.s`

As syscalls ja usadas pelo projeto cobrem o suficiente para sustentar:

- stdout
- arquivos
- heap de processo
- framebuffer em casos especificos
- execucao de programas

Com isso, programas nativos conseguem interagir com o kernel sem precisar linkar internamente com ele.

---

# Gerenciamento de Memoria

O LiwusOS separa memoria em alguns blocos conceituais:

- memoria fisica
- memoria virtual
- heap do kernel
- memoria de userland
- mapeamentos especiais, como framebuffer

Esse foi um dos subsistemas mais sensiveis no projeto.

Muitos faults importantes surgiram justamente aqui.

---

# PMM

O Physical Memory Manager fica principalmente em:

- `include/pmm.h`
- `src/kernel/pmm.c`

Ele cuida da alocacao de paginas fisicas.

Sem um PMM confiavel, tudo em cima vira instavel:

- page tables
- heaps
- buffers
- ELF loading

O PMM do projeto evoluiu justamente para sustentar cargas maiores, inclusive o Doom.

---

# VMM

O Virtual Memory Manager fica em:

- `include/vmm.h`
- `src/kernel/vmm.c`

Responsabilidades:

- criar mapeamentos
- mapear paginas
- lidar com espacos de endereco do kernel e userland

Uma correcao importante no projeto foi parar de supor um limite fixo pequeno e mapear corretamente memoria detectada, inclusive acima do que antes era identity-mapped.

Outra correcao importante foi ajustar flags de user/supervisor para paginas de processos.

Sem isso, o userland falhava ja na primeira instrucao de `_start`.

---

# Heap do Kernel

Arquivos:

- `include/kheap.h`
- `src/kernel/kheap.c`

O heap do kernel sustenta estruturas dinamicas internas.

Ele e usado por varios subsistemas:

- filesystem
- networking
- user process setup
- buffers diversos

Como em qualquer kernel, bugs de heap tendem a ser traiçoeiros. Por isso o projeto trata essa camada com bastante cuidado.

---

# Userland

Userland no LiwusOS significa programas que:

- rodam fora do kernel
- usam a libc propria
- acessam recursos via syscall

Isso e importante porque move o projeto de "kernel com demos internas" para "sistema que consegue executar apps de verdade".

Arquivos e diretorios relacionados:

- `sdk/libc`
- `apps/*`
- `src/kernel/elf.c`
- `src/kernel/syscall.c`

---

# Tasks, Scheduler e Context Switch

Tasking no LiwusOS fica principalmente em:

- `include/task.h`
- `src/kernel/task.c`
- `src/kernel/timer.c`

O sistema tem:

- task principal do kernel
- tasks de userland
- agendamento baseado em timer

Ao longo do projeto, o scheduler foi fonte de varios bugs dificeis:

- troca de contexto de kernel threads
- stacks iniciais
- retorno para tarefas
- estados de tarefa

Hoje ele ja sustenta casos reais o bastante para:

- terminal
- execucao de programas
- ports em userland

---

# Execucao de Programas

O LiwusOS suporta execucao de programas carregados como ELF.

Isso envolve:

- localizar o arquivo
- carregar segmentos
- montar stack
- preparar contexto de usuario
- fazer a transicao

No estado atual, essa capacidade ja e usada por:

- Lua
- Doomgeneric
- outros testes e apps

---

# Loader ELF

Arquivos:

- `include/elf.h`
- `src/kernel/elf.c`

O loader interpreta o binario ELF e cria o ambiente de execucao.

Essa camada foi testada na pratica com software real.

Um dos marcos do projeto foi sair de faults em:

- entry point
- page permissions
- stacks

ate conseguir carregar programas maiores corretamente.

---

# Processos e Modelo de Execucao

O modelo do LiwusOS ainda e mais simples que o de Linux ou BSD.

Nao ha pretensao de compatibilidade total com POSIX neste ponto.

Mas ja existe uma base util:

- processos de usuario
- tasks do kernel
- identificacao de nomes
- estatisticas basicas usadas pelo `top`

Esse caminho e suficiente para construir ferramentas internas e, depois, amadurecer interfaces de processo.

---

# Sistema de Arquivos

Hoje o LiwusOS trabalha com duas camadas principais:

1. `initrd`
2. `FAT32`

Entender a diferenca entre elas e essencial.

---

# Initrd

O `initrd` do projeto e empacotado como um tar.

Arquivos:

- `src/fs/initrd.c`
- `include/initrd.h`

Uso:

- entregar programas e assets iniciais junto da ISO
- servir como "live media"
- permitir que o sistema suba mesmo sem depender do disco persistente

Conteudo tipico do initrd:

- `doomgeneric`
- `freedoom1.wad`
- `hello.lua`
- apps de exemplo

Importante:

- o initrd e essencialmente leitura
- ele representa a experiencia "preview/live" do sistema

---

# FAT32

O FAT32 e o disco persistente do LiwusOS.

Arquivos:

- `include/fat32.h`
- `src/fs/fat32.c`

Ele permite:

- montar um volume persistente
- criar arquivos
- ler arquivos
- escrever arquivos
- renomear
- excluir
- criar diretorios

Ao longo do projeto, varias correcoes importantes foram feitas no FAT32:

- formatacao inicial
- layout dos setores
- montagem
- persistencia entre boots
- escrita correta no disco

Hoje ele e o "C:/" do sistema.

---

# Disco Persistente

O disco virtual usado pelo sistema e:

- `liwus_disk.img`

Ele e conectado ao QEMU como disco bruto e montado pelo sistema.

O fluxo atual e:

- se o disco ja esta formatado, o sistema monta
- se nao esta, tenta formatar e montar

Isso permitiu que o OS passasse de um ambiente puramente live para algo com persistencia.

---

# Drivers

Os drivers do projeto ficam em:

- `src/drivers`

Eles cobrem:

- ATA
- GPU/framebuffer
- teclado
- mouse
- PCI
- serial
- RTL8139
- caminhos VirtIO experimentais

---

# Driver ATA

Arquivos:

- `include/ata.h`
- `src/drivers/ata.c`

Esse driver e crucial para:

- ler setores
- escrever setores
- sustentar o FAT32

Houve correcoes importantes aqui em:

- selecao LBA
- flush de escrita
- consistencia do disco

Sem ATA confiavel, o FAT32 simplesmente nao fecha.

---

# Driver de Video

Arquivos:

- `include/video.h`
- `src/drivers/video.c`

Mesmo no estado terminal-only, o driver de video continua importante porque:

- inicializa o framebuffer
- faz desenho basico
- suporta o console visual do sistema

Uma das correcoes mais importantes do projeto foi mapear o framebuffer real informado pelo bootloader, em vez de assumir enderecos fixos.

Isso evitou faults como:

- `cr2=0xFE000000`

---

# Driver de Teclado

Arquivos:

- `include/keyboard.h`
- `src/drivers/keyboard.c`

Esse driver evoluiu bastante.

Hoje ele ja suporta melhor:

- fila de caracteres
- digitação rapida
- layout mais amigavel para teclado brasileiro
- teclas especiais importantes para terminal

Ao longo do projeto foram corrigidos bugs de:

- scancodes estendidos
- estado pressionado/solto
- perda de teclas
- digitação lenta

---

# Driver de Mouse

Arquivos:

- `include/mouse.h`
- `src/drivers/mouse.c`

Mesmo com a GUI pausada como caminho principal, o driver continua no sistema.

No estado atual terminal-only, ele tem menos protagonismo, mas permanece parte da base para futuros retornos de GUI.

---

# Serial

Arquivos:

- `include/serial.h`
- `src/drivers/serial.c`

A serial e uma das ferramentas mais importantes de desenvolvimento no LiwusOS.

Ela permite:

- logs de boot
- logs do kernel
- captura de erros
- depuracao de excecoes
- espelhamento de saida do terminal para copiar e colar do QEMU

Boa parte do desenvolvimento da rede, FAT32 e Doom foi validada pela serial.

---

# PCI

Arquivos:

- `include/pci.h`
- `src/drivers/pci.c`

O PCI e usado para enumerar dispositivos e detectar hardware emulado pelo QEMU.

Exemplos:

- RTL8139
- VirtIO GPU

Logs tipicos:

- `PCI: FOUND VIRTIO DEVICE!`
- `PCI: IT IS THE GPU (VirtIO-GPU)! ...`

---

# RTL8139

Arquivos:

- `include/rtl8139.h`
- `src/drivers/rtl8139.c`

Esse driver hoje e um dos grandes marcos do projeto.

Ele foi evoluido para suportar caminho real de rede com:

- inicializacao da placa
- buffers persistentes de TX
- bus mastering
- transmissao
- recepcao

Isso foi o que permitiu sair de "rede parece existir" para "rede responde ICMP de verdade".

---

# VirtIO e estado atual

Arquivos:

- `include/virtio.h`
- `src/drivers/virtio.c`

O projeto teve varias iteracoes com VirtIO GPU.

Importante:

- o codigo continua no repositorio
- houve tentativas reais de scanout e apresentacao
- mas esse caminho nao e o caminho principal estavel neste momento

A decisao atual foi:

- manter o que ja existe
- nao depender disso no boot padrao
- amadurecer o OS pelo terminal

Isso e muito importante para entender o estado real do projeto.

---

# Pilha de Rede

A rede do LiwusOS fica principalmente em:

- `src/net/net.c`
- `src/net/netstack.c`
- `src/net/tcp.c`
- `src/net/http.c`

Headers:

- `include/net.h`
- `include/netstack.h`
- `include/tcp.h`
- `include/http.h`

Hoje a stack tem partes reais funcionando e partes ainda em amadurecimento.

---

# Ethernet

O sistema constroi e envia frames Ethernet reais pela RTL8139 emulada.

Isso permite comunicar com:

- gateway do QEMU
- hosts de teste dentro do modelo `-net user`

---

# ARP

O ARP foi importante para destravar o `ping`.

Sem ele, o sistema nao saberia:

- qual MAC usar ao enviar para um IP local
- ou qual MAC do gateway usar ao sair da sub-rede

Hoje ha resolucao ARP real na stack.

---

# IPv4

O sistema hoje trabalha com IPv4 basico.

Exemplo validado no QEMU:

- interface `eth0`
- IP `10.0.2.15`
- gateway `10.0.2.2`

---

# ICMP

O comando `ping` do LiwusOS usa ICMP real.

Isso e importante porque valida o caminho inteiro:

- resolucao de MAC
- construcao de pacote
- envio pela placa
- recepcao da resposta
- processamento de frame de rede

Exemplo de output real:

```text
PING 10.0.2.2 (10.0.2.2)
64 bytes from 10.0.2.2: icmp_seq=1 time=0 ms
64 bytes from 10.0.2.2: icmp_seq=2 time=0 ms
64 bytes from 10.0.2.2: icmp_seq=3 time=0 ms
64 bytes from 10.0.2.2: icmp_seq=4 time=0 ms
--- ping statistics ---
4 packets transmitted, 4 received, 0 lost
avg time = 0 ms
```

O `0 ms` e normal dentro da granularidade atual do timer, especialmente sob QEMU local.

---

# TCP

O TCP existe em estado inicial.

A stack ja tem:

- conexao basica
- envio
- recebimento

Mas esse subsistema ainda nao pode ser considerado maduro.

Ele esta sendo usado como base para `wget`, mas ainda precisa evolucao significativa para confiabilidade maior.

---

# HTTP

O HTTP e uma camada acima do TCP e esta em fase experimental.

Hoje o projeto ja tentou um `wget` real, mas o estado deve ser lido com honestidade:

- `ping` esta funcionando de verdade
- `wget` ainda esta em fase de estabilizacao

Se voce ler o repositorio procurando "internet pronta", a interpretacao certa nao e essa.

A interpretacao certa hoje e:

- a infraestrutura de Ethernet/ARP/IPv4/ICMP ja esta real
- o passo seguinte de maturar TCP/HTTP ainda esta em andamento

---

# Terminal como Ambiente Principal

O terminal e hoje a interface principal do LiwusOS.

Isso nao e um modo de emergencia improvisado.

Ele foi assumido como ambiente oficial temporario para:

- reduzir complexidade
- estabilizar o sistema
- aumentar produtividade no core
- criar um OS util mesmo sem GUI

Essa decisao deixa o sistema mais forte para crescer depois.

---

# Shell Interna do LiwusOS

O terminal do LiwusOS nao e apenas um prompt de debug.

Ele ja funciona como shell interna com:

- prompt de caminho
- comandos embutidos
- execucao de programas
- integracao com Lua
- comandos de disco
- comandos de rede
- editor de texto em modo terminal
- `top`

Isso transforma o terminal em um ambiente real de uso do sistema.

---

# Comandos Ja Disponiveis

Os comandos evoluem com o projeto, mas a base atual ja inclui uma combinacao importante de comandos de sistema e arquivos.

Exemplos importantes ja trabalhados no projeto:

- `ls`
- `cd`
- `pwd`
- `touch`
- `mkdir`
- `rm`
- `cp`
- `mv`
- `edit`
- `top`
- `ifconfig`
- `ping`
- `wget`
- `lua`

Isso coloca o LiwusOS num nivel muito mais util do que um terminal puramente cosmetico.

---

# Editor em modo texto

Existe um editor em modo texto embutido no terminal.

Objetivo:

- criar e editar arquivos no `C:/`
- permitir desenvolvimento e teste dentro do proprio sistema

Estado atual:

- util
- funcional para casos basicos
- ainda longe de um Vim completo

Ele e uma ferramenta de produtividade, nao apenas uma demo.

---

# `top`

O `top` do LiwusOS foi pensado para mostrar estado real do kernel e das tasks.

Nao e um "top fake" imprimindo texto solto.

Ele mostra informacoes reais sobre:

- tasks
- estados
- ticks de CPU
- memoria
- scheduler

Isso e excelente para amadurecer o sistema porque oferece observabilidade interna.

---

# Lua como linguagem de script do sistema

Lua foi portada para o LiwusOS com um objetivo claro:

- servir como linguagem de script do sistema

Ela pode funcionar como:

- automacao
- scripts do usuario
- base para apps pequenos
- linguagem de extensao

Isso e estrategicamente muito bom para o projeto porque cria uma camada entre:

- kernel e apps pesados em C
- scripts leves e automacoes em Lua

No futuro, um arquivo `run.lua` pode cumprir o papel que `run.sh` cumpre em outros ambientes, respeitando as limitacoes e APIs do LiwusOS.

---

# Doom como marco tecnico

Portar Doomgeneric foi um marco gigante.

Nao so pelo simbolismo, mas pelo que ele exigiu do sistema:

- memory management
- ELF loading
- syscalls
- input
- arquivos
- framebuffer
- estabilidade em userland

Rodar Doom nao quer dizer que o kernel esta pronto.

Mas quer dizer que a base ja e forte o bastante para sustentar um programa real nao trivial.

---

# Apps do projeto

O repositorio possui varios apps e experimentos em diferentes estagios.

Diretorios relevantes:

- `src/apps`
- `apps/hello`
- `apps/doomgeneric`
- `apps/doomprobe`
- `apps/lua`

Entre eles:

- terminal
- editor
- explorer
- browser
- settings
- calc
- launcher
- welcome
- Lua
- Doomgeneric

Importante:

- nem todos fazem parte do caminho principal atual
- varios refletem fases diferentes da evolucao do OS

---

# GUI antiga, GUI atual e caminho futuro

O projeto teve uma GUI propria importante ao longo do desenvolvimento.

Ela envolvia:

- compositor
- janelas
- taskbar
- explorer grafico
- apps graficos

Esse trabalho nao foi perdido.

Ele continua no repositorio e serve como base de aprendizado e eventual retomada.

Mas hoje o caminho principal nao depende disso.

O terminal-only foi escolhido como caminho principal porque:

- simplifica o sistema
- remove uma fonte gigante de bugs
- acelera evolucao do kernel e userland

---

# Estado do LVGL

LVGL entrou no projeto como possibilidade de substituir ou simplificar a GUI manual.

Houve integracoes e experimentos, inclusive shell grafica baseada em LVGL.

Mas a migracao total ainda nao foi concluida.

Hoje, a leitura correta e:

- ha codigo e configuracao de LVGL no repositiorio
- existe trabalho real nessa direcao
- mas a GUI atual padrao do sistema esta pausada
- o foco foi movido para terminal-only

Isso e importante para qualquer contribuinte nao criar expectativa errada.

---

# SDK e Toolchain

O LiwusOS mantem uma SDK propria.

Isso inclui:

- libc propria
- headers
- syscalls wrappers
- tool de empacotamento

Diretorios:

- `sdk/libc`
- `sdk/tools`

Ferramenta importante:

- `sdk/tools/liw-builder`

Ela ajuda a empacotar apps e formatos usados pelo sistema.

---

# Layout do Repositorio

Visao geral simplificada:

## `include/`

Headers centrais do sistema.

Exemplos:

- memoria
- drivers
- rede
- apps
- compositor
- syscalls

## `src/boot/`

Bootstrap e linker script.

## `src/kernel/`

Kernel core:

- GDT
- IDT
- ISR
- memoria
- tasking
- syscall
- ELF

## `src/drivers/`

Drivers de hardware e dispositivos emulados.

## `src/fs/`

Initrd, FAT32 e VFS.

## `src/net/`

Stack de rede.

## `src/gui/`

GUI propria, LGX e experimentos com LVGL.

## `src/apps/`

Apps embutidos e interfaces de nivel mais alto.

## `apps/`

Ports externos e apps separados do kernel core.

## `sdk/`

Libc e ferramentas do ecossistema do LiwusOS.

## `third_party/`

Dependencias externas, como:

- LVGL
- Doomgeneric
- Lua

---

# Como Compilar

O projeto usa Docker para montar um ambiente consistente com:

- `i686-elf-gcc`
- binutils
- ferramentas de ISO
- QEMU

Arquivos relevantes:

- `Dockerfile`
- `Makefile`
- `run.sh`

Fluxo tipico:

1. build da imagem Docker
2. compilacao do kernel e apps
3. geracao do `initrd.tar`
4. criacao da ISO
5. boot via QEMU

---

# Como Rodar

Em geral, o fluxo principal e:

```bash
sudo ./run.sh
```

Esse script normalmente:

- garante a imagem Docker
- compila o sistema
- gera a ISO
- sobe o QEMU

O QEMU tem sido usado com configuracoes que incluem:

- `-serial stdio`
- `-net nic,model=rtl8139`
- `-net user`

Isso e importante para:

- ver logs do kernel
- testar rede

---

# Como Testar subsistemas

Uma boa forma de trabalhar com o LiwusOS e validar blocos especificos.

## Testar disco

No terminal do LiwusOS:

```text
pwd
ls
mkdir TESTE
cd TESTE
touch ARQUIVO.TXT
ls
```

## Testar editor

```text
edit ARQUIVO.TXT
```

## Testar Lua

```text
lua hello.lua
```

ou:

```text
HELLO.LUA
```

dependendo do nome em FAT32 curto.

## Testar rede

```text
ifconfig
ping 10.0.2.2 4
```

## Testar Doom

O Doom foi portado e usado como prova forte de userland.

O fluxo exato pode variar conforme o estado atual do boot, mas os assets e binarios estao no projeto.

---

# Como depurar pelo log do QEMU

Uma pratica central no projeto e usar:

- serial
- output do QEMU

Isso ajuda em varios casos:

- boot travado
- CPU exception
- mount de FAT32
- rede
- output de apps

Hoje, inclusive, parte da saida do terminal pode ser espelhada na serial para facilitar copia e cola de logs.

---

# Limitacoes atuais

Lista objetiva do que ainda esta incompleto ou experimental:

## Rede

- `ping` funciona
- `wget` ainda esta em amadurecimento
- HTTPS/TLS nao esta pronto

## POSIX

- a libc do LiwusOS ainda nao cobre um POSIX amplo
- muitos programas Linux tradicionais ainda nao vao portar facilmente

## GUI

- GUI principal pausada
- modo terminal-only e o padrao atual
- LVGL ainda nao substituiu o sistema inteiro

## Sistema de processos

- ainda nao ha o mesmo nivel de maturidade de sistemas grandes

## Audio

- nao e um subsistema central consolidado hoje

## Ferramentas

- ainda faltam muitos utilitarios de sistema

---

# Roadmap recomendado

O caminho mais forte para o LiwusOS, dado o estado atual, e algo assim:

## Fase 1: consolidar terminal-first

Prioridades:

- shell melhor
- editor melhor
- `top`, `ps`, `kill`
- utilitarios de disco
- historico e autocompletar
- scripts Lua

## Fase 2: consolidar rede

Prioridades:

- estabilizar TCP
- destravar HTTP
- fazer `wget` confiavel
- DNS
- talvez `netstat`

## Fase 3: consolidar userland

Prioridades:

- mais ports
- mais comandos
- ferramentas de sistema
- melhor experiencia de desenvolvimento dentro do OS

## Fase 4: voltar para GUI

Quando a base estiver madura:

- decidir GUI final
- preferencialmente sobre base mais pronta
- possivelmente com LVGL

Essa ordem reduz retrabalho.

---

# Contribuicao

Quem quiser contribuir deveria idealmente:

1. ler este README inteiro ou pelo menos as secoes relevantes
2. entender o estado atual real do sistema
3. evitar assumir que a GUI antiga ainda e o caminho principal
4. priorizar integridade do kernel e da infraestrutura

Boas areas para contribuicao hoje:

- shell
- editor
- FAT32
- rede
- Lua
- utilitarios
- testes
- documentacao

---

# FAQ tecnico

## O LiwusOS e um hobby OS?

Sim, no modelo de desenvolvimento.

Mas ja nao e um toy OS simplista.

## Rodar Doom significa que o kernel esta pronto?

Nao.

Mas significa que muitas camadas importantes ja funcionam juntas.

## O sistema tem disco persistente de verdade?

Sim, via FAT32 no `liwus_disk.img`.

## O initrd e o sistema de arquivos principal?

Nao.

Ele e o sistema live/preview.

O disco persistente real e o FAT32.

## A GUI ainda existe?

Sim, no codigo.

Mas o caminho principal atual e terminal-only.

## O sistema tem internet?

Tem conectividade Ethernet real suficiente para `ping` via RTL8139.

`wget` ainda esta em amadurecimento.

## Lua e oficial no sistema?

Hoje ela ja e uma linguagem portada e muito promissora para virar linguagem oficial de script do LiwusOS.

---

# Glossario

## Bootloader

Programa que carrega o kernel.

## GRUB

Bootloader usado no projeto.

## Multiboot

Protocolo de boot usado para carregar o kernel.

## GDT

Tabela de segmentos do x86.

## IDT

Tabela de descritores de interrupcao.

## ISR

Handler de interrupcao/excecao.

## PMM

Gerenciador de memoria fisica.

## VMM

Gerenciador de memoria virtual.

## ELF

Formato de executavel usado para userland.

## Initrd

Imagem inicial de arquivos carregada no boot.

## FAT32

Sistema de arquivos persistente atual do disco.

## RTL8139

Placa de rede emulada usada pelo projeto.

## LVGL

Toolkit grafico considerado para GUI futura.

---

# Creditos

O LiwusOS se apoia em trabalho proprio e em componentes externos importantes.

Projetos e bases relevantes no repositiorio:

- Doomgeneric
- LVGL
- Lua
- toolchain GNU
- QEMU
- GRUB

Tambem vale reconhecer o valor da cultura de hobby OS como inspiracao geral:

- OSDev Wiki
- projetos experimentais de kernel
- ports classicos como Doom

---

# Estado Final Resumido

Se voce leu ate aqui e quer a fotografia mais curta possivel do LiwusOS hoje, ela e esta:

- o LiwusOS e um OS experimental x86 com kernel proprio
- ele boota por GRUB
- tem memoria, tasking, syscalls e userland
- usa initrd como ambiente live e FAT32 como disco persistente
- roda em QEMU
- possui terminal como interface principal atual
- ja executa Doom e Lua
- ja tem rede real suficiente para `ping`
- ainda esta amadurecendo TCP/HTTP e a futura GUI

Esse estado ja e muito forte para um projeto desse tipo.

E, mais importante, ele forma uma base excelente para continuar evoluindo com menos adivinhacao e mais engenharia de verdade.

---

# Secao Detalhada Adicional

As secoes acima cobrem a arquitetura e o estado do sistema. A partir daqui, o objetivo e aprofundar ainda mais o documento, em formato mais enciclopedico, para que ele funcione tambem como referencia extensa de manutencao.

---

# Detalhamento Extenso do Boot

O boot de um sistema operacional em x86 costuma parecer simples quando resumido em "GRUB carrega o kernel". Na pratica, mesmo num projeto experimental, existem varias transicoes importantes.

No LiwusOS, vale entender o boot como uma sequencia de contratos:

1. o GRUB aceita o kernel como imagem multiboot
2. o boot assembly posiciona o ambiente inicial
3. o linker script garante layout coerente do binario
4. o kernel recebe informacoes de video e modulos
5. o kernel assume controle do hardware e do estado de CPU

Essa divisao e importante porque ajuda a depurar.

Por exemplo:

- se o kernel nem imprime o banner inicial, o problema provavelmente esta antes do bootstrap em C
- se imprime o banner mas falha em memoria, o problema ja esta depois da transicao do bootloader
- se monta o initrd mas trava em disco, o caminho anterior ja foi validado

Essa visao por etapas permite atacar bugs com mais calma.

---

# Por que terminal-only hoje faz sentido

O LiwusOS passou por uma fase muito rica de GUI propria.

Essa fase foi util porque forçou o sistema a lidar com:

- input grafico
- hit testing
- move e resize de janelas
- composicao
- widgets artesanais
- loops de redraw

Mas, ao mesmo tempo, ela puxou a arquitetura para um custo alto de manutencao.

Uma GUI em um OS hobby nao pesa apenas em "desenhar botoes". Ela afeta:

- agenda de tasks
- politica de repaint
- modelagem de superfícies
- sincronizacao entre input e desenho
- consumo de memoria
- bugs em clipping e coordenadas
- depuracao mais dificil

Ao reduzir o sistema para terminal-only, o projeto ganhou:

- foco
- clareza
- menos ruído visual
- menos superficies de bug
- melhor capacidade de maturar filesystem, rede e shell

Isso nao significa derrota da GUI.

Significa escolha de fase.

---

# O papel do terminal como shell do sistema

Num sistema em evolucao, o terminal pode ser visto de duas formas:

- como fallback temporario
- como ambiente serio de produtividade

No LiwusOS, a segunda leitura e a melhor.

Se o terminal permite:

- navegar no disco
- editar arquivos
- rodar scripts
- inspecionar tasks
- testar rede
- executar programas

entao ele deixa de ser "modo de emergencia" e passa a ser o ambiente principal do sistema.

Isso ajuda inclusive no design futuro:

- quando a GUI voltar, ela voltara sobre uma base forte
- quando novos apps surgirem, eles ja poderao contar com disco, rede, shell e scripting melhores

---

# Sobre o formato 8.3 e nomes em FAT32

Hoje, o FAT32 do LiwusOS ainda trabalha fortemente no universo de nomes curtos.

Isso significa que e comum encontrar arquivos salvos como:

- `ARQUIVO.TXT`
- `HELLO.LUA`

em caixa alta e em formato curto.

Essa nao e uma "conversao para executavel".

E simplesmente o reflexo do estado atual da implementacao do FAT32.

Esse detalhe importa porque afeta a experiencia do usuario e a documentacao precisa ser clara:

- um arquivo `.LUA` continua sendo script Lua
- o nome apenas foi armazenado segundo o formato curto atual

No futuro, suporte a LFN seria um passo natural.

---

# Sobre o `ping` atual

O `ping` ja e uma ferramenta real.

Isso merece ser destacado porque e facil subestimar o trabalho envolvido.

Para o `ping` funcionar, varias coisas precisam dar certo:

1. a placa precisa inicializar
2. o driver precisa transmitir
3. o driver precisa receber
4. o ARP precisa resolver o MAC correto
5. o IP precisa ser montado corretamente
6. o ICMP precisa ser serializado corretamente
7. o timeout precisa ser tratado
8. a resposta precisa ser reconhecida e associada ao request

Quando o projeto passou de `Timeout` para `Resposta recebida` e depois para o output estilo ping com `icmp_seq`, houve um salto qualitativo importante.

Esse tipo de ferramenta tambem ajuda como base para evoluir:

- diagnostico de rede
- latencia
- timers
- confiabilidade de RX/TX

---

# Sobre o `wget` atual

O `wget` foi introduzido com uma ambicao correta:

- trazer uma ferramenta util de internet para o LiwusOS

Mas e importante descrever seu estado com precisao.

Hoje:

- a intencao e real
- o caminho usa TCP/HTTP reais
- o sistema ainda esta estabilizando essa camada

Ou seja:

- nao e um comando de brinquedo hardcoded
- mas tambem nao deve ser anunciado como "internet totalmente pronta"

Essa honestidade e especialmente importante quando o README serve como base para contribuidores.

---

# Sobre o papel do Lua no ecossistema LiwusOS

Lua e especialmente interessante para este projeto por varios motivos:

## 1. Baixo peso

Ela e pequena, embutivel e madura.

## 2. Excelente para scripting

Permite automatizar tarefas do sistema sem criar um shell complexo demais cedo demais.

## 3. Boa ponte entre kernel e usuario

O kernel continua em C, mas varias automacoes e pequenas ferramentas podem migrar para Lua.

## 4. Bom caminho para apps leves

Em vez de cada pequeno utilitario virar um binario C compilado, parte deles pode virar script Lua.

## 5. Base para futuro ecossistema

No futuro, e facil imaginar:

- instaladores em Lua
- tarefas automatizadas em Lua
- setup scripts em Lua
- apps simples em Lua

Isso cria densidade de userland com menos atrito.

---

# Sobre ports em geral

Doom e Lua ja mostraram que portar software real e uma estrategia vencedora.

O valor de um port nao e so "rodar um app famoso". Ele esta em:

- encontrar lacunas de libc
- validar syscalls
- descobrir bugs de paginação
- melhorar filesystem
- exercitar input e output

Por isso o projeto se beneficia muito de ports escolhidos com criterio.

Ports bons sao aqueles que:

- revelam falhas reais
- adicionam utilidade
- forcam a arquitetura a amadurecer

Lua foi excelente nisso.
Doom tambem.

No futuro, outros bons candidatos podem incluir:

- mais ferramentas de terminal
- bancos pequenos
- interpretes
- utilitarios standalone

---

# Sobre a GUI futura

Mesmo com a fase terminal-first, faz sentido registrar uma direcao futura.

Se a GUI voltar, ha duas coisas muito importantes a preservar:

## 1. Nao repetir complexidade desnecessaria

Uma GUI desenhada inteira na unha, sem toolkit e sem uma base muito madura, tem alto custo.

## 2. Aproveitar a base que o terminal construiu

Quando a GUI voltar, idealmente ela encontra um sistema que ja possui:

- disco forte
- shell forte
- scripting
- rede mais madura
- apps uteis

Isso faz com que a GUI seja uma camada de experiencia, nao uma distração que mascara a falta de base.

---

# Mapa narrado do repositiorio

Esta secao funciona como guia de leitura para novos contribuidores.

## `src/kernel/kernel.c`

Ponto de entrada logico do sistema.

Se voce quer entender o startup, comece aqui.

## `src/kernel/task.c`

Ponto central para entender tasks, estados e agendamento.

## `src/kernel/syscall.c`

Ponto central para entender a fronteira kernel-userland.

## `src/kernel/elf.c`

Ponto central para entender execucao de binarios.

## `src/fs/fat32.c`

Ponto central para entender persistencia e disco.

## `src/fs/initrd.c`

Ponto central para entender o lado live do sistema.

## `src/drivers/rtl8139.c`

Ponto central para entender a placa de rede.

## `src/net/netstack.c`

Ponto central para ARP, ICMP e roteamento simples.

## `src/net/tcp.c`

Ponto central para o caminho experimental de TCP.

## `src/apps/terminal.c`

Hoje esse arquivo e muito importante, porque o terminal e a principal interface do sistema.

Ele concentra tanto UX do OS quanto varias ferramentas.

---

# Estrategia de leitura do codigo

Para entender o projeto sem se perder, uma boa ordem de leitura e:

1. `README.md`
2. `src/kernel/kernel.c`
3. `src/kernel/pmm.c`
4. `src/kernel/vmm.c`
5. `src/kernel/task.c`
6. `src/kernel/syscall.c`
7. `src/kernel/elf.c`
8. `src/fs/initrd.c`
9. `src/fs/fat32.c`
10. `src/apps/terminal.c`
11. `src/drivers/rtl8139.c`
12. `src/net/netstack.c`
13. `src/net/tcp.c`

Essa ordem acompanha bem a evolucao logica do sistema.

---

# Sobre depuracao orientada por milestones

Uma pratica que combinou muito com o LiwusOS foi validar milestones pequenos e muito concretos.

Por exemplo:

- "subiu ate VMM initialized"
- "montou initrd"
- "montou FAT32"
- "executou ELF"
- "rodou Lua"
- "pingou o gateway"

Esse estilo de depuracao e poderoso porque reduz ansiedade e reduz chutes.

Em vez de tentar "internet total", valida-se:

- placa subiu
- interface apareceu
- ARP respondeu
- ICMP respondeu
- depois TCP
- depois HTTP

Esse README reforca esse metodo porque ele tem funcionado muito bem no projeto.

---

# Sobre design de APIs proprias

O LiwusOS carrega uma tensao natural de todo hobby OS:

- quanta coisa criar do zero
- quanta coisa portar

A experiencia do projeto ja mostra uma resposta equilibrada:

- criar do zero o que e essencial para entender o sistema
- portar ou adotar o que reduz custo sem matar o aprendizado

Exemplo claro:

- Doom e Lua foram ports muito valiosos
- a GUI totalmente artesanal se mostrou mais cara do que o desejado
- a ideia de LVGL reaparece justamente por isso

Essa maturidade de escolha e um sinal bom do projeto.

---

# Linha do tempo tecnica simplificada

Sem entrar em datas exatas, vale descrever uma linha de evolucao do projeto:

1. boot e kernel basico
2. memoria e interrupcoes
3. framebuffer e interfaces visuais iniciais
4. tasking e syscalls
5. initrd e userland
6. Doom como grande teste de integracao
7. FAT32 persistente
8. shell/terminal mais forte
9. Lua como linguagem portada
10. rede real com RTL8139 e ping
11. pausa estrategica da GUI em favor de terminal-first

Essa narrativa ajuda a entender por que o repositorio tem codigo de GUI antigo e, ao mesmo tempo, boot terminal-only hoje.

---

# Sobre o valor educacional do projeto

Mesmo que o objetivo final seja um sistema mais "usavel", o LiwusOS ja tem muito valor como projeto de engenharia e aprendizado.

Ele ensina, na pratica:

- boot em x86
- paginação
- excecoes
- mapeamento de framebuffer
- drivers simples
- disco em bloco
- FAT32
- agendamento
- syscalls
- userland
- ports
- rede Ethernet

Poucos projetos pequenos conseguem juntar tudo isso de forma integrada.

---

# Recomendações para quem quer hackear o sistema

Se voce quer abrir o projeto e sair alterando tudo, uma pausa util:

comece por uma area pequena e termine.

Boas primeiras frentes:

- adicionar um comando novo ao terminal
- melhorar `top`
- adicionar `cat`
- estabilizar `wget`
- evoluir o editor
- melhorar suporte a nomes no FAT32

Frentes mais arriscadas:

- reescrever scheduler inteiro
- reintroduzir GUI grande cedo demais
- tentar TLS completo cedo

Esse tipo de disciplina faz muita diferenca num OS desse porte.

---

# Cenarios de uso reais do LiwusOS hoje

Mesmo sendo um projeto experimental, ja existem cenarios reais que fazem sentido:

## 1. Laboratorio de kernel

Usar o sistema para aprender OS internamente.

## 2. Laboratorio de userland minima

Testar apps pequenos com a libc propria.

## 3. Ambiente de scripting

Usar Lua como linguagem do sistema.

## 4. Laboratorio de rede

Testar Ethernet/ARP/ICMP/TCP em ambiente controlado com QEMU.

## 5. Plataforma para ports

Trazer programas que forcao melhorias reais da base.

---

# O que seria um grande proximo salto

Se fosse escolher um unico salto grande e muito valioso depois do estado atual, eu apontaria algo assim:

- consolidar um ambiente terminal muito forte
- e destravar um `wget` funcional

Por que isso?

Porque esse par ja abre portas para:

- baixar arquivos
- instalar scripts
- expandir userland
- testar internet real

Em paralelo, evoluir o editor e o shell completaria um ciclo muito poderoso.

---

# Conclusao

LiwusOS nao e apenas um kernel que boota.

Tambem nao e apenas uma GUI experimental.

Ele ja e um sistema operacional experimental com:

- identidade
- userland
- disco
- rede
- scripting
- ports

Ao mesmo tempo, o projeto e disciplinado o suficiente para reconhecer onde ainda falta maturidade.

Essa combinacao de ambicao com honestidade tecnica e, provavelmente, a caracteristica mais forte do projeto hoje.

Se o caminho terminal-first continuar sendo bem executado, o LiwusOS tem uma base excelente para crescer sem perder legibilidade, sem perder controle da stack e sem se afogar em complexidade antes da hora.

Fim do documento.

---

# 🛠️ Apêndice Técnico: Arquitetura da Versão 2.0-brabo (Big Update)

Esta seção documenta a grande refatoração que transformou o LiwusOS em um ambiente de desenvolvimento real.

## 1. O Novo Motor VFS (Virtual File System) unificado

A versão 2.0 eliminou a necessidade de o kernel "adivinhar" de onde vinham os arquivos. Agora, existe uma árvore lógica única que organiza todo o armazenamento.

### Estrutura de Nós (fs_node_t) e Handlers
As estruturas de dados foram expandidas para suportar operações de diretório no estilo Unix:
- **`readdir`**: Handler que permite ao terminal listar conteúdos de pastas de forma genérica.
- **`finddir`**: O coração da navegação, permitindo que o VFS encontre um arquivo descendente sem conhecer o driver subjacente.
- **`Best-Match Mounting`**: O VFS agora gerencia múltiplos sistemas de arquivos montados. Ao abrir `/house/localhost/main.c`, o motor percorre a lista de montagens (`/` e `/house/localhost`) e escolhe o caminho mais específico (o disco FAT32), garantindo que os binários do sistema no Initrd não sejam sobrescritos.

## 2. A Estrada para o C: Engenharia da Libc Mínima

Para rodar programas compilados pelo GCC fora do SO (Passo 2 do plano "brabo"), pavimentamos a estrada com uma biblioteca padrão cirúrgica.

### Arquitetura de Syscalls (INT 0x80)
Padronizamos as chamadas de sistema seguindo o modelo clássico de 32 bits:
- **`write` (ID 4)**: Agora redireciona streams do userland diretamente para o terminal do SO.
- **`brk` (ID 2)**: Fundamental para permitir que o `malloc` funcione. Ela permite que os aplicativos expandam seu espaço de memória sob demanda.
- **`exit` (ID 1)**: Garante o encerramento limpo de processos e a devolução de recursos ao Kernel.

### Formatação de Texto (vsnprintf)
Implementamos um parser de formato nativo que permite o uso de `printf` complexos. Ele suporta `%s` (strings), `%d` (inteiros), `%x` (hexadecimal) e `%c` (caracteres), traduzindo tudo para o driver de vídeo ou porta serial do kernel.

## 3. Liwim: O Editor de Texto Profissional

O Liwim deixou de ser apenas um visualizador e tornou-se um editor funcional inspirado no Vim.

### Máquina de Estados e Comandos
Implementamos uma lógica de processamento de teclas em dois níveis:
1.  **Modo NORMAL**: Suporte a movimentação clássica (`h`, `j`, `k`, `l`), pulo de palavras (`w`, `b`), e comandos de linha potentes como `dd` (deletar linha), `yy` (copiar linha) e `p` (colar abaixo).
2.  **Modo INSERT**: Inserção cirúrgica de caracteres baseada em mapeamento de índices lineares (conversão de X,Y para posição real no buffer).
3.  **Auto-Scroll**: O editor agora monitora a posição do cursor e ajusta a "janela de visão" (viewport) automaticamente, permitindo editar arquivos muito maiores que a tela.

## 4. Shell e Produtividade do Usuário

O terminal do LiwusOS agora oferece uma experiência de sistema operacional maduro.

### TAB Auto-complete
O Shell agora integra-se diretamente ao VFS. Ao apertar **TAB**, o sistema:
- Varre o diretório atual via handlers de VFS.
- Filtra nomes que coincidem com o texto parcial.
- Completa o comando ou nome do arquivo instantaneamente, ou lista múltiplas opções caso haja ambiguidades.

### Suite de Utilidades (As 10 Utils)
Adicionamos ferramentas nativas para observabilidade:
- **`free`**: Relatório de uso de memória física (RAM).
- **`df`**: Estatísticas de espaço em disco e Initrd.
- **`uptime`**: Tempo decorrido desde o boot em segundos reais.
- **`uname`**: Identificação de versão e arquitetura.
- **`whoami`**: Identidade do usuário e host.
- **`pwd`**: Caminho absoluto atual no VFS unificado.

## 5. Persistência e Ciclo de Vida de Dados

A v2.0 resolveu a volatilidade do sistema. Através de ajustes no driver ATA e no script de inicialização `run.sh`, o arquivo `liwus_disk.img` agora preserva seu conteúdo entre reinicializações. O Kernel também ganhou a inteligência de detectar se o disco é novo e formatá-lo automaticamente no primeiro uso, garantindo que o sistema esteja sempre pronto para o usuário.

---
*Manual técnico expandido e atualizado em 13 de Abril de 2026. LiwusOS v2.0.*