# Roadmap da ABI Linux

Este roadmap organiza a compatibilidade x86-64 Linux do LiwusOS em gates verificáveis. A compatibilidade traduz a ABI na borda e reutiliza o mesmo motor de processos, memória virtual e VFS do sistema. Cada fase começa somente depois do gate anterior passar.

> **Estado desta revisão:** Gate 0 permanece como marco histórico (`6402de8`). Os passos 9a/9b (`6bbb7cf`, `7fd8184`), 9c–14 e 16 estão implementados; 12–13 incluem VMAs ordenadas, NXE e W^X. O passo 15 foi implementado com stacks de kernel guardadas, stacks distintas por tarefa, IRQ por CPU e IST para page fault/double fault. O passo 17 foi migrado nos caminhos de ELF/VM e nos mapeamentos de kernel/MMIO; wrappers legados ainda existem dentro da camada VMM para operações de boot. A Fase 2 está aberta: Incrementos A/B separam processo e thread; Incremento C implementa wait queues, bloqueio nativo de waitpid/pipes e FUTEX_WAIT/WAKE/WAIT_BITSET/REQUEUE privados, ainda com cobertura incremental. A personalidade Linux passa probes para timeout relativo de FUTEX_WAIT e WAIT_BITSET absoluto, `uname`/`sysinfo`, getters de processo, relógio, `fork`/`wait4` básicos, TID de filho, `pipe`/vectored I/O e `getdents64`. ELFs estáticos `puts` e pthread musl com stack fornecida/padrão também passaram no QEMU. A suíte confirmou 51 casos de topo e 23 marcadores ring-3 com SMAP/4 CPUs. Testes de quatro pthreads com mutex, condvar e nomes de thread passaram; usuários seguem no BSP, bitsets incompatíveis e requeue ainda carecem de stress concorrente. Futex compartilhado, robust lists, sinais e dynamic linking seguem pendentes. O smoke glibc estático x86-64-v3 passa com QEMU `-cpu max`, mas esse ELF executa AVX e falha sob o CPU QEMU padrão sem AVX.

## Fase 0 — Congelar e descrever a base

1. Guardar a versão GUI antiga em uma tag `legacy-gui` apontada para `f15c120`, sem trazê-la de volta para a árvore de trabalho.
2. Consolidar as remoções e modificações da versão enxuta em um commit da branch de trabalho `lean-kernel`.
3. Reescrever `README.md` para descrever apenas o sistema enxuto que existe nesta árvore.
4. Adicionar CI mínima que execute `make`, `make test` e `make ring3-test` em cada push.
5. Documentar números, convenção de chamadas, tipos e errno da ABI nativa em `docs/ABI.md`.
6. Manter este roadmap, `docs/ABI_LINUX.md` e `docs/MM.md` como decisões e gates da compatibilidade.
7. Resolver os warnings existentes e habilitar `-Werror` sem esconder avisos.
8. Retirar binários gerados do índice e manter regras apropriadas em `.gitignore`.

**Gate 0:** árvore limpa após commit, documentação correspondente ao código, CI verde e README fiel à versão enxuta.

## Fase 1 — Endereçamento e proteções

9. Migrar para kernel higher-half e direct map físico separado da identidade temporária de boot.
10. Remover o teto de 1 GiB com PMM alimentado pelo mapa Multiboot2 e bitmap acessível pelo direct map.
11. Definir o espaço de usuário x86-64 canônico, com stack perto de `0x00007fff_ffff_f000`, mmap na faixa alta e PIE perto de `0x00005555_5555_4000`.
12. Introduzir uma lista de VMA por espaço de endereçamento com intervalo, proteção, flags e backing.
13. Habilitar EFER.NXE e aplicar W^X nos mapeamentos de usuário e kernel.
14. Habilitar SMEP e SMAP quando disponíveis; proteger acessos de usercopy com STAC/CLAC.
15. **Implementado nesta revisão:** usar kernel stacks de 16–32 KiB com página de guarda, stack por tarefa para syscalls, stack IRQ por CPU e IST para page faults/double faults. O teste dispara #PF real na guarda e verifica remoção dos mapeamentos ao liberar.
16. Registrar a decisão de adiar KPTI enquanto o modelo de ameaça não exigir isolamento de kernel por CR3.
17. **Implementado para os caminhos em uso:** operações de página recebem `page_directory_t *`; ELF, VM de processo, criação/liberação de stacks e mapeamentos de kernel usam diretório explícito. Wrappers sem `_in` permanecem como conveniência de boot sobre o diretório atual; não são usados pela compatibilidade Linux nem para modificar mm de outro processo.

**Gate 1 — PASSOU nesta revisão:** ET_DYN carregado e executado em `0x00005555_5555_4000`; em QEMU com 4 GiB foi alocado, acessado e liberado um frame acima de 1 GiB; o teste ring-3 confirma NX; as suítes nativa e ring-3 preservaram 45/45. `make smap-test` repetiu a evidência com SMAP e quatro CPUs inicializadas. O log verificável fica em `/tmp/liwusos-smap-integration.serial`. Demand paging, COW e loader `PT_INTERP` são passos posteriores (27–34), não pré-requisitos deste gate. A cobertura não prova todos os endereços até 512 GiB, nem migração de tarefas entre CPUs ou shootdown TLB remoto; o scheduler de userspace continua no BSP.

## Fase 2 — Processos, threads e sincronização

18. **Parcial — Incrementos A/B:** separar `process_t` (PID, relação parental, mm, FDs, cwd, credenciais, ABI, heap, referências e lista de threads) de `thread_t` (TID, scheduler, registradores/contexto, kernel stack, FS/GS e FPU). O scheduler usa o alias de fonte `task_t` para `thread_t`. Há threads simultâneas dentro do mesmo processo, saída individual/grupal, coleta após troca de stack e um teste de processo multithread. Faltam revisão/stress de falhas concorrentes, sinais e bloqueio seguro.
19. **Parcial — Incremento B:** a personality aceita `CLONE_VM|CLONE_FS|CLONE_FILES|CLONE_SIGHAND|CLONE_THREAD`, opcionalmente `CLONE_SETTLS`, `CLONE_SYSVSEM`, `CLONE_DETACHED`, `CLONE_PARENT_SETTID`, `CLONE_CHILD_SETTID` e `CLONE_CHILD_CLEARTID`. O probe freestanding valida stack do filho, retorno 0, memória compartilhada, FS TLS independente, TID do pai, clear-TID/futex wake no exit, `exit` individual e `exit_group`; musl estático passa create/join com stack fornecida e padrão, além de quatro threads com mutex/join. A stack padrão exigiu `mprotect(10)`, agora implementado no subconjunto eager-backed de VMAs. `fork` continua criando processo próprio. Clones de processos com combinações independentes de recursos, rollback/stress maior e cobertura geral de pthreads ainda faltam.
20. **Parcial — Incremento B:** `ARCH_SET_FS/ARCH_GET_FS`, `ARCH_SET_GS/ARCH_GET_GS`, base FS/GS e clone com `CLONE_SETTLS` são armazenados por thread. O probe alterna pai/filho e verifica FS-base distinto sob SMAP. Não há teste de estresse de alternância TLS, interrupções dirigidas a threads, nem migração entre CPUs.
21. Implementar sinais pendentes/bloqueados, `sigaction`, `sigprocmask`, `sigreturn`, `sigaltstack` e entrega síncrona de SIGSEGV/SIGFPE.
22. **Parcial:** credenciais genéricas (`uid/euid/gid/egid`, `umask`) são lidas pela personalidade via getters simples e `getresuid/getresgid` e `getgroups` (lista suplementar vazia); `umask` altera o campo genérico do processo. Os IDs seguem provisionados pelo modelo LIWUS (atualmente zero), e `getgroups` reporta a lista vazia atual. `getrlimit`/`prlimit64` consultam o `RLIMIT_NOFILE` efetivo (32) e recusam alterações. Armazenamento de grupos suplementares e syscalls de alteração de credenciais continuam pendentes.
23. **Parcial:** grupos/sessões, SIGCHLD, `wait4` com status de sinal e estados stop continuam pendentes. A personality Linux implementa `fork(57)` e `wait4(61)` limitado a PID positivo/`-1`, saída normal codificada e WNOHANG; `rusage` retorna zeros enquanto LIWUS não tem contabilização por processo.
24. **Parcial — Incremento C:** wait queues genéricas, estado `TASK_BLOCKED`, wake individual/múltiplo e remoção durante término. `waitpid` nativo, leitor e escritor de pipe usam bloqueio sem polling; os pipes retêm o objeto durante I/O suspenso e acordam em dados, espaço e fechamento. O teste mede ticks de oito waiters durante quatro ciclos de bloqueio/despertar; o scheduler não os escalona enquanto bloqueados.
25. **Parcial — Incremento C:** Linux `FUTEX_WAIT_PRIVATE` e `FUTEX_WAKE_PRIVATE` com chave `(mm,address)`, comparação do valor antes do bloqueio e contagem real de waiters despertados. Timeout relativo de `FUTEX_WAIT` integra o wait queue genérico, arredonda para cima em ticks de 10 ms e limita a duração segura; o probe passou em QEMU. REQUEUE/CMP_REQUEUE privados foram adicionados: comparação opcional e transferência de waiters entre chaves privadas. Probes ring-3 verificam chamadas vazias, mismatch `-EAGAIN` e destino inválido `-EFAULT`; um ELF musl estático com quatro threads, mutex, condition variable e broadcast/join passou em QEMU com SMAP/4 CPUs. Isso não valida todas as corridas/requeue nem migração entre CPUs. `FUTEX_WAIT_BITSET_PRIVATE`/`FUTEX_WAKE_BITSET_PRIVATE` foram adicionados: o probe cobre deadlines absolutos expirados em monotonic/realtime, máscara inválida e uma espera/wake de thread com máscara compatível. Permanecem futex compartilhado entre processos e robust lists; faltam testes de máscaras incompatíveis e stress de timeout/wake/requeue.
26. **Parcial:** `set_tid_address(218)` registra o ponteiro por thread; no exit dentro do mesmo address space o kernel genérico zera a palavra e desperta um waiter futex privado. O probe valida retorno de TID, ponteiro inválido, zero e wake. `set_robust_list` continua pendente; `rseq` pode responder ENOSYS para runtimes que saibam desativá-lo.

**Gate 2:** programa musl estático com oito pthreads, mutex e condvar executa; kill com handler funciona; `wait4` retorna status correto.

**Estado Gate 2:** aberto. Incrementos A–C fornecem threads Linux básicas, TLS por thread, wait queues, espera nativa sem busy-wait e FUTEX_WAIT/WAKE/WAIT_BITSET/REQUEUE privados com timeouts relativo/absoluto e cobertura inicial de bitset/condvar. `fork`, a fatia básica de `wait4` com WNOHANG/status de saída, TID de filho, `pipe`/vetores, `getdents64`, `getcwd`/`mprotect`, o smoke test `puts`, pthread musl com stack fornecida, stack padrão, quatro threads com mutex e broadcast de condvar passaram sob SMAP em configuração QEMU de quatro CPUs. A validação não prova migração de thread entre CPUs. Ainda faltam futex compartilhado, robust lists, validação de máscaras incompatíveis e casos concorrentes abrangentes de requeue, sinais, status de sinal/process groups/SIGCHLD, alteração/grupos de credenciais e stress maior de threads. Não há teste Linux de sinal via `kill` nem validação de rusage real.

## Fase 3 — VM de userspace e loader ELF

27. Implementar demand paging: page fault consulta VMA, aloca página zerada e cresce stack respeitando gap de guarda.
28. Implementar COW no fork com refcount de frames físicos e cópia na primeira escrita.
29. Implementar mmap de arquivo privado/compartilhado com offset e page cache compartilhado.
30. Implementar `mremap`, `madvise` (stub inicialmente) e `MAP_FIXED` com substituição real; `mprotect` tem implementação eager-backed para VMAs mapeadas, ainda sem concorrência/TLB-shootdown validado.
31. **Parcial:** a personality Linux mantém break exato por processo e atualiza a VMA `VMA_HEAP` ao crescer/encolher; não há validação de corrida com `mmap`/threads nem suporte a `RLIMIT_DATA`.
32. **Parcial:** Linux ELF64 recebe auxv básico com AT_PHDR/PHENT/PHNUM/ENTRY/BASE/PAGESZ/BRK/RANDOM/UID/GID/PLATFORM/HWCAP/HWCAP2/CLKTCK/SECURE/EXECFN; probe valida HWCAP contra CPUID.1:EDX, HWCAP2=0, AT_BRK e `brk(0)` mais grow/shrink. PT_INTERP, PT_GNU_STACK, PT_TLS e tratamento mais amplo de headers não mapeados seguem pendentes. Fallback AT_RANDOM sem RDRAND não é criptográfico.
33. **Parcial:** stack inicial Linux ELF64 com argc/argv/envp/auxv e alinhamento de 16 bytes validada pelo probe; completar para libc/dynamic linker e integrar RNG confiável.
34. Fornecer vDSO com clock_gettime, gettimeofday, time e getcpu, anunciado por AT_SYSINFO_EHDR.

**Gate 3:** `hello` linkado estaticamente com glibc executa; em seguida, `ld.so` mapeia libc.so.6 e executa um binário dinâmico.

## Fase 4 — Superfície de syscalls Linux

35. Implementar entrada x86-64 `syscall/sysret` via STAR/LSTAR/SFMASK; manter `int 0x80` i386 somente se WoW64/i386 for priorizado.
36. Criar dispatcher isolado em `compat/linux/`, com números Linux, tradução de argumentos/estruturas e retorno `-errno`; cada processo registra sua ABI.
37. **Parcial:** `openat` aceita `AT_FDCWD` ou dirfd de diretório para caminhos relativos; caminhos absolutos ignoram o dirfd. O syscall legado `open(2)` também está traduzido pelo resolvedor genérico, e as flags x86-64 `O_LARGEFILE`, `O_NONBLOCK` e `O_NOFOLLOW` são aceitas sem efeito de I/O em arquivos regulares; `O_NONBLOCK` é preservado em `F_GETFL`, e `O_NOFOLLOW` só é no-op enquanto VFS não tiver symlinks. A personality valida modos de acesso, `O_CREAT|O_EXCL`, modo/umask, CLOEXEC e `O_DIRECTORY` (incluindo `ENOTDIR`/`EISDIR`). `faccessat`/`faccessat2(flags=0)`, `mkdir(83)`/`mkdirat`, `rmdir(84)`, `unlink(87/at)` e `newfstatat(flags=0)` agora compartilham os serviços genéricos de caminho; `rename(82)`, `renameat(264)` e `renameat2(316, flags=0)` renomeiam arquivos regulares por CWD/dirfd e cobrem mesmo caminho, origem ausente e destino existente; probes ring-3 validam acesso, criação, consulta do tipo de diretório e remoção usando caminho relativo, além de `EBADF`/`ENOTDIR` e caminho absoluto com dirfd inválido. A resolução de dirfd depende do caminho salvo continuar resolvível; renomeação/remoção de diretórios abertos não tem semântica de vnode/dentry. `statx(332)` também produz o layout x86-64 de 256 bytes e `STATX_BASIC_STATS` para caminhos/dirfds comuns, validando layout, tipo, máscara, `ENOENT`, `EFAULT` e flags recusadas; birth time, mount ID, `AT_EMPTY_PATH` e atributos extendidos seguem ausentes. `getdents64` foi implementado na personality Linux com registros variáveis alinhados a 8 bytes e validado para enumeração, EOF, descritor inválido e buffer curto sem consumir entradas. `pread64`/`pwrite64` preservam o offset compartilhado em arquivos regulares e validam leitura/escrita posicional, extensão com zero-fill e offsets inválidos; `preadv`/`pwritev` e `preadv2`/`pwritev2` suportam vetores de até 1024 elementos e offsets posicionais, e `*v2` aceita flags zero e offset -1 para usar o offset compartilhado; flags RWF, atomicidade entre vetores e cobertura concorrente permanecem pendentes. O backend continua limitado a 32 bits. `getrandom(318)` usa RDRAND somente quando anunciado por CPUID, aceita `GRND_NONBLOCK|GRND_RANDOM`, limita a chamada a 256 bytes e retorna `EIO` sem fallback não criptográfico; probes cobrem transferência, flags e ponteiros. `close_range(436)` fecha ou marca `CLOEXEC` na tabela atual e tem probe para buracos, `CLOEXEC` e argumentos inválidos; o limite de 32 descritores permanece e `CLOSE_RANGE_UNSHARE` está pendente. `access`/`faccessat`/`faccessat2(flags=0)` usam o verificador de direitos da VFS; falta validar negação com credenciais não privilegiadas. `chdir` e `fchdir` usam o CWD e descrições genéricas e são verificados por `getcwd`, com a mesma limitação de caminho salvo. `pipe2(O_CLOEXEC|O_NONBLOCK)`, `dup3(O_CLOEXEC)`, `F_DUPFD_CLOEXEC`, `F_GETFD` e `F_SETFD` são validados, junto com `F_DUPFD`/`F_GETFL` e `F_SETFL(O_APPEND|O_NONBLOCK)` em arquivo regular e pipe; `pipe2(O_NONBLOCK)` também tem probes de `EAGAIN`, `dup` e atomicidade até `PIPE_BUF`. `fsync(74)`, `fdatasync(75)` e `syncfs(306)` agora encaminham para callback genérico da VFS; SDFS descarrega bitmap e cache do dispositivo, enquanto filesystems voláteis podem responder como no-op. Static musl valida arquivo, pipe (`EINVAL`) e FD fechado (`EBADF`) no QEMU, sem teste de perda de energia em mídia física. `ftruncate` e demais interfaces ainda pendentes incluem `AT_EMPTY_PATH`, `readlinkat`, renomeação de diretórios, flags de `renameat2` diferentes de zero e `utimensat`.
38. **Parcial:** `fork(57)` usa o `fork_process` genérico do LIWUS; `wait4(61)` suporta PID positivo/`-1`, WNOHANG e status de saída normal Linux. `execve(59)` reutiliza o carregador ELF genérico, faz fork/exec de ET_EXEC estático, valida argv/status e fecha descritores `FD_CLOEXEC` apenas após troca de imagem bem-sucedida. Stack/auxv freestanding inclui AT_BRK/HWCAP e passa o probe Linux; auxv/libc completo, `PT_INTERP`/dynamic linker e rollback abrangente continuam pendentes. `clone(56)` suporta o contrato de thread Linux exercitado pelos probes (TLS, TID, `CLONE_VM` e `CLONE_THREAD`); clone3, combinações gerais de clone, SIGCHLD/sinais, status de término por sinal, grupos de processos e kill/tgkill continuam pendentes. `prctl(157)` cobre somente `PR_SET_NAME`/`PR_GET_NAME`, com herança e isolamento de nome testados.
39. Completar syscalls de memória: mmap/munmap/mprotect/mremap/brk/madvise/msync. `mprotect` já cobre intervalos eager-backed de VMA; demand paging, file-backed mmap e semântica geral de MAP_FIXED ainda faltam.
40. **Parcial:** private futex WAIT/WAKE/timeout, `pipe(22)`, `pipe2(O_CLOEXEC|O_NONBLOCK)`, `readv(19)` e `writev(20)` foram implementados e provados em QEMU. `eventfd`, `timerfd`, `epoll_create1/ctl/wait` e `poll/ppoll` permanecem pendentes.
41. **Parcial:** `time(201)`, `clock_gettime` para REALTIME/MONOTONIC, `clock_getres`, `gettimeofday`, `nanosleep` e o subconjunto `clock_nanosleep` (relativo, deadline absoluto futuro e vencido) passaram probes QEMU; continuam pendentes EINTR/sinais, outros relógios e limites de duração maiores.
42. **Parcial:** `uname`, `sysinfo`, `sched_yield`, `getcpu` (CPU atual e nó NUMA único), `sched_getaffinity` (CPU BSP, somente CPU0), `fadvise64` (hints sem page cache), consulta de `RLIMIT_NOFILE` via `getrlimit`/`prlimit64`, `getrandom` via RDRAND (parcial), `close_range` limitado à tabela de 32 FDs, e `prctl` para nomes de thread foram implementados/probados. Alteração de limites, `personality` e ioctls Linux de terminal permanecem pendentes.
43. Fornecer `/proc` mínimo: self/exe, self/maps, self/status, self/fd, mounts, cpuinfo e meminfo.
44. Fornecer `/dev/null`, zero, full, random, urandom, tty e ptmx/pts; sem PTY, shell interativo não é um alvo suportado.
45. Implementar AF_UNIX antes de AF_INET; rede IPv4 depende de uma stack de rede.
46. Traduzir O_*, `struct stat`, S_IF*, termios e dirent na borda de compatibilidade, sem alterar a ABI nativa.

**Gate 4:** BusyBox estático (sh, ls, grep, vi e wget local), dash/bash e um interpretador (Lua ou Python) executam.

## Fase 5 — Regressão, robustez e métricas

47. Manter matriz de CI para musl estático, glibc estático/dinâmico, threads, sinais, stress de futex/fork/mmap.
48. Implementar ptrace básico, também necessário para depuração e como pré-requisito do Wine.
49. Fazer fuzzing da entrada de syscall e do usercopy.
50. Manter suíte de regressão da ABI nativa para impedir regressões.
51. Medir latência de syscall, tempo de fork e RSS por processo.

**Gate 5:** 72 horas de soak sem panic e nenhuma regressão na suíte nativa.

## Fase 6 — Rede, áudio e gráficos

52. Implementar virtio-net/e1000 e TCP/IP para sockets AF_INET.
53. Adicionar HDA/virtio-sound e API de PCM; compatibilidade com ioctls ALSA é um projeto separado.
54. Adicionar framebuffer/KMS e entrada; mirar um protocolo de superfícies compatível com Wayland para aplicações Linux.
55. Priorizar virtio-gpu com virgl/venus antes de drivers gráficos bare-metal.

## Fase 7 — Windows

56. Tratar Wine como dependente de threads, futex, sinais/SEH, ptrace, `/proc/self/maps`, mmap, 64-bit e, para WoW64, suporte x86.
57. Começar por notepad/winver, avançar para jogos 2D e só depois considerar 3D.
58. Para jogos 3D, avaliar uma VM com passthrough de GPU como caminho pragmático antes de uma implementação nativa.

## Regras de execução

- Um épico por vez. Não iniciar a fase seguinte antes do gate atual.
- Todo gate deve ter uma verificação automatizada que falha antes da implementação e passa depois dela.
- Documentar ABI e memória antes das refatorações de VM e processo.
- Compatibilidade Linux traduz ABI e semântica na borda; nunca cria um segundo caminho de fork, VM ou VFS.
- As estimativas do laudo são planejamento, não compromissos de prazo.

## Próximos incrementos após o milestone atual

Esta sequência prioriza dependências técnicas; os itens não têm estimativa de prazo.

1. Incremento C residual: estressar wake/close concorrentes e repetidos nos pipes, e cobrir encerramento/cancelamento de waiters futex. O teste ring-3 já cobre o kill de um processo com leitor de pipe bloqueado e a liberação de suas referências.
2. Completar completar a cobertura de bitset/requeue além do subconjunto privado implementado; adicionar testes para máscaras incompatíveis e testes concorrentes para chaves iguais/diferentes, timeout, wake e requeue.
3. Definir política de capacidades da CPU e ampliar estado x87/SSE/AVX com save/restore antes de anunciar extensões; repetir ELF Linux externo com alvo ISA suportado.
4. Incremento D: implementar sinais, semântica de `wait4`/SIGCHLD e credenciais, preservando o contrato nativo de `waitpid`.
5. Completar VM Linux que o loader necessita: demand paging, stack/auxv, `PT_TLS`, COW e mmap de arquivo; então validar ELF estático de terceiros sem modificação e registrar os artefatos/hash do teste.
6. Implementar `PT_INTERP` e o carregamento do interpretador dinâmico; validar glibc dinâmica após a execução estática passar.

## Retomada da compatibilidade Linux

- **Status:** RETOMADA por solicitação do mantenedor; ABI nativa Liwus continua primária e inalterada.
- **Localização:** `src/compat/linux/` isolada; testes Linux continuam ativos.
- **Documentação de continuidade:** [docs/linux-compat/HANDOFF.md](docs/linux-compat/HANDOFF.md), [docs/linux-compat/IMPLEMENTATION_STATUS.md](docs/linux-compat/IMPLEMENTATION_STATUS.md), [docs/linux-compat/RESUME_PLAN.md](docs/linux-compat/RESUME_PLAN.md), [docs/linux-compat/KNOWN_ISSUES.md](docs/linux-compat/KNOWN_ISSUES.md), [docs/linux-compat/REPRODUCIBILITY.md](docs/linux-compat/REPRODUCIBILITY.md).
- **Regra:** Adicionar compatibilidade apenas na personality isolada ou em primitivas genéricas justificadas; manter funcionalidade nativa e testes existentes. Diferenciar sempre código implementado de comportamento validado.
