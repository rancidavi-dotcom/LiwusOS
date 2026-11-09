# PC físico de testes — firmware identificado como H61

Registro atualizado em 2026-11-03, a partir das fotos e dos relatos do
mantenedor. Consultar este arquivo antes de perguntar novamente sobre este PC.
Os valores abaixo descrevem o estado observado; não pressupor que opções da
BIOS tenham sido alteradas depois das fotos.

## Identidade e conexão

- É outro computador, diferente do Linux usado para desenvolver o LIWUS.
- O teclado está conectado diretamente a uma porta traseira do PC, segundo
  o mantenedor; não há hub/adaptador externo declarado nesse caminho.
- Fabricante/modelo exato da placa-mãe, CPU, RAM, modelo do teclado e IDs PCI
  do PC de testes ainda não foram identificados.
- O inventário `lspci`/`lsusb` do computador de desenvolvimento não identifica
  este PC. Não atribuir a ele os controladores AMD/NVIDIA vistos no host Linux.

## BIOS/UEFI — foto da aba Main

| Campo | Valor observado |
| --- | --- |
| Interface | Aptio Setup Utility, American Megatrends |
| Copyright exibido | 2012 American Megatrends, Inc. |
| BIOS Vendor | American Megatrends |
| Core Version | 4.6.5.4 |
| Compliancy | UEFI 2.3.1; PI 1.2 |
| Project Version | H61 0.19 x64 |
| Build Date and Time | 07/31/2025 12:01:45 |
| Versão no rodapé | 2.15.1236 |
| System Language | English |
| Access Level | Administrator |
| Data/hora mostradas na foto | Tue 11/03/2026, 17:52:25 |

`H61` é a identificação do projeto de firmware exibida na foto, não um
modelo completo da placa-mãe. A data de copyright não é a data do build.

## BIOS — USB Configuration

| Campo | Valor observado |
| --- | --- |
| USB Module Version | 8.10.27 |
| USB Devices | 2 Keyboards, 1 Mouse, 2 Hubs |
| Legacy USB Support | Enabled |
| USB3.0 Support | Enabled |
| XHCI Hand-off | Enabled |
| EHCI Hand-off | Disabled |
| USB Mass Storage Driver Support | Enabled |
| Port 60/64 Emulation | Enabled |
| USB transfer time-out | 20 sec |
| Device reset time-out | 20 sec |
| Device power-up delay | Auto |

A contagem da BIOS não prova que existam dois teclados físicos nem que o
teclado usado esteja atrás de um hub externo. Dispositivos compostos e a
topologia interna ainda precisam ser identificados.

As opções USB3.0 e XHCI Hand-off existem e estão habilitadas. Isso impede
concluir, apenas pelo nome H61, que a placa possui somente EHCI. Também não
prova, por si só, que haja um controlador xHCI acessível ao LIWUS: falta o
inventário PCI do alvo. A hipótese anterior de EHCI exclusivo não foi validada.

## Resultados reais dos boots

1. A primeira imagem apresentou aviso do GRUB de console indisponível e um
   cursor estático. Depois do ajuste do console, o shell passou a ser visível.
2. Houve inundação de glifos parecidos com P/b sem digitação correspondente.
   A proteção do caminho PS/2 não resolveu. Após a mudança de detecção/entrada
   da UART COM1, o mantenedor confirmou que os caracteres espúrios cessaram.
   Não houve captura elétrica nem registro dos bytes no alvo: o mecanismo
   exato proposto para a UART não foi medido no hardware físico.
3. O shell aparece corretamente, mas as teclas não geram caracteres e o LED
   do Caps Lock não acende. Caps Lock/relatórios USB de LED ainda não estavam
   implementados; a ausência de luz não identifica sozinha a falha de entrada.
4. Depois da correção do campo Max ESIT Payload, o alvo ainda mostrou
   `usb xhci unavailable`.
5. Depois da correção do endereço do registrador de propriedade do firmware
   e da instrumentação do boot, a foto mostrou:

```text
[boot] vfs ok
[boot] pci ok
[boot] usb: no PCI xHCI controller found
[disk] AHCI controller found, bringing up links...
[disk] SATA drive online (port 4)
[boot] disk-hw ok
[boot] fs: mount tried
Formatting disk for first use...
[boot] fs: formatted
WARNING: no disk found; using ramdisk (data lost on reboot)
Installing 20 files to disk...
[16/20] /bin/stat.lxe[boot] sched ok
LiwusOS - ring-3 shell
[init] user process supervisor started
LIWUS OS
Native Shell v0.1
Type help for commands.
liwus>
```

O log confirma shell em ring 3 e fallback para ramdisk. Não demonstra que o
disco SATA físico foi formatado ou que a instalação ficou persistente.

6. Depois de aumentar o inventário PCI geral de 32 para 256 entradas e manter
   uma lista separada de controladores USB, o mantenedor testou novamente. A
   tela ainda mostrou `[boot] usb: no PCI xHCI controller found`. Isso
   descarta, para este teste, o limite original de 32 dispositivos como causa
   observada. O inventário PCI do alvo ainda não está visível no console.

7. Com o inventário PCI USB habilitado, o boot físico mostrou:

```text
[usb-pci] EHCI 00:1a.0 8086:1c2d
[usb-pci] EHCI 00:1d.0 8086:1c26
[boot] usb: no PCI xHCI controller found
```

   A tabela oficial da Intel identifica `D26:F0 / 1C2D` como USB EHCI #2 e
   `D29:F0 / 1C26` como USB EHCI #1. Portanto, a enumeração PCI do firmware
   deste PC apresenta dois controladores EHCI Intel e nenhum xHCI. O driver
   xHCI existente não pode receber teclado por esses controladores. O
   mapeamento entre cada controlador/porta lógica e o conector traseiro do
   teclado ainda não foi medido. Fontes: [Intel 6 Series Chipset Specification
   Update](https://www.intel.com.br/content/dam/www/public/us/en/documents/specification-updates/6-and-c200-chipset-specification-update.pdf)
   e [Intel 6 Series/C200 Datasheet — EHCI](https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/6-chipset-c200-chipset-datasheet.pdf).

8. O primeiro probe somente de leitura dos registradores EHCI mostrou, nos dois
   controladores, `CAP=20`, `HCS=00200002`, `HCC=00036881`, `ports=2` e
   `no connected root ports`. Os BARs têm `MSE=1` e os registradores de
   capacidade retornaram valores coerentes. A Intel documenta que, com o
   Rate-Matching Hub integrado, cada EHCI pode expor a porta lógica 0 como o
   hub interno e a porta 1 como debug; portanto, esse resultado ainda não
   localiza o teclado nem prova que não haja portas USB físicas conectadas.
   Fonte: seção 5.19 do datasheet Intel 6 Series/C200. A foto recebida em
   seguida ainda é dessa imagem somente de leitura; nela é esperado ver
   `CMD=00008000`, `STS=00001000`, `CF=00000000` e `PORT*=00003000`.

9. O driver EHCI foi então avançado para solicitar ownership, parar e resetar
   os controladores, definir `CONFIGFLAG`, habilitar DMA PCI e iniciar uma
   fila assíncrona com um QH de reclamation sem transações. QEMU ICH9 confirma
   `USBCMD.RUN`, `USBCMD.ASE`, `USBSTS.ASS` e chegada ao shell; o teste de
   entrada xHCI com teclado/mouse também continua passando. A imagem foi
   gravada no pendrive `8014CEAF` e comparada byte a byte nos primeiros
   34.338.816 bytes. Ainda falta iniciar essa imagem no PC H61 e fotografar as
   linhas `[ehci]`/`USB EHCI`; nenhum teclado físico foi enumerado até agora.

10. A implementação atual avançou além da fila vazia: faz transfers de
    controle no endpoint zero, configura teclado HID de boot e agenda leituras
    periódicas. `make usb-ehci-start-test` passou com teclado EHCI direto no
    QEMU ICH9; o teste observou relatório HID traduzido para o caminho de
    teclado do kernel. `make test-all` também passou (51 casos de kernel, 23
    marcadores ring-3 e regressão SMAP). Essa evidência não cobre a hub RMH
    Intel `8087:0024`. O próximo passo é inicializar a imagem atual no H61 e
    fotografar as novas linhas
    `[ehci]`/`USB EHCI` e a resposta do teclado.

11. A imagem `liwusos.iso` de 34.349.056 bytes foi gravada no dispositivo
    identificado pelo serial `8014CEAF`; `cmp --bytes=34349056` confirmou
    igualdade byte a byte e o flush do dispositivo terminou. As partições
    resultantes não estão montadas no host. A próxima evidência precisa vir de
    um boot desta mídia no PC H61.

12. A revisão do schedule EHCI encontrou e corrigiu a S-mask ausente nos QHs
    periódicos full/low-speed usados por dispositivos atrás de um TT. O teste
    host `test_usb_ehci_schedule` agora verifica S-mask, C-mask, endereço/porta
    TT e limites de `bInterval`; `make usb-ehci-start-test` e `make test-all`
    passaram depois da correção. A ISO atual foi novamente gravada no mesmo
    serial `8014CEAF` e comparada byte a byte em 34.349.056 bytes. É esta mídia
    revisada que precisa ser testada no H61.

13. Após o handoff, o driver também desabilita as fontes SMI do bloco Legacy
    EHCI para que a configuração USB de legado do firmware não continue
    gerando SMIs durante as alterações de BAR/PCI Command. `make
    usb-ehci-start-test` e `make test-all` passaram após essa alteração. A ISO
    foi gravada novamente e `cmp --bytes=34349056` confirmou os bytes no
    serial `8014CEAF`; esta é a build mais recente para o próximo boot físico.

14. A revisão do caminho de relatórios detectou esperas ocupadas longas dentro
    de `usb_ehci_poll()`, chamado pelo timer. O rearme do schedule periódico foi
    convertido em estados STOPPING/STARTING observados em ticks sucessivos, com
    timeout sem espera bloqueante. `make test-all`, `make usb-ehci-start-test`
    e `make usb-probe-test` passaram depois da alteração. A ISO atual foi
  gravada de novo no serial `8014CEAF` e comparada byte a byte em
  34.349.056 bytes. Falta iniciar essa imagem no H61 e capturar o resultado
  físico.

15. O último boot físico chegou ao shell, mas não aceitou entrada. A foto mostra
    os dois EHCIs ativos (`CMD=00000021`, `CF=00000001`, `ASYNC` válido) e as
    portas 0/1 em `00001000`: alimentadas, sem `CCS` (dispositivo conectado).
    Assim, o driver ainda não alcança o hub RMH `8087:0024` nem chega à
    configuração HID do teclado; o LED apagado é compatível com essa etapa
    anterior à enumeração, e não prova falha na transferência HID.

    A foto também mostrou que o driver escrevia `CONFIGFLAG` antes de iniciar
    o host controller e sua fila DMA. A seção 16.2.2.8 do datasheet Intel diz
    que o software deve defini-lo como última ação de configuração do host.
    A sequência foi corrigida: iniciar o controller e a fila assíncrona,
    definir `CONFIGFLAG` e então ler/servir as portas. `make test-all` e
    `make usb-ehci-start-test` passaram com a nova ordem. Esta correção ainda
    precisa de um boot físico para saber se o `CCS` do RMH passa a aparecer.
    A ISO resultante foi gravada no pendrive `8014CEAF`; `cmp --bytes=34349056`
    confirmou a cópia integral e o flush terminou.

## Diagnóstico pendente e continuidade

O perfil `make run` usa QEMU `pc-q35-11.1`/ICH9, sem i8042, com dois EHCIs e
teclado/mouse USB ligados ao EHCI. O teste EHCI também usa dois controladores
nas posições PCI `00:1a.0` e `00:1d.0`. Isso aproxima a topologia que foi
observada neste H61 e remove xHCI/PS2 da regressão interativa. Não é uma
emulação fiel do PCH H61: QEMU reporta seus dispositivos ICH9 e possui seis
portas EHCI; seu `usb-hub` embutido é full-speed e não conecta à raiz EHCI
high-speed, então o RMH integrado não pode ser reproduzido com esse dispositivo.

O boot físico posterior à correção de ordem de CONFIGFLAG continuou mostrando
`PORT0=00001000` e `PORT1=00001000` em ambos os controladores, sem teclado.
Essa alteração não resolveu a detecção. A próxima imagem aguarda 100 ms após
CONFIGFLAG antes de consultar as portas, pois a varredura anterior era imediata;
isto é uma hipótese de temporização, ainda não validada no H61. O teste EHCI
no QEMU passou, e a ISO foi gravada e comparada no pendrive `8014CEAF`.

A foto seguinte mostrou `PORT0=00001803`, seguido por `PORT0=00001007` após
reset, e `USB EHCI: root device enumeration failed`. Correção de leitura do
registrador: em EHCI PORTSC, bits 11:10 são Line Status, não um indicador de
velocidade. Portanto `0x1803` significa conectado (`CCS`), mudança de conexão
pendente (`CSC`), alimentação ligada (`PP`) e estado de linha `2`; isso não
identifica por si só a velocidade do dispositivo. O dado firme é que a porta
conecta e o erro vem na enumeração do dispositivo hub antes do teclado HID.

- O bloqueio observado ocorre na procura PCI por xHCI, antes da configuração
  do teclado. Ajustes de endpoint e handoff não explicam esse resultado.
- O inventário PCI geral agora guarda até 256 dispositivos, e controladores
  USB têm uma lista dedicada de até 32 entradas. Aumentar esse limite não
  alterou o resultado neste PC.
- O inventário físico confirmou dois controladores EHCI Intel. A imagem atual
  solicita handoff, reseta o host, inicia a fila DMA e só então define
  `CONFIGFLAG`. Próximo passo: iniciar o pendrive atualizado com o teclado
  conectado e fotografar as linhas `[ehci]`, `[ehci-ro]` e `USB EHCI`. Isso
  mostrará se o RMH aparece conectado após a sequência corrigida.
- O driver atual usa o primeiro xHCI encontrado; hubs, seleção de múltiplos
  controladores e hot-plug não estão completos. Não confundir sucesso no
  QEMU com validação do teclado deste PC.
- Não recomendar novas alterações de BIOS sem relacioná-las ao inventário
  real. Os valores já fotografados acima não precisam ser perguntados de novo.

## Confirmação de Funcionamento em Hardware Real (2026-11-09)

A falha de DMA (PCI Received Master Abort `PCI_STS=2290` e Host System Error
no EHCI) foi corrigida:
- As estruturas `ehci_qtd_t` e `ehci_queue_head_t` foram expandidas para o
  layout de 64 bits (`extended_buffer[5]`), respeitando a especificação EHCI 1.0
  (Tabela 3-17) para controladores com `HCCPARAMS` bit 0 ativo (como o PCH H61).
  Anteriormente, o qTD contíguo seguinte (`qtd[1]`) colidia no offset 0x20 do
  `qTD[0]`, gerando um endereço alto espúrio no barramento PCI.
- O registrador `CTRLDSSEGMENT` passou a ser zerado na inicialização.
- O campo `capabilities` da QH de controle passou a ter `MULT=0` conforme a
  especificação de transferências de controle/bulk.

Após a gravação da imagem no pendrive `8014CEAF`, o boot no computador físico H61
foi concluído com sucesso:
- O Rate Matching Hub integrado (Intel `8087:0024`) foi enumerado via EHCI.
- As portas do hub foram varridas, e o teclado USB HID foi configurado e
  associado ao driver de teclado.
- **Resultado real confirmado pelo mantenedor:** entrada de teclado plenamente
  funcional e responsiva no shell ring-3 (`liwsh`) em bare-metal.

## Otimizações de Vídeo e Refinamento do Teclado (2026-11-09)

1. **Aceleração do Framebuffer & Scroll:**
   - Adicionado shadow buffer (`vga_backbuffer`) no heap do kernel (RAM da CPU),
     eliminando as leituras síncronas não-cacheadas via barramento PCIe ("efeito CRT
     de baixo para cima").
   - `fast_memcpy` (instruções SSE2 de 64 bytes por iteração com *stream store*)
     usado para transferir dados da RAM para a VRAM na operação de scroll.
   - `memmove` otimizado para blocos de 64 bits (`uint64_t`).
2. **Eliminação de Travadas no EHCI e Atualização Atômica de qTD:**
   - Eliminado o desligamento contínuo de `PERIODIC_ENABLE` no `USBCMD`. O EHCI
     agora mantém o schedule periódico ativo ininterruptamente, e a reativação
     da Queue Head é feita de forma atômica atualizando apenas `qh->next_qtd`
     (conforme Seção 4.10.2 da especificação Intel EHCI 1.0).
   - Adicionada exclusão mútua (`ehci_poll_active` com `cli`/`sti`) em `usb_ehci_poll`,
     eliminando colisões entre a IRQ do timer e `sys_read`.
   - Isso eliminou as pausas/congelamentos no meio da digitação e garantiu que
     o relatório de soltura da tecla seja capturado em 1 ms, permitindo apagar
     com toques sucessivos de Backspace sem falhas.
3. **Backspace Estrito de 1 Caractere (Sem Auto-Repeat):**
   - Removida qualquer repetição automática por software em `keyboard.c`. Cada toque
     no Backspace apaga rigorosamente 1 único caractere, evitando repetições
     acidentais ou loops indesejados.
4. **Resolução Dinâmica e Tela Cheia no `edit`:**
   - `TIOCGWINSZ` atualizado no kernel para retornar as dimensões reais do console.
   - Removido o teto artificial de 80x25 no editor `edit`, expandindo a interface
     para ocupar 100% da tela do monitor físico.

## Mídia de teste e evidências

O pendrive usado para transportar as imagens foi identificado no host Linux
como Flash Disk, serial `8014CEAF`, capacidade aproximada de 7,3 GiB (8 GB
comerciais). Ele apareceu como `/dev/sdd` nas gravações anteriores. Esse nome
é transitório: sempre verificar identidade e montagens antes de gravar.

Fotos fornecidas na conversa (caminhos temporários, não arquivos permanentes
do repositório; as informações relevantes foram transcritas acima):

- Boot com ausência de xHCI:
  `/tmp/codex-clipboard-f8c27037-ef2c-40b3-9b47-b9dcd48dd16e.png`
- BIOS Main:
  `/tmp/codex-clipboard-5f777188-7e24-434a-bf00-d3702535a40e.png`
- BIOS USB Configuration:
  `/tmp/codex-clipboard-eacf7d08-568e-43a5-b2a5-864fc1c048e9.png`
