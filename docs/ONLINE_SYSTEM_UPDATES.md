# Atualizações completas pela internet

## O que é atualizado

O feed `system-v2.txt` aponta para dois artefatos do mesmo build:

- `hd_boot.img`: imagem híbrida GRUB + kernel, gravada na área reservada do
  disco (`LBA 0..65535`);
- `system-update.tar`: arquivos enviados pela imagem de instalação, sem a
  imagem de boot.

Os dois downloads exigem HTTPS, tamanho e CRC32 corretos. O arquivo TAR é
validado antes de ser extraído; nomes absolutos, `..`, links e tipos de entrada
desconhecidos são recusados. A atualização preserva `/etc`, `/home`, `/var` e
`/.system_installed`. A gravação do boot só ocorre depois da instalação do TAR;
ela é lida de volta e comparada, com tentativa de rollback se a verificação
falhar. Reinicie depois de uma atualização completa.

O atualizador só grava o disco associado à instalação SDFS montada em
`SDFS_HD_PARTITION_LBA`. Em modo live, ramdisk ou montagem USB, recusa atualizar
o boot.

## Progresso durante a atualização

`lpkg sysupdate` e o atualizador gráfico mostram o andamento por fase. O
progresso de rede conta os bytes do corpo recebidos e usa o `Content-Length`
quando disponível; para os dois artefatos principais também usa o tamanho
publicado no manifesto. O terminal atualiza a cada 5% da fase de transferência
e mostra `recebidos/total bytes`; a interface gráfica atualiza a barra global e
o nome da fase.

As fases visíveis são: índice de pacotes, manifesto, imagem de boot, arquivo de
sistema, validação de tamanho/CRC e formatos, instalação dos arquivos, cópia de
segurança dos setores de boot, gravação, releitura/verificação do boot e
finalização. A instalação mostra o número de arquivos concluídos; as operações
de boot mostram setores concluídos conforme o driver de disco os processa.
Fases síncronas curtas aparecem como início/conclusão e não inventam uma
porcentagem interna que o subsistema não fornece.

## Primeiro upgrade e uso

A implementação de OTA completa precisa estar instalada primeiro, pois uma
versão antiga não contém o código para substituir seu próprio kernel. Inicie a
ISO nova uma vez e use “Instalar Sistema”. Se o instalador encontrar uma
partição SDFS existente, ele atualiza os arquivos sem formatá-la e mantém os
diretórios de dados/configuração listados acima. Depois do upgrade, use
`lpkg sysupdate` ou o botão “Atualizacao de Sistema” para receber versões
futuras sem pendrive.

## Publicar o feed de um build

Depois de `make all`, execute `make update-feed`. O diretório
`build/update-feed/` conterá `system-v2.txt`, `hd_boot.img` e
`system-update.tar`. Publique os três na raiz da branch `pkg-repo` do repositório
GitHub; a versão antiga usa `system.txt`, que não deve ser sobrescrita para
evitar que clientes legados marquem um kernel como atualizado sem instalá-lo.

`UPDATE_VERSION`, `UPDATE_DATE` e `UPDATE_BASE_URL` podem ser definidos na
chamada de `make update-feed`. O manifesto gerado contém os CRCs calculados dos
arquivos daquele build.

## Limitação de recuperação

O rollback cobre erros de I/O e divergência detectada durante a mesma sessão.
Uma perda de energia enquanto os setores de boot estão sendo gravados pode
interromper o boot; nesse caso, inicie pela ISO de recuperação e reinstale o
boot. O caminho não é uma atualização A/B transacional.

A extração do TAR também é sequencial. Erro de disco ou falta de espaço pode
deixar parte dos arquivos de sistema da versão nova gravados; mantenha backup
dos dados importantes antes de atualizar. O manifesto e os artefatos usam
HTTPS e CRC32 para detectar falhas de transferência, sem assinatura digital
independente do publicador.

## Publicação 2.2.10 — 2026-11-13

Corrige a descoberta/seleção de rotas de saída analógica no driver HDA e só
marca a saída como ativa quando codec, rota e stream foram detectados. O boot
`hd_boot.img` publicado tem 5.183.488 bytes (CRC32 `1cd197f4`). O
`system-update.tar` não mudou em relação à versão 2.2.9 (CRC32 `0111036c`),
então o feed mantém o mesmo arquivo. Build e validação de tamanho/CRC foram
concluídos; saída de som ainda requer confirmação no PC físico.

## Publicação 2.2.11 — 2026-11-13

Adiciona o comando `screenshot <ip-do-PC> [porta]`, que envia o framebuffer
como BMP por TCP para o receptor `tools/liwus_screenshot_receiver.py`. O boot
publicado tem 5.187.584 bytes (CRC32 `e0c200a3`). O `system-update.tar` não
mudou em relação à versão 2.2.10 (CRC32 `0111036c`). Feed e os dois artefatos
foram baixados da branch `pkg-repo` e validados por tamanho e CRC32 após a
publicação. O teste de captura em hardware ainda depende da atualização e do
uso do comando no LIWUS.

## Publicação 2.2.12 — 2026-11-13

Atualiza a configuração da rota analógica HDA para liberar amplificadores de
entrada nos widgets da rota escolhida (incluindo selectors) e amplificadores de
saída intermediários. O boot tem 5.187.584 bytes (CRC32 `531a1122`); o TAR de
sistema permanece igual (CRC32 `0111036c`). Os artefatos publicados foram
baixados e validados por tamanho e CRC32. A correção ainda precisa de
confirmação auditiva no hardware ALC887.

## Publicação 2.2.13 — 2026-11-13

Atualiza o LiwusWeb com os globais básicos `window`, `self` e `navigator`,
metadados iniciais de `location`/`document.URL`, execução de módulos JavaScript
inline sem imports e seletores CSS `:nth-child()` com fórmulas `an+b`. Imports
de módulos e APIs completas de navegador continuam fora deste incremento. O
`hd_boot.img` tem 5.201.920 bytes (CRC32 `4518d5e1`) e o
`system-update.tar` tem 14.008.320 bytes (CRC32 `d23c3571`).

## Publicação 2.2.16 — 2026-11-14

Publica o emulador SNES e a identificação PCI da GPU no Settings/log de boot,
além do backend de modos EDID/VBE para QEMU Standard VGA. A troca de resolução
em GPU física ainda depende de um driver próprio; esta atualização permite
identificar o adaptador do PC H61 pelo Settings antes de implementar esse
driver. O `hd_boot.img` tem 5.259.264 bytes (CRC32 `0aa1728e`) e o
`system-update.tar` tem 14.376.960 bytes (CRC32 `e18adee4`). Em instalações
com suporte OTA, instale pelo comando `lpkg sysupdate` e reinicie.

## Publicação 2.2.17 — 2026-11-14

Promove a resolução widescreen automática e corrige ícones de fallback e
alinhamento de botões no LiwusWeb. O `hd_boot.img` tem 5.259.264 bytes
(CRC32 `c935ea6d`); o `system-update.tar` permanece com 14.376.960 bytes
(CRC32 `e18adee4`). O feed foi publicado na branch `pkg-repo`. Para receber
esta versão em uma instalação com OTA habilitado, execute `lpkg sysupdate` e
reinicie.

## Publicação 2.2.18 — 2026-11-14

Corrige a ordem dos bits dos modos EDID estabelecidos, permite rolar o conteúdo
do Settings em telas pequenas, impede downgrade pelo LPkg e trata falhas de
redirecionamento e sintaxe incompatível no `liwsh`. O `hd_boot.img` tem
5.263.360 bytes (CRC32 `18a73753`) e o `system-update.tar` tem 14.376.960 bytes
(CRC32 `6dcf7865`). Para receber esta versão em uma instalação com OTA
habilitado, execute `lpkg sysupdate` e reinicie. A publicação 2.2.19 abaixo
inclui a seleção automática do maior modo efetivamente anunciado pelo monitor.

## Publicação 2.2.19 — 2026-11-14

Ao iniciar com o backend EDID/VBE disponível, o desktop agora escolhe o maior
modo anunciado pelo monitor, sem limitar a seleção a uma lista fixa de quatro
resoluções. O `hd_boot.img` tem 5.263.360 bytes (CRC32 `bf3ee3a3`) e o
`system-update.tar` tem 14.376.960 bytes (CRC32 `6dcf7865`). Para instalar via
OTA, execute `lpkg sysupdate` e reinicie.

## Publicação 2.2.20 — 2026-11-14

Remove a resolução fixa do cabeçalho Multiboot e a lista presumida do GRUB. O
bootloader escolhe um modo disponível sem preferência de largura/altura. Se
GRUB entregar 24 bpp e o adaptador oferecer EDID/VBE, o kernel muda para 32 bpp
e o maior modo anunciado antes de iniciar o desktop. O `hd_boot.img` tem
5.263.360 bytes (CRC32 `bbcda097`) e o
`system-update.tar` tem 14.376.960 bytes (CRC32 `6dcf7865`). Para instalar via
OTA, execute `lpkg sysupdate` e reinicie.

## Publicação 2.2.21 — 2026-11-14

Adiciona controle de volume HDA ao Settings, com aplicação imediata e
persistência no SDFS. O carregador aplica a preferência salva depois de montar
o sistema de arquivos. O `hd_boot.img` tem 5.263.360 bytes (CRC32
`a9c4a1c0`) e o `system-update.tar` tem 14.376.960 bytes (CRC32 `6dcf7865`).
Para instalar via OTA, execute `lpkg sysupdate` e reinicie.
