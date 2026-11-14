# Decisões de arquitetura da ABI Linux

Este documento fixa as decisões que orientam a compatibilidade Linux x86-64. O código ainda não implementa todas elas. Cada decisão deve ser refletida no gate correspondente do `ROADMAP.md`.

## Fronteira de compatibilidade

LiwusOS permanece um sistema operacional independente. A personality Linux fica isolada em `src/compat/linux/` e nos headers UAPI de compatibilidade; a ABI nativa e seu `int 0x80` continuam no dispatcher nativo. `task_t.abi` seleciona a interpretação do número e dos argumentos. O stub de entrada `SYSCALL`, TLS por thread e a VM são mecanismos arquiteturais genéricos do LiwusOS, reutilizados pela personality; eles não incorporam código nem subsistemas do kernel Linux. O kernel e os programas nativos continuam utilizáveis sem a personality Linux.

## Limites e princípios

- Alvo inicial: ELF Linux x86-64 LP64, em userspace de 64 bits.
- A ABI Linux é uma camada de tradução. VFS, page tables, scheduler, processo e thread são serviços únicos do LiwusOS.
- A ABI nativa do LiwusOS continua válida e documentada separadamente. Números e estruturas Linux não contaminam UAPI nativa.
- Uma tarefa/threads Linux mantém um identificador de ABI; o dispatcher seleciona a ABI antes de interpretar números ou estruturas.
- Erros internos usam valores negativos `-errno`. A borda Linux retorna sempre `-errno` em RAX; a ABI nativa preserva sua semântica documentada.

## Endereçamento: higher-half e direct map

**Decisão:** migrar o kernel para higher-half e criar um direct map físico independente. A identidade 1:1 fica restrita à transição de boot e é removida quando o kernel já opera no mapa definitivo.

- Base do direct map: `0xffff800000000000 + physical_address`.
- Região do kernel: linkada em `0xffffffff80000000` (higher-half); layout final e tamanho são validados no linker script.
- Endereços físicos são valores, não ponteiros C dereferenciáveis fora de helpers que aplicam o direct map.
- Multiboot2 fornece os intervalos físicos utilizáveis. O PMM não impõe teto de 1 GiB; estruturas de boot, módulos, MMIO e kernel são reservados explicitamente.
- User addresses precisam ser canônicos de 48 bits e menores que `0x0000800000000000`. Endereços nulos e a primeira página são inválidos.
- O suporte a LA57 não faz parte do ABI inicial.

## Modelo de processo e thread

**Decisão:** `process_t` é a unidade de recursos e `thread_t` é a unidade de execução. A implementação atual pode ser migrada gradualmente, mas novas interfaces devem respeitar essa separação.

`process_t` possui:

- PID/TGID, processo pai, grupos/sessão e estado de saída;
- `mm` (espaço virtual e lista de VMAs);
- tabela de FDs e cwd, credenciais e umask;
- ações de sinais e estado de sinais compartilhado do grupo;
- ABI selecionada e endereço de `clear_child_tid` quando aplicável.

`thread_t` possui:

- TID, estado de escalonamento, registradores e kernel stack;
- FS base/TLS, contexto FPU e máscara/sinais pendentes próprios;
- referência ao processo proprietário e wait state.

`clone` cria processo ou thread conforme flags e compartilha recursos somente quando a flag correspondente é pedida. `exit` encerra uma thread; `exit_group` encerra todas as threads do processo. `fork` e `clone` chamam as mesmas primitivas de VM e recursos, sem dispatcher alternativo.

**Estado atual (após Incrementos A/B/C da Fase 2):** `process_t` é um objeto separado
que possui PID, pai, mm, heap/brk, mmap cursor, FDs, cwd, ABI, credenciais,
referências e lista de threads. `thread_t` possui TID, estado/contexto do
scheduler, kernel stack, FS/GS-base e contexto FPU; `task_t` é apenas um alias
de compatibilidade de fonte para `thread_t`. Uma thread mantém uma referência
ao processo; o objeto de processo só pode ser coletado sem threads e depois da
remoção da referência da tabela de processos. `fork` cria processo/thread
separados e `waitpid` os coleta. A personality Linux pode criar uma thread com
`CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD`, opcionalmente com
`CLONE_SETTLS`. Para o caminho observado da libc musl, também aceita
`CLONE_SYSVSEM` e `CLONE_DETACHED` (sem efeito porque os recursos
correspondentes não estão implementados), `CLONE_PARENT_SETTID` e
`CLONE_CHILD_SETTID` e `CLONE_CHILD_CLEARTID`. Os endereços de TID são validados; o kernel publica o
TID do pai antes de retornar e registra o clear-TID da filha antes de torná-la
executável. No fim da thread, zera a palavra e acorda um waiter. `gettid`,
`exit` individual e `exit_group` são implementados.
Um probe ET_EXEC freestanding confirma memória compartilhada, retorno do filho,
FS-base diferente no filho e continuidade do processo após o filho encerrar.
Isso ainda não implementa a semântica geral de `clone`, combinações independentes de recursos,
futex compartilhado ou migração SMP. ELFs musl estáticos passaram create/join
com stack fornecida pelo aplicativo, create/join com stack padrão e um teste de
quatro threads com mutex sob SMAP e configuração QEMU de quatro CPUs. Isso não
valida migração entre CPUs; condvars, stress com mais threads e libc geral
seguem sem cobertura. A falha de stack padrão foi corrigida ao implementar
`mprotect(10)`: musl reserva a região como `PROT_NONE`, depois libera a stack
usável antes de copiar TLS. A infraestrutura genérica salva/restaura x87/SSE
com FXSAVE e ativa XSAVE/XCR0 para x87/SSE/AVX quando a CPU anuncia suporte;
AVX-512 e componentes de estado além de XCR0=7 seguem desabilitados.
O PID e TID coincidem enquanto essa limitação existir.

## Espaço de endereçamento e VMAs

**Decisão:** cada `mm` possui uma coleção ordenada de VMAs não sobrepostas, inicialmente uma lista balanceada pode ser adiada até a cardinalidade exigir.

Cada VMA contém `start`, `end` (half-open), `prot`, `flags`, backing (anônimo ou arquivo), offset e referência ao objeto de arquivo. A página presente na page table é cache da VMA; a VMA é a fonte para faults e operações de intervalo.

- `mmap` seleciona endereços livres na faixa alta do userspace; `MAP_FIXED` substitui sobreposições conforme regras Linux após validar o intervalo todo.
- `munmap` divide, remove ou encurta VMAs e desmapeia páginas correspondentes.
- `mprotect` exige intervalo mapeado e aplica proteção por página; NXE fornece proteção de execução. A personality implementa o caso eager-backed atual com divisões de VMA; não há validação de alterações concorrentes nem TLB shootdown remoto.
- Page fault consulta VMA, materializa página anônima zerada ou carrega backing de arquivo, ou entrega SIGSEGV.
- Stack cresce sob demanda com limite configurado e página de guarda não mapeada.
- `fork` compartilha páginas COW com refcounts; escrita cria cópia. VMAs e flags são clonadas como metadados.
- mmap de arquivo privado usa COW; mmap compartilhado grava de volta no objeto/page cache conforme semântica do VFS.
- TLB invalidation é explícita nas mudanças de mapeamento e proteção.

## Tradução de flags e estruturas

**Decisão:** traduzir na borda com tabelas e estruturas UAPI próprias, sem reutilizar macros do host nem redefinir macros globais comuns.

| Interface Linux | Representação interna | Regra |
|---|---|---|
| `O_RDONLY/O_WRONLY/O_RDWR` | modo de acesso do VFS | bits Linux preservados na entrada |
| `O_CREAT` (`0x40`) | criar se ausente | traduzir para flag interna |
| `O_EXCL` (`0x80`) | falhar se já existir | combinar com criação atomicamente |
| `O_TRUNC` (`0x200`) | truncar na abertura | não confundir com flag nativa |
| `O_APPEND` (`0x400`) | append | offset compartilhado na descrição de arquivo |
| `O_CLOEXEC` (`0x80000`) | flag por FD | removido no exec, preservado na descrição |
| `struct stat` x86-64 | `linux_stat64` fixo em UAPI | preencher campos e padding Linux explicitamente |
| `S_IF*` | tipo/permissão VFS | traduzir bits e tipo na borda |
| `dirent64` | entrada de diretório Linux | gerar `d_reclen`, `d_type`, nome e alinhamento Linux |
| `termios` | estado de terminal LiwusOS | traduzir flags e caracteres de controle suportados |

Os valores completos e layouts binários devem ser definidos em headers sob `include/compat/linux/` (ou diretório equivalente aprovado na implementação), com asserts de tamanho/offset no host build. Estruturas Linux não devem ser passadas diretamente ao VFS.

## Política de errno e resultados

- Funções internas retornam zero/valor não negativo em sucesso e `-ERRNO` em falha.
- A fronteira de syscall Linux coloca `-errno` em RAX; não retorna `-1` acompanhado de errno em memória.
- Syscall desconhecida ou recurso não suportado retorna `-ENOSYS`.
- Ponteiro ou intervalo de userspace inválido retorna `-EFAULT`; flags inválidas retornam `-EINVAL`.
- Falta de memória retorna `-ENOMEM`; permissão negada retorna `-EACCES` ou `-EPERM` conforme a operação.
- As rotinas de tradução não convertem errno duas vezes. Um erro positivo só é usado internamente se a API chamada exigir explicitamente essa convenção.

## Entrada de syscall

**Decisão:** syscall x86-64 Linux usa instrução `SYSCALL/SYSRET`, MSRs STAR/LSTAR/SFMASK e a convenção Linux: número em RAX; argumentos em RDI, RSI, RDX, R10, R8 e R9; resultado em RAX. `int 0x80` permanece como entrada nativa existente e pode futuramente receber compatibilidade i386 separada. `SYSRET` só é usado após validar RIP/RFLAGS de retorno; caso contrário, retornar por IRETQ.

Para `futex(202)`, o mask de `WAIT_BITSET`/`WAKE_BITSET` é o sexto argumento Linux em R9; timeout e segundo endereço usam R10 e R8, respectivamente. Não inferir esses registradores da convenção de chamada C do kernel.

O stub salva estado de usuário de modo que interrupções e preempção preservem a mesma estrutura de contexto do scheduler. A implementação não reutiliza RCX como quarto argumento Linux: SYSCALL grava RIP em RCX e RFLAGS em R11.

**Implementado parcialmente:** STAR/LSTAR/SFMASK são configurados por CPU; `SYSCALL` troca GS com `swapgs`, salva RSP de usuário em `cpu_local_t`, muda para a kernel stack da tarefa e constrói `registers_t`. A personality em `src/compat/linux/syscall.c` traduz o subconjunto listado na matriz de status e retorna `-ENOSYS` para operações ainda não suportadas. O retorno usa IRETQ, que restaura CS/SS/RIP/RSP/RFLAGS sem depender de SYSRET; a otimização SYSRET com validação de contexto ainda não foi adicionada. `openat` aceita `AT_FDCWD` e dirfd de diretório para caminhos relativos; caminhos absolutos ignoram dirfd. `faccessat`, `mkdirat`, `unlinkat` e `newfstatat(flags=0)` também usam o resolvedor genérico de caminhos por dirfd. As descrições de diretório guardam um caminho, então renomear/remover o diretório enquanto aberto não segue semântica completa de vnode/dentry. File-backed mmap e várias flags continuam sem suporte.

### Sequência da entrada e invariantes de retorno

- `SYSCALL` entra com RSP ainda apontando para a stack de usuário. O stub não pode usar a pilha antes de trocar para a kernel stack.
- A primeira instrução é `swapgs`, que troca o GS de usuário pelo GS do kernel. O kernel mantém `IA32_KERNEL_GS_BASE` com a base do bloco por CPU; a troca é obrigatória na entrada e na saída, inclusive em NMI/#MC, que usam IST dedicado.
- O bloco por CPU (acessível via GS) contém a tarefa corrente, topo de kernel stack, `user_rsp_scratch` e `cpu_id`. Em syscall, o stub usa a stack de kernel guardada da tarefa; `TSS.RSP0` é mantido coerente para entradas por interrupção.
- Ordem mínima: `swapgs`; salvar o RSP de usuário no bloco por CPU; carregar RSP de `current_thread->kernel_stack_top`; construir o frame completo (RIP, RFLAGS, RCX, R11, argumentos e registradores callee-saved); chamar o dispatcher.
- `TSS.RSP0` aponta para o topo da kernel stack da thread corrente antes de qualquer retorno a ring 3. Ele é atualizado na troca de contexto, não no caminho de entrada.
- `IA32_FMASK` deve mascarar IF, TF, DF e AC, e o kernel assume DF limpo. NMI e #MC usam IST porque `SYSCALL` não os desabilita.
- A stack de usuário não garante alinhamento de 16 bytes no ponto de entrada; o kernel não pode assumir alinhamento nem usar a red zone (compilação com `-mno-red-zone`).
- Com SMAP ativo, cada entrada de exceção/interrupção limpa AC antes do handler. Somente as primitivas de usercopy usam STAC/CLAC, depois de validar o intervalo; carregadores e inicializadores de memória devem escrever pelo direct map antes de mapear frames ou chamar `copy_to_user`.
- #PF usa IST1 e #DF usa IST2, com stacks próprias por CPU. Como essas stacks não pertencem à tarefa, um #PF originado em ring 3 tem o frame completo copiado para a kernel stack da tarefa antes do caminho de término/escalonamento. Stacks de IRQ também têm região própria por CPU; o handler C verifica que executa dentro dela.
- Saída: se RIP e RFLAGS de retorno forem canônicos e seguros (sem TF, DF, AC ou NT), usar `SYSRET` com RCX e R11; caso contrário, montar `IRETQ`. `SYSRET` termina com `swapgs`, e a validação acontece antes de qualquer acesso com GS de usuário.
- O retorno não pode executar `swapgs` duas vezes: interrupções aninhadas já rodam com GS de kernel e stack de kernel.
- `int 0x80` permanece como entrada nativa do LiwusOS. Uma compatibilidade i386 Linux futura exigirá um seletor de ABI por tarefa antes de reutilizar o mesmo vetor.

## O que a compatibilidade Linux não faz

- Não cria um segundo scheduler, VFS, page table manager, fork ou allocator.
- Não executa binários Linux de outra arquitetura ou ABI por tradução de instruções.
- Não promete suporte completo a ioctls, `/proc`, `/dev`, sockets ou syscalls apenas porque há um número Linux reservado.
- Não implementa KPTI sem requisito de ameaça definido; a decisão inicial é adiá-lo e documentar o risco.
- Não inclui suporte i386/`int 0x80`, AF_INET, Wayland, GPU ou Wine nos gates iniciais; esses itens dependem dos gates anteriores.
- Não altera a ABI nativa para acomodar diferenças Linux.

## Evidência do milestone atual

- O ELF `tests/ring3/linux_syscall_probe.S` é um `ET_EXEC` estático, sem `PT_INTERP`, que usa a convenção Linux x86-64 sem caminhos ou chamadas especiais do LIWUS. Ele valida `getrandom` (usa RDRAND somente se CPUID o anunciar; `EIO` sem fallback inseguro, flags limitadas e máximo de 256 bytes por chamada), `close_range` (fechar faixa e `CLOSE_RANGE_CLOEXEC`), `prctl(PR_SET_NAME/PR_GET_NAME)` com truncamento de 15 bytes, erros de ponteiro/opção e nome por thread, estado YMM através de `nanosleep`/troca de tarefa e herança por `fork`/`wait4` quando XSAVE+AVX existem, `getuid/geteuid/getgid/getegid`, `getresuid/getresgid`, `getgroups`, `umask`, `getrlimit`/`prlimit64`, identidade de processo, `getcwd`/`chdir`/`fchdir`, `uname`, relógios e sleeps, `fork`/`wait4`, `access`/`faccessat`, `fstat`/layout `stat`, `openat` e chamadas legadas `open(2)`, `unlink(87)`, `mkdir(83)`, `rmdir(84)` e `rename(82)`; `renameat(264)`/`renameat2(316, flags=0)` cobrem dirfd relativo, `EBADF`/`ENOTDIR`, mesmo caminho, destino existente e flags não suportadas; `openat` (dirfd relativo, caminho absoluto com dirfd inválido, `EBADF`/`ENOTDIR`, modos de acesso, `O_CREAT|O_EXCL`, modo 0600, `O_CLOEXEC`, `O_DIRECTORY` com diretório/arquivo, `ENOTDIR`/`EISDIR`, flags inválidas e `ENOENT`), `pipe`/`pipe2(O_CLOEXEC|O_NONBLOCK)`, `dup`/`dup2`/`dup3(O_CLOEXEC)`, `fcntl` (`F_DUPFD`, `F_DUPFD_CLOEXEC`, `F_GETFD`, `F_SETFD`, `F_GETFL`), `execve` com fechamento dos descritores CLOEXEC no sucesso e preservação após falha, `readv`/`writev`, `pread64`/`pwrite64`, `getdents64`, `mmap`/`mprotect`/`munmap`, TLS FS e `-ENOSYS`. A suíte ring-3 passou após essas alterações: 51 casos de kernel e 23 marcadores ring-3. Os ELFs musl em `tests/linux/` verificam create/join em stacks padrão e fornecida, quatro threads com mutex, condition-variable broadcast/join e `pthread_setname_np`/`pthread_getname_np`. O probe de descritores duplica as pontas de um pipe, fecha o descritor original de leitura e confirma que a cópia ainda lê os mesmos dados.
- `tests/ring3/linux_clone_probe.S` executa `FUTEX_WAIT_PRIVATE` bloqueante e exige que o WAKE do pai retorne exatamente um waiter; valida mismatch `-EAGAIN` em memória gravável e somente leitura, endereço desalinhado `-EINVAL`, ponteiro inválido `-EFAULT`, timeout relativo expirado `-ETIMEDOUT`, `tv_nsec` inválido `-EINVAL`, ponteiro de timespec inválido `-EFAULT` e recusa do modo compartilhado sem backing com `-ENOSYS`. Também registra `set_tid_address` no thread filho e verifica que o kernel zera essa palavra e acorda o pai em seu futex antes de a thread desaparecer. Os probes foram executados em QEMU pela suíte ring-3 nesta revisão.
- A resolução observável de `clock_gettime`/`gettimeofday` é 10 ms. `CLOCK_MONOTONIC` deriva do contador genérico de ticks de 64 bits do timer BSP; `CLOCK_REALTIME` usa o shim de tempo LIWUS (NTP quando sincronizado; caso contrário, valor-base local avançado pelo uptime). Só `CLOCK_REALTIME` e `CLOCK_MONOTONIC` (IDs 0 e 1) estão implementados. `time(201)` retorna segundos `time_t` de 64 bits e copia opcionalmente o valor ao ponteiro informado, com `EFAULT` para destino inválido. A ABI Linux retorna estruturas Linux, mas a fonte e a manutenção do relógio são do kernel LIWUS.
- `getdents64(217)` converte a enumeração genérica da VFS em registros Linux x86-64 de tamanho variável, NUL no nome e `d_reclen` alinhado em 8 bytes. `d_off` é o índice seguinte da enumeração VFS, e o offset compartilhado do open-file-description só avança após copiar o registro. O probe valida alinhamento/NUL, EOF, rewind por `lseek(SEEK_SET)`, `EBADF`, `ENOTDIR`, `EFAULT` e buffer curto (`EINVAL`) sem perder a primeira entrada. Ainda não há teste de chamadas concorrentes compartilhando o mesmo diretório/offset; o teto atual é 32 descritores por processo. Em diretórios, `lseek` aceita SEEK_SET/SEEK_CUR sobre esses cookies, não SEEK_END. `lseek` em arquivos regulares ainda herda o offset nativo de 32 bits.
- `fork(57)` chama o fork genérico do LIWUS, cujo processo filho herda `TASK_ABI_LINUX_X86_64` e retorna 0 no filho. `wait4(61)` suporta PID positivo ou -1 e a opção WNOHANG; códigos de saída normais são limitados a 8 bits em `exit/exit_group` e entregues no formato Linux (`code << 8`). O probe cria o filho, testa WNOHANG, garante que status inválido retorna `EFAULT` sem consumir o filho, valida status/rusage e `ECHILD` após reap. `rusage` é escrito com zeros porque não existe accounting por processo. Não há status de sinal, stop/continue, SIGCHLD ou process groups; opções fora de WNOHANG são recusadas com `EINVAL`.
- `nanosleep` aceita um intervalo relativo de até `UINT32_MAX` milissegundos, arredonda para cima à resolução de 10 ms do scheduler e não pode ser interrompido por sinais enquanto a personality não implementar entrega de sinais. `clock_nanosleep` aceita IDs 0/1 (REALTIME/MONOTONIC), intervalo relativo ou `TIMER_ABSTIME`; deadline absoluto vencido retorna sucesso. O restante do prazo é ignorado e não há EINTR sem entrega de sinais. `sched_yield` chama o yield genérico do scheduler. Esses caminhos foram exercitados pelo probe Linux em QEMU.
- `access(21)`, `faccessat(269)` e `faccessat2(439)` usam helpers genéricos que copiam o caminho do usuário e verificam direitos via `vfs_open_access` com UID/GID reais. `faccessat` e `faccessat2(flags=0)` aceitam `AT_FDCWD`, caminhos relativos ancorados em dirfd de diretório e caminhos absolutos que ignoram o dirfd; os probes cobrem `faccessat2` com flags zero e `EINVAL` para flags não suportadas, além de `EBADF` e `ENOTDIR`. O probe roda como UID 0, então negações por permissões para usuário não privilegiado ainda não foram testadas. `AT_EACCESS`, `AT_SYMLINK_NOFOLLOW` e `AT_EMPTY_PATH` continuam fora do suporte.
- `chdir(80)` usa o helper genérico do kernel e o CWD já pertencente ao `process_t`; o probe confirma caminho relativo até `sys`, leitura subsequente por `getcwd`, `ENOENT` sem alterar o CWD, restauração para `/` e `EFAULT` para caminho inválido em memória. `fchdir`, sincronização concorrente de CWD e paridade completa de erros/permissões ainda não foram validados.
- `fchdir(81)` usa o caminho guardado na descrição de arquivo genérica; a descrição é compartilhada por `dup`/`fork`. O probe muda o CWD para `sys`, abre `/`, volta ao root pelo fd e confirma por `getcwd`; também verifica `EBADF` após fechar o fd e `ENOTDIR` para stdout. Como o VFS ainda não mantém dentry estável, rename/unlink de diretório aberto, mount namespace e concorrência de CWD continuam fora do suporte validado.
- `mkdir(83)`, `rmdir(84)`, `mkdirat(258)` e `unlinkat(263)` resolvem caminhos através das operações genéricas da VFS; as versões legadas usam o CWD, e as formas `*at` aceitam `AT_FDCWD` ou dirfd de diretório. `unlinkat` aceita apenas flags zero ou `AT_REMOVEDIR`. O probe valida criação, `EEXIST`, remoção, `ENOENT`, `EINVAL` para flags inválidas e o ciclo relativo de criar/consultar/remover diretório via dirfd. ELFs estáticos musl e glibc também verificam `mkdir`/`rmdir`. A resolução depende do caminho salvo na descrição do arquivo continuar válido; rename/unlink de diretório aberto não está modelado.
- `newfstatat(262)` aceita `AT_FDCWD` e caminhos relativos por dirfd quando `flags == 0`, usando `sys_stat_at` e a conversão para o layout Linux `struct stat`. O probe valida o modo `S_IFDIR` por caminho relativo. Symlinks, `AT_EMPTY_PATH` e demais flags não estão suportados.
- `statx(332)` escreve o layout Linux x86-64 de 256 bytes e informa `STATX_BASIC_STATS`, com modo/tipo, inode, tamanho, blocos, UID/GID e timestamps aproximados do relógio LIWUS. Aceita `AT_FDCWD`, dirfd relativo e caminho absoluto; `AT_SYMLINK_NOFOLLOW` é equivalente enquanto não há symlinks. Flags `AT_*` restantes retornam `EINVAL`; `AT_EMPTY_PATH`, birth time, mount ID, atributos extendidos e consistência/sync de timestamps ainda não existem. O probe verifica layout, diretório, máscara, `ENOENT`, `EFAULT` e rejeição de flag.
- `fsync(74)`, `fdatasync(75)` e `syncfs(306)` usam callbacks genéricos da VFS. SDFS descarrega alterações pendentes do bitmap e o cache do dispositivo; `fdatasync` usa a mesma operação porque SDFS ainda não separa persistência de dados e metadados; o callback SDFS também descarrega todo o filesystem para `syncfs`. Filesystems voláteis sem callback completam como no-op. Pipes retornam `EINVAL` para sync por descritor e descritores fechados/inexistentes retornam `EBADF`; static musl smoke cobre arquivo, pipe e FD fechado. Isso valida o caminho no QEMU, não queda de energia nem persistência física sob falhas do dispositivo.
- `sched_getaffinity(204)` reporta somente CPU 0, que é a única CPU em que o scheduler atualmente executa tarefas de usuário, ainda que APs estejam inicializados. Aceita pid zero, TID atual ou TGID atual; rejeita pid ausente (`ESRCH`), PID negativo/tamanho curto (`EINVAL`) e ponteiro inválido (`EFAULT`). A saída x86-64 é um bitmask de 8 bytes e o smoke musl valida a máscara atual. Afinidade por CPU e migração SMP permanecem sem suporte.
- `fadvise64(221)` valida descritor, faixa de offset/tamanho e advice Linux `NORMAL/RANDOM/SEQUENTIAL/WILLNEED/DONTNEED/NOREUSE`; aceita advice válido em arquivo regular como no-op porque LIWUS ainda não tem page cache. Pipes retornam `ESPIPE`, descritor fechado `EBADF`, e argumento inválido `EINVAL`. Static musl exercita `posix_fadvise`; hints não alteram política de I/O.
- Os syscalls legados `open(2)`, `unlink(87)`, `rename(82)`, `mkdir(83)` e `rmdir(84)` também são traduzidos para os caminhos genéricos da VFS. `rename(82)` usa o CWD; `renameat(264)` aceita dirfds com caminhos relativos; `renameat2(316)` só aceita flags zero. Essas formas renomeiam arquivos regulares, tratam o mesmo caminho como sucesso sem alteração e retornam `ENOENT` para fonte ausente e `EEXIST` para destino existente; renomeação de diretórios é recusada pela VFS genérica, e flags de troca/não substituição/whiteout não existem. `open` e `openat` aceitam `O_LARGEFILE` como no-op na ABI x86-64 e aceitam `O_NONBLOCK` em arquivo regular: o estado é preservado em `F_GETFL`, embora leitura/escrita de arquivo não precise bloquear. `O_NOFOLLOW` também é aceito sem efeito enquanto o VFS não tiver symlinks; quando suporte a symlink surgir, essa flag precisará ser aplicada na resolução. ELFs estáticos musl e glibc validam criação, escrita, `lseek`, leitura, `stat`/`fstat`/`access`, abertura com `O_NOFOLLOW`, `O_NONBLOCK` em arquivo via `F_GETFL`/`F_SETFL`, `pipe2(O_CLOEXEC|O_NONBLOCK)`, `EAGAIN`, round-trip de pipe, fechamento, renomeação e remoção de arquivo via libc; percorrem `/` com `opendir`/`readdir`/`closedir` (que usa `getdents64`); e criam/consultam/removem um diretório temporário. `O_PATH` e outras flags continuam fora do suporte.
- `fcntl(72)` implementa `F_SETFL` para `O_APPEND` e `O_NONBLOCK` em open-file descriptions regulares, compartilhados por dup e observados por `F_GETFL`; append seleciona EOF antes de gravar, enquanto nonblock é apenas estado para arquivos regulares. Em pipes, `O_NONBLOCK` é estado compartilhado entre dup da mesma ponta: leitura vazia com writers e escrita sem espaço retornam `EAGAIN`; writes não bloqueantes de até `PIPE_BUF` são atômicos e falham inteiros quando não há espaço suficiente; `O_LARGEFILE` enviado pelo wrapper musl em `F_SETFL` é aceito e ignorado como flag imutável. A suíte ring-3 valida abertura/consulta/troca de flags em arquivos, seek+write anexado, pipe vazio/cheio, `EAGAIN`, round-trip e alteração via dup; atomicidade entre escritores concorrentes não foi validada.
- `fstat(5)` traduz os descritores genéricos de arquivo e reporta os dois extremos de pipe como `S_IFIFO`. O probe confirma os tipos `S_IFCHR` para stdout e `S_IFIFO` para ambas as pontas de `pipe2`. Pipes ainda não expõem todos os metadados Linux; `O_DIRECT`/packet mode, `poll`/`epoll` e sockets/outros tipos de descritor não têm tradução.
- `read(0)`/`write(1)` e `readv(19)`/`writev(20)` validam a tabela de descritores e o sentido permitido do pipe antes de chamar a implementação genérica; descritor inválido ou endpoint incompatível retorna `EBADF`, inclusive com vetor vazio. Um retorno interno `-1` após a validação é traduzido para `EINTR` pela personality, mantendo a convenção de errno do Linux.
- `pread64(17)`/`pwrite64(18)` operam sobre arquivos regulares por offset explícito e não mexem no offset compartilhado do open-file description usado por `read`, `write`, `dup` e `fork`. Pwrite estende o arquivo e materializa gaps com bytes zero via VFS; buffers inválidos e offsets negativos são rejeitados. O backend atual limita arquivos/offsets a 32 bits e não há teste de concorrência entre threads.
- `preadv(295)`/`pwritev(296)` e `preadv2(327)`/`pwritev2(328)` usam o offset x86-64 dividido entre `pos_l` e `pos_h`, executam cada vetor pela mesma camada positional e preservam o offset compartilhado. `preadv2`/`pwritev2` aceitam flags zero e offset `-1` para usar o offset compartilhado; flags RWF não implementadas retornam `EOPNOTSUPP`. Suportam até 1024 vetores, retornos parciais no primeiro vetor incompleto e erros para offset negativo/overflow; static musl smoke valida leitura/escrita em dois vetores, offset preservado, offset negativo no syscall legado e validação de flags. Continuam limitados a arquivos regulares, vetores individuais de até `UINT32_MAX` e às limitações de 32 bits do backend; operações com múltiplos vetores não são atômicas.
- `getrandom(318)` aceita `GRND_NONBLOCK` e `GRND_RANDOM`, retorna no máximo 256 bytes por chamada e usa RDRAND apenas quando CPUID anuncia suporte; sem o recurso ou após dez tentativas malsucedidas retorna `EIO`, sem recorrer ao PRNG não criptográfico do kernel. Isso não fornece pool de entropia, bloqueio de inicialização, interrupção por sinal ou paridade completa com Linux.
- `time(201)` retorna o relógio real LIWUS em segundos e aceita `NULL` no argumento de saída. O probe compara retorno e valor copiado e verifica `EFAULT` para um destino inválido; a fonte continua sendo o shim RTC/NTP do kernel LIWUS.
- `close_range(436)` fecha os FDs abertos dentro do intervalo limitado à tabela LIWUS de 32 descritores ou marca-os `CLOEXEC`; buracos e erros individuais de `close` são ignorados. O `CLOSE_RANGE_UNSHARE` que resolve corridas entre threads Linux ainda não está implementado.
- `prctl(157)` implementa somente `PR_SET_NAME` e `PR_GET_NAME` para a thread chamadora; o nome é limitado a 15 bytes mais NUL. Outras operações `prctl` retornam `EINVAL`.
- `getcpu(309)` copia o índice per-CPU retornado pelo estado GS do scheduler e reporta nó NUMA 0, pois LIWUS ainda não tem topologia NUMA. Os ponteiros CPU/node podem ser nulos; ambos os destinos inválidos são testados como `EFAULT`. Isso valida o valor corrente no probe, mas não prova migração concorrente de threads nem hotplug de CPU.
- `execve(59)` encaminha para o carregador ELF genérico, mantendo a personality Linux armazenada no processo. Em ELF64 Linux o stack inicial inclui `argc/argv/envp` e um auxv básico com `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_BASE`, `AT_FLAGS`, `AT_ENTRY`, `AT_BRK`, IDs reais/efetivos, `AT_PLATFORM`, `AT_HWCAP`, `AT_CLKTCK`, `AT_SECURE`, `AT_RANDOM`, `AT_HWCAP2`, `AT_EXECFN` e `AT_NULL`. `AT_HWCAP` reproduz `CPUID.1:EDX`; `AT_HWCAP2` é zero porque LIWUS não habilita MWAIT nem FSGSBASE em ring 3. O probe compara `AT_HWCAP` ao CPUID executado pelo próprio ELF e exige `AT_HWCAP2 == 0`. O probe também confere os valores ET_EXEC, endereço da tabela de program headers, strings, alinhamento do stack, 16 bytes não nulos de `AT_RANDOM` e o terminador. Se a CPU anunciar RDRAND, ele fornece as palavras aleatórias; o fallback do LIWUS sem RNG de hardware é best-effort e **não é criptograficamente seguro**. O ELF de teste usa um PT_LOAD que mapeia os headers e program headers. Quando `LINUX_STATIC_ELF` é fornecido, o probe faz `execve` desse executável estático externo através da personality Linux; o runner valida a saída e status, exercitando stack/auxv com um binário musl não modificado. O probe também valida `FD_CLOEXEC` no sucesso e a preservação dos descritores/imagem após falha. Ainda não há `PT_INTERP`, dynamic linker, vDSO/`AT_SYSINFO_EHDR`, nem cobertura abrangente de rollback; isto não equivale a suporte completo de musl/glibc.

- `AT_BRK` e `brk(12)` têm estado exato, separado dos campos de heap da ABI nativa. No `execve` ELF64, o LIWUS calcula o maior fim de segmento `PT_LOAD` e arredonda para página, como o carregador Linux; `brk(0)` devolve esse valor. Crescimento mapeia páginas zeradas e graváveis sob demanda; encolhimento remove páginas e pode remover a VMA de heap vazia. Pedidos abaixo do início, acima do teto de usuário ou com overflow retornam o break atual, como Linux. O probe testa `AT_BRK`, query, grow através de página, conteúdo zerado, escrita, shrink e pedidos inválidos. A alocação é eager e operações concorrentes de `brk`/`mmap` no mesmo espaço de endereçamento ainda não têm teste de corrida nem lock dedicado.
- A personalidade implementa `FUTEX_WAIT_PRIVATE` com ou sem timeout relativo, `FUTEX_WAKE_PRIVATE`, `FUTEX_WAIT_BITSET_PRIVATE` com deadline absoluto e `FUTEX_WAKE_BITSET_PRIVATE`. WAIT_BITSET usa chave privada `(mm,endereço)`, rejeita máscara zero, trata prazo já expirado como `ETIMEDOUT` e arredonda o prazo futuro à resolução de 10 ms; o clone probe valida uma espera monotônica limitada e wake com máscara correspondente, e o syscall probe verifica prazo expirado em monotonic/realtime e máscaras inválidas. A chave é `(process->mm, endereço virtual alinhado)`; colisões de hash comparam os dois campos. O timeout é arredondado para cima à resolução de 10 ms do timer e limitado a `INT32_MAX` ticks para comparação segura com wrap. `FUTEX_REQUEUE_PRIVATE` e `FUTEX_CMP_REQUEUE_PRIVATE` também estão implementados. Probes verificam requeue sem waiters, comparação divergente (`EAGAIN`) e destino inválido (`EFAULT`); um ELF musl estático com mutex e condition-variable broadcast/join passou no QEMU. Isso é evidência do caminho comum, não valida todas as interleavings, filtros de máscaras incompatíveis sob concorrência, futexes compartilhados entre processos, robust lists ou migração SMP de threads. Usuários dos testes continuam no BSP. O teste não prova paralelismo SMP: usuários executam no BSP; o lock do scheduler torna comparação/enfileiramento atômicos em relação às syscalls concorrentes nesse modelo atual.
- `make smap-test` executa o probe e a suíte nativa/ring-3 no QEMU `-cpu max -smp 4 -m 4096`; a matriz atual contém 51 casos de topo e 23 marcadores ring-3, incluindo a regressão `.lxe`. A evidência cobre SMAP, APIC IDs 1–3, remoção da identidade, frame alocado/acessado/liberado acima de 1 GiB e page fault real na guarda. O agendador executa as tarefas do teste no BSP; os outros processadores são validados na inicialização SMP, não por migração de tarefas entre CPUs.
- É possível fornecer um ELF GNU/Linux estático externo sem incorporá-lo ao repositório: `make smap-test LINUX_STATIC_ELF=/caminho/para/programa`. O ELF é empacotado temporariamente como `linux_glibc_hello` na imagem de teste. O smoke test musl e um smoke test estático glibc compilado a partir de `tests/linux/musl_hello.c` passaram no harness, imprimiram `Hello from an unmodified static GNU/Linux ELF` e terminaram com status 0. Reproduza o musl com `tests/linux/README.md`; o glibc desta máquina foi compilado com `gcc -static -O0 -march=x86-64 -mtune=generic` e executado usando `-cpu max` no alvo `smap-test`.
- A falha glibc anterior em `vpxor` foi resolvida ao habilitar `CR4.OSXSAVE`, selecionar `XCR0=7` e salvar/restaurar x87/SSE/AVX por thread com XSAVE/XRSTOR. O probe ring-3 também mantém um padrão YMM através de um `nanosleep` que troca tarefas. Isso valida esse smoke ELF estático e esse conjunto de estado; não valida `PT_INTERP`, glibc dinâmica, caminhos AVX-512/AMX, nem todos os requisitos de outros binários.
- FS/GS-base são armazenadas por thread e restauradas na troca de contexto. `ARCH_SET_FS/GET_FS`, `ARCH_SET_GS/GET_GS` e `CLONE_SETTLS` passam no probe Linux; o teste verifica valores FS diferentes no pai e no filho e roda com SMAP ativo. O retorno da thread de teste via `exit` deixa o pai continuar; o `exit_group` termina o processo. Um teste musl separado validou um `pthread_create`/`pthread_join` usando stack fornecida pelo programa. Não valida oito threads, todas as corridas de sincronização, entrega de sinais durante interrupções nem migração entre APs.
- A execução dos smokes estáticos musl e glibc cobre apenas `puts`/startup/exit; o pthread create/join é outro teste mínimo. Isso não valida programas complexos, glibc dinâmica, sinais, VM sob demanda ou dynamic linker e não constitui suporte completo Linux.
- O ensaio foi executado com SMAP ativo em QEMU de quatro CPUs; isso valida a inicialização SMP e as rotas testadas sob SMAP. A suíte ainda não migra tarefas de usuário entre APs, então preservação TLS sob migração multicore permanece sem evidência. Também não foi validado TLB shootdown remoto para tasks concorrentes em APs.

## Continuidade e pausa controlada
- **Estado:** Linux personality PAUSADA para expansão. Implementação preservada integralmente.
- **Documentação completa:** Ver `docs/linux-compat/` (HANDOFF, IMPLEMENTATION_STATUS, RESUME_PLAN, KNOWN_ISSUES, REPRODUCIBILITY).
- **Regra de retomada:** Qualquer trabalho futuro na personality Linux deve atualizar a matriz de implementação e seguir o plano ordenado por dependências.
