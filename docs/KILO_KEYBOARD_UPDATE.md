# Kilo e layout do teclado

Este patch corrige dois problemas encontrados no uso do terminal:

- O Kilo agora trata gravações parciais, tenta novamente quando a chamada é
  interrompida e mantém o documento marcado como alterado se a escrita ou o
  fechamento falhar. Mensagens de erro incluem o código LIWUS e a quantidade de
  bytes gravada.
- A leitura de `/etc/keyboard.conf` usava os índices numéricos invertidos:
  `US` selecionava ABNT2 e `ABNT2` selecionava US. A seleção agora usa os
  valores nomeados de `keyboard_layout_t` e também reconhece `US_INTL`.
- O comando de configuração do teclado grava a escolha em
  `/etc/keyboard.conf`, quando o SDFS permite escrita.
- O argumento de boot `mode=text` agora força o terminal mesmo quando o GRUB
  fornece framebuffer. A entrada “Modo Texto” passa esse argumento.
- O menu do GRUB oferece variantes de boot US e ABNT2 em modo gráfico e texto;
  a escolha é aplicada antes da inicialização do shell.

## Comportamento de atualização

O novo `lpkg sysupdate` consome `system-v2.txt` e exige três campos do feed:
URL, tamanho e CRC32 para `hd_boot.img` e `system-update.tar`. Ele valida os
dois downloads por HTTPS antes de instalar qualquer um. O TAR atualiza os
arquivos de sistema; `/etc`, `/home`, `/var` e `/.system_installed` são
preservados. Em seguida, GRUB e kernel são atualizados na área LBA 0–65535 do
mesmo disco cuja partição SDFS está montada em LBA 65536. A gravação é lida de
volta e comparada; em erro, o código tenta restaurar a imagem de boot anterior.
É necessário reiniciar para executar o kernel novo.

O primeiro bootstrap ainda exige iniciar a nova ISO. O instalador agora monta
uma instalação SDFS existente sem formatá-la, atualiza os arquivos do sistema
e mantém `/etc`, `/home` e `/var`; uma partição SDFS com erro de corrupção é
recusada para evitar apagamento. Depois desse bootstrap, atualizações completas
podem ser baixadas pela rede sem pendrive. Uma interrupção de energia durante a
gravação do boot pode exigir iniciar novamente pela ISO de recuperação.

`make update-feed` gera `build/update-feed/hd_boot.img`,
`build/update-feed/system-update.tar` e `system-v2.txt` com tamanhos/CRCs do
build corrente. Esses três arquivos devem ser publicados na raiz da branch
`pkg-repo`; a branch antiga continua servindo o `system.txt` legado para que
instalações antigas não interpretem o novo feed como uma atualização de kernel.

## Verificação

`make all` constrói a imagem completa `liwusos.iso`; `make update-feed` cria o
feed com checksums exatos desse build. A validação da aplicação OTA no disco
físico ainda requer uma instalação de teste e um reboot após a atualização.

Em uma instalação persistente, `/etc/keyboard.conf` aceita `US`, `ABNT2` ou
`US_INTL`; essa escolha é carregada no boot seguinte. O comando `kblayout`
grava essa configuração quando executado no terminal de recuperação do kernel.
Também é possível editar o arquivo com o Kilo. Em modo Live, selecione a
variante de teclado no GRUB; o SDFS é um ramdisk e não preserva a configuração
depois do reboot.
