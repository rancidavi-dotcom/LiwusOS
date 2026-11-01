# Roadmap da ABI Linux

Este roadmap organiza a compatibilidade x86-64 Linux do LiwusOS em gates verificáveis. A compatibilidade traduz a ABI na borda e reutiliza o mesmo motor de processos, memória virtual e VFS do sistema. Cada fase começa somente depois do gate anterior passar.

> **Estado atual:** Gate 0 concluído (`6402de8`); passos 9a/9b (`6bbb7cf`, `7fd8184`) e 9c/10/11 concluídos nesta branch. Passos 12 e 13 implementados no escopo atual: cada diretório possui lista VMA ordenada integrada com ELF, stack, heap, mmap, munmap e fork; EFER.NXE, proteção de imagem do kernel e W^X em ELF/mmap. Passo 14 implementado: SMEP/SMAP são ligados por CPU quando disponíveis; cópias de usercopy usam STAC/CLAC e caminhos de ELF, `brk`, `mmap` e construção de stack foram ajustados para não escrever diretamente em páginas user. `make test`/`make ring3-test` passam 43/43; o cenário `-cpu max -smp 4 -m 4096` também passou com SMAP ativo, incluindo APs, remoção da identidade e 43/43. O alvo reproduzível é `make smap-test`. Isso revelou e corrigiu a falta de EFER.NXE no trampoline dos APs (`8211823`) e uma escrita de kernel bloqueada por SMAP em `brk`. O passo 17 oferece operações `_in(page_directory_t *)` para mapear, alterar permissões e remover páginas; `mmap`, `munmap` e `brk` as usam, mas a migração das demais operações ainda falta. A Fase 4 também começou: o stub `SYSCALL` x86-64, STAR/LSTAR/SFMASK, `swapgs`, seleção explícita de ABI por tarefa e um dispatcher isolado já cobrem `read`, `write`, `close`, `mmap`, `munmap`, `brk`, `getpid`, `exit` e `exit_group`; um ELF ring-3 confirma getpid/mmap/munmap/write com numeração Linux. Isso é uma fatia inicial, não fecha os passos 35/36. Ainda faltam demand paging, stacks de kernel guardadas e a migração completa para operações explícitas por mm para fechar o Gate 1.

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
15. Usar kernel stacks de 16–32 KiB com página de guarda e separar stacks de syscall e IRQ.
16. Registrar a decisão de adiar KPTI enquanto o modelo de ameaça não exigir isolamento de kernel por CR3.
17. Fazer operações de página receberem explicitamente o `mm`/diretório alvo em vez de depender de `current_directory` global.

**Gate 1:** ET_DYN carregado e executado em `0x00005555_5555_4000`; máquina com 4 GiB consegue usar memória acima de 1 GiB; acesso NX gera page fault; suíte nativa e ring-3 preservadas.

## Fase 2 — Processos, threads e sincronização

18. Separar `process_t` (mm, FDs, cwd, credenciais, grupos e ações de sinal) de `thread_t` (registradores, kernel stack, FS base e TID).
19. Implementar clone com `CLONE_VM`, `CLONE_FS`, `CLONE_FILES`, `CLONE_SIGHAND`, `CLONE_THREAD` e `CLONE_SETTLS`, além de `exit` e `exit_group`.
20. Implementar `arch_prctl(ARCH_SET_FS/ARCH_GET_FS)` e base FS por thread, com suporte a TLS.
21. Implementar sinais pendentes/bloqueados, `sigaction`, `sigprocmask`, `sigreturn`, `sigaltstack` e entrega síncrona de SIGSEGV/SIGFPE.
22. Implementar credenciais consistentes (`uid/euid/gid/egid`, `umask` e consultas), inicialmente com IDs zero.
23. Implementar grupos e sessões, `wait4`, `WIFEXITED`, `WNOHANG` e SIGCHLD.
24. Substituir loops de espera em waitpid e pipes por wait queues de bloqueio e despertar no scheduler.
25. Implementar futex WAIT/WAKE/REQUEUE e robust lists sobre as wait queues; prioridade herdada pode ficar para depois.
26. Implementar `set_tid_address` e `set_robust_list`; `rseq` pode responder ENOSYS para runtimes que saibam desativá-lo.

**Gate 2:** programa musl estático com oito pthreads, mutex e condvar executa; kill com handler funciona; `wait4` retorna status correto.

## Fase 3 — VM de userspace e loader ELF

27. Implementar demand paging: page fault consulta VMA, aloca página zerada e cresce stack respeitando gap de guarda.
28. Implementar COW no fork com refcount de frames físicos e cópia na primeira escrita.
29. Implementar mmap de arquivo privado/compartilhado com offset e page cache compartilhado.
30. Implementar `mprotect`, `mremap`, `madvise` (stub inicialmente) e `MAP_FIXED` com substituição real.
31. Integrar `brk` por processo à lista de VMA.
32. Atualizar loader para PT_INTERP, PT_GNU_STACK, ET_DYN/PIE, PT_TLS e auxv com AT_PHDR, AT_PHENT, AT_PHNUM, AT_ENTRY, AT_BASE, AT_PAGESZ, AT_RANDOM, AT_UID/GID, AT_HWCAP e AT_CLKTCK.
33. Montar stack inicial Linux com argc/argv/envp/auxv, alinhamento de 16 bytes e 16 bytes aleatórios em AT_RANDOM.
34. Fornecer vDSO com clock_gettime, gettimeofday, time e getcpu, anunciado por AT_SYSINFO_EHDR.

**Gate 3:** `hello` linkado estaticamente com glibc executa; em seguida, `ld.so` mapeia libc.so.6 e executa um binário dinâmico.

## Fase 4 — Superfície de syscalls Linux

35. Implementar entrada x86-64 `syscall/sysret` via STAR/LSTAR/SFMASK; manter `int 0x80` i386 somente se WoW64/i386 for priorizado.
36. Criar dispatcher isolado em `compat/linux/`, com números Linux, tradução de argumentos/estruturas e retorno `-errno`; cada processo registra sua ABI.
37. Implementar syscalls de arquivos: openat, newfstatat, readlinkat, getdents64, renameat, unlinkat, mkdirat, faccessat, utimensat, fsync, ftruncate e fcntl (incluindo F_DUPFD_CLOEXEC/F_GETFL).
38. Implementar syscalls de processo: clone/clone3, execve, exit_group, wait4, getpid/getppid/gettid, kill/tgkill e prctl básico.
39. Implementar syscalls de memória: mmap/munmap/mprotect/mremap/brk/madvise/msync.
40. Implementar sincronização e I/O: futex, eventfd, timerfd, epoll_create1/ctl/wait, poll/ppoll e pipe2.
41. Implementar tempo: clock_gettime/clock_getres, nanosleep/clock_nanosleep e gettimeofday.
42. Implementar uname, sysinfo, getrandom, sched_yield, getrlimit/setrlimit (stub), personality e ioctls de terminal básicos (TCGETS/TCSETS/TIOCGWINSZ).
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
