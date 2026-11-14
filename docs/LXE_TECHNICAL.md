# LIWUS `.lxe`: referência técnica para suporte

Este documento descreve como um executável nativo `.lxe` é produzido,
carregado, iniciado e instalado no LIWUS OS. A extensão identifica o artefato
por convenção. **`.lxe` não é um formato de contêiner:** o conteúdo é um ELF
comum.

## Contrato do artefato

O Native SDK produz executáveis com este perfil:

- ELF64 little-endian para x86-64 (`EM_X86_64`), normalmente `ET_EXEC`;
- entrada ELF no símbolo `_start` fornecido pelo `crt0.o` do SDK;
- segmentos `PT_LOAD` estáticos, sem carregador dinâmico;
- texto RX, dados somente leitura R/NX e dados graváveis RW/NX;
- linkagem com a biblioteca estática `libliwus.a` e com o script `elf64.ld`.

O nome terminado em `.lxe` não altera a interpretação do arquivo pelo kernel.
O loader escolhe ELF32 ou ELF64 pelo cabeçalho; o contrato oficial do Native
SDK `.lxe` é ELF64 x86-64. O loader também tem um caminho ELF32 e aceita alguns
ELF64 `ET_DYN`, mas isso não torna executáveis dinâmicos suportados: `PT_INTERP`
é recusado e relocação dinâmica não é aplicada. O perfil portátil para instalar
é o ELF64 `ET_EXEC` estático gerado pelo SDK.

## Produção pelo SDK

Na raiz do repositório, `make sdk` monta `build/liwus-sdk/` e constrói os
programas nativos do projeto. `make sdk-install SDK_PREFIX=/caminho` instala a
interface pública do SDK naquele prefixo: headers, `crt0.o`, `libliwus.a` e
`elf64.ld`.

Um programa C fornece `main(int argc, char **argv, char **envp)`. Exemplo
reproduzível para um arquivo `hello.c`:

```sh
make sdk
SDK=build/liwus-sdk
gcc -std=gnu99 -O2 -Wall -Wextra -Werror -ffreestanding \
  -fno-builtin -fno-pie -fno-pic -fno-stack-protector -m64 -mno-red-zone \
  -I"$SDK/include" -c hello.c -o hello.o
ld -m elf_x86_64 -nostdlib -static --build-id=none \
  -T "$SDK/share/liwus/elf64.ld" -o hello.lxe \
  "$SDK/lib/crt0.o" hello.o "$SDK/lib/libliwus.a"
```

As opções evitam os arquivos de inicialização do host, PIE, stack protector e
red zone incompatíveis com este perfil. O script de link define `_start`,
posiciona o ELF a partir de `0x400000` e separa os segmentos por permissão. Os
headers do SDK são LIWUS (`liwus/...`), não uma declaração de compatibilidade
POSIX ou Linux. Wrappers de syscall retornam valores nativos LIWUS; o chamador
deve tratar valores negativos como falha e usar as constantes `LIW_*` para
flags.

## Inicialização em ring 3

O `crt0.o` contém `_start`. Na entrada, o kernel aponta RSP para a pilha inicial;
`_start` passa esse endereço a `liw_crt_start`, alinha a pilha conforme a ABI
AMD64 e chama `main(argc, argv, envp)`. Quando `main` retorna, o CRT chama
`liw_exit`.

No `execve` nativo, o kernel copia `argv` e `envp`, cria uma pilha de 64 KiB com
topo em `USER_STACK_TOP` (`0x00007ffffffff000`) e coloca nela `argc`, os vetores
terminados por NULL e as strings. Os limites atuais de cópia são 32 posições
por vetor (incluindo a posição terminadora), 256 bytes por string e 8192 bytes
combinados para as strings de argumentos e ambiente. Ao executar um programa
Linux, o mesmo loader recebe uma pilha/auxv Linux própria; isso é controlado
pela personality do processo e não muda a ABI nativa.

Os wrappers nativos em `libliwus/syscall.c` chamam `int 0x80`. O número de
`execve` nativo é `SYS_EXECVE` (`15`); argumentos seguem nos registradores
RDI, RSI e RDX, com argumentos adicionais em R10, R8 e R9. O kernel restaura a
execução no endereço de entrada ELF, em ring 3. A ABI Linux x86-64 usa a
instrução `SYSCALL` e dispatcher separado.

## Carregamento ELF

O fluxo principal fica em `src/kernel/core/syscall.c` e
`src/kernel/core/elf.c`:

1. `execve` resolve o caminho, verifica permissão de execução pela VFS/SDFS e
   lê o arquivo de SDFS ou, quando permitido, do initrd.
2. O kernel identifica a classe ELF. O loader ELF64 valida arquitetura,
   tabela de program headers, limites e offsets dos segmentos, e recusa
   `PT_INTERP`.
3. Para cada `PT_LOAD`, reserva páginas de usuário, copia `p_filesz` bytes e
   zera a cauda até `p_memsz`.
4. Aplica permissões em granularidade de página: usuário pode ler; escrita e
   execução seguem os flags do segmento. Uma página que resulte gravável e
   executável é rejeitada (W^X). O `e_entry` deve cair dentro de um segmento
   executável carregado.
5. Constrói a pilha e só então confirma a nova imagem do processo. Em falha, o
   kernel restaura o espaço de endereçamento anterior. Após sucesso, fecha os
   descritores marcados CLOEXEC.

O intervalo de endereços de usuário é `0x1000` até, exclusive,
`0x0000800000000000`. Para `ET_DYN`, o loader escolhe load bias a partir de
`USER_PIE_BASE` (`0x0000555555554000`); não há relocador dinâmico. A carga de
segmentos é eager: as páginas dos `PT_LOAD` são alocadas durante `execve`.

## Como o shell encontra um `.lxe`

`liwsh` tenta primeiro o nome/caminho exato. Para nomes sem sufixo, tenta
`.lxe`; para nomes sem separador de caminho, procura também em `sys:bin/`,
`/bin/` e `disk:bin/`. Assim, `hello` pode iniciar `/bin/hello.lxe`.
`execve(path, ...)` do kernel recebe o caminho solicitado; a busca por sufixo
e diretórios é principalmente responsabilidade do shell, não uma propriedade
do formato ELF.

Os executáveis do sistema podem vir do initrd e ser copiados para `/bin` no
primeiro boot de um SDFS novo. Uma instalação existente não é sobrescrita pelo
initrd em boots posteriores.

## Instalação de um programa

Há dois fluxos diferentes:

### Imagem SDFS offline (host)

```sh
make app-install APP=hello.lxe DISK=/caminho/liwus.img
make app-install APP=hello.lxe DISK=/caminho/liwus.img REPLACE=1
```

O segundo comando permite substituir um `/bin/hello.lxe` existente. A imagem
deve estar fechada no QEMU. `tools/liwus_sdfs_install.py` edita uma cópia
temporária, valida o SDFS V2 e exige ELF64 little-endian `ET_EXEC` x86-64 sem
`PT_INTERP` ou `PT_DYNAMIC`, verifica segmentos e W^X, grava `/bin/<nome>.lxe`
com modo executável e publica a imagem atualizada. Consulte
[`SDFS_INSTALL.md`](SDFS_INSTALL.md) para limites e recuperação.

### Gerenciador `lpkg` no sistema

O `lpkg` também instala pacotes em `/bin` e mantém seu registro em
`/etc/lpkg.db`. O catálogo pode vir do repositório GitHub ou de um pacote local.
No estado atual, o caminho de download do `lpkg` faz uma checagem inicial da
assinatura ELF, mas não oferece a mesma validação estrutural completa do
instalador offline. A validação final ocorre quando o kernel tenta executar o
arquivo. O transporte HTTPS configurado atualmente aceita qualquer certificado;
não trate downloads como autenticados ou atualizações como seguras para um
sistema de produção.

## Inspeção e diagnóstico

No host, examine o artefato antes de instalá-lo:

```sh
file hello.lxe
readelf -h -l hello.lxe
```

Confirme `ELF64`, little-endian, `Advanced Micro Devices X86-64`, `EXEC`, `_start`,
segmentos `LOAD` e ausência de `INTERP`/`DYNAMIC`. Recompile com o SDK se o
artefato depender de glibc, de um carregador dinâmico, de shared libraries ou
de syscalls Linux.

| Sintoma | Verificação inicial |
|---|---|
| `command not found` | Confirme o arquivo e o caminho `/bin/<nome>.lxe`; teste o caminho completo. |
| Falha ao abrir / `ENOENT` | Verifique SDFS montado, nome/sufixo e se o initrd contém o arquivo. |
| `EACCES` | Confira permissão de execução SDFS e permissões de busca dos diretórios. |
| `ENOEXEC` / ELF inválido | Confira classe, máquina x86-64, `PT_LOAD`, entry point, limites de usuário e ausência de `PT_INTERP`; use `readelf -h -l`. |
| `ENOMEM` ao carregar | O loader aloca todas as páginas dos segmentos e a pilha no `execve`; reduza o tamanho/reserva da imagem ou verifique memória disponível. |
| Executável Linux não inicia | `.lxe` é o nome do formato nativo LIWUS. Para Linux, use um ELF Linux estático e a personality Linux, que tem cobertura parcial e requisitos próprios. |

Para regressão do SDK/loader: `make sdk`, `make test` e `make ring3-test`.
`make app-install-test` cobre instalação persistente em imagem temporária. Estes
comandos testam caminhos diferentes; um build bem-sucedido não prova que uma
imagem específica executa em hardware real.

## Código de referência

- SDK, CRT e script: `programs/native-app.mk`, `lib/liwus/crt0.S`,
  `lib/liwus/crt.c`, `sdk/elf64.ld`.
- Syscalls nativas: `include/uapi/syscall_nums.h`,
  `lib/liwus/syscall.c`, `src/kernel/core/syscall.c`.
- Loader e validação de segmentos: `src/kernel/core/elf.c`.
- Resolução de comando: `programs/liwsh/main.c`.
- Instalação offline: `tools/liwus_sdfs_install.py`.
- Pacotes no OS: `src/kernel/terminal/lpkg.c`.
