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
`CLONE_SETTLS`. `gettid`, `exit` individual e `exit_group` são implementados.
Um probe ET_EXEC freestanding confirma memória compartilhada, retorno do filho,
FS-base diferente no filho e continuidade do processo após o filho encerrar.
Isso ainda não implementa a semântica geral de `clone`, pthreads da libc, futex
compartilhado ou migração SMP. A infraestrutura de estado FPU salva e
restaura x87/SSE por thread; AVX/XSAVE não é habilitado nem anunciado.
O PID e TID coincidem enquanto essa limitação existir.

## Espaço de endereçamento e VMAs

**Decisão:** cada `mm` possui uma coleção ordenada de VMAs não sobrepostas, inicialmente uma lista balanceada pode ser adiada até a cardinalidade exigir.

Cada VMA contém `start`, `end` (half-open), `prot`, `flags`, backing (anônimo ou arquivo), offset e referência ao objeto de arquivo. A página presente na page table é cache da VMA; a VMA é a fonte para faults e operações de intervalo.

- `mmap` seleciona endereços livres na faixa alta do userspace; `MAP_FIXED` substitui sobreposições conforme regras Linux após validar o intervalo todo.
- `munmap` divide, remove ou encurta VMAs e desmapeia páginas correspondentes.
- `mprotect` exige intervalo mapeado e aplica proteção por página; NXE fornece proteção de execução.
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

O stub salva estado de usuário de modo que interrupções e preempção preservem a mesma estrutura de contexto do scheduler. A implementação não reutiliza RCX como quarto argumento Linux: SYSCALL grava RIP em RCX e RFLAGS em R11.

**Implementado parcialmente:** STAR/LSTAR/SFMASK são configurados por CPU; `SYSCALL` troca GS com `swapgs`, salva RSP de usuário em `cpu_local_t`, muda para a kernel stack da tarefa e constrói `registers_t`. O dispatcher Linux inicial traduz `read`, `write`, `close`, `openat`, `stat`, `fstat`, `newfstatat`, `lseek`, `mmap`, `munmap`, `brk`, `getpid`, `exit` e `exit_group`, com tradução explícita da primeira forma x86-64 de `struct stat`. O retorno usa IRETQ, que restaura CS/SS/RIP/RSP/RFLAGS sem depender de SYSRET; a otimização SYSRET com validação de contexto ainda não foi adicionada. Os outros números Linux retornam `-ENOSYS`. A tradução inicial de `openat` aceita somente `AT_FDCWD`; acesso por `dirfd`, `O_CLOEXEC`, file-backed mmap e várias regras de flags ainda não são suportados.

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

- O ELF `tests/ring3/linux_syscall_probe.S` é um `ET_EXEC` estático, sem `PT_INTERP`, que usa a convenção Linux x86-64 sem caminhos ou chamadas especiais do Liwus. Ele valida `getpid`, `fstat`/`stat` layout, `openat`/`-ENOENT`, `-ENOSYS`, `mmap` com argumento 4 em R10, `munmap`, `ARCH_SET_FS`, leitura via `FS:0`, `ARCH_GET_FS`, `write` e `exit`.
- `tests/ring3/linux_clone_probe.S` também executa `FUTEX_WAIT_PRIVATE` bloqueante e exige que o WAKE do pai retorne exatamente um waiter; valida mismatch `-EAGAIN` em memória gravável e somente leitura, endereço desalinhado `-EINVAL`, ponteiro inválido `-EFAULT` e recusa do modo compartilhado sem backing com `-ENOSYS`. O waiter confirma que a palavra compartilhada mudou e termina individualmente.
- A personalidade suporta apenas `FUTEX_WAIT_PRIVATE` sem timeout e `FUTEX_WAKE_PRIVATE`. A chave é `(process->mm, endereço virtual alinhado)`; colisões de hash comparam os dois campos. Futexes compartilhados entre processos, REQUEUE e timeout retornam `-ENOSYS`. O teste não prova paralelismo SMP: usuários executam no BSP; o lock do scheduler torna comparação/enfileiramento atômicos em relação às syscalls concorrentes nesse modelo atual.
- `make smap-test` executa o probe e a suíte nativa/ring-3 no QEMU `-cpu max -smp 4 -m 4096`; a evidência atual mostra SMAP, APIC IDs 1–3, remoção da identidade, frame alocado/acessado/liberado acima de 1 GiB, page fault real na guarda e `47/47 PASS`. O agendador executa as tarefas do teste no BSP; os outros processadores são validados na inicialização SMP, não por migração de tarefas entre CPUs.
- É possível fornecer um ELF GNU/Linux estático externo sem incorporá-lo ao repositório: `make smap-test LINUX_STATIC_ELF=/caminho/para/programa`. O ELF é empacotado temporariamente como `linux_glibc_hello` na imagem de teste.
- O teste adicional com a libc estática desta máquina foi tentado. O ELF estático sem `PT_INTERP` começou a executar, mas terminou com `#UD` em `vpxor` no `_dl_aux_init`; seu `GNU_PROPERTY` exige x86-64 v3/v4, AVX/XSAVE, que o kernel ainda não habilita nem salva/restaura. Portanto, o probe freestanding Linux passa, mas esse binário glibc não passa. O ELF GNU original com `PT_INTERP` também é corretamente recusado enquanto o loader dinâmico não existir.
- FS/GS-base são armazenadas por thread e restauradas na troca de contexto. `ARCH_SET_FS/GET_FS`, `ARCH_SET_GS/GET_GS` e `CLONE_SETTLS` passam no probe Linux; o teste verifica valores FS diferentes no pai e no filho e roda com SMAP ativo. O retorno da thread de teste via `exit` deixa o pai continuar; o `exit_group` termina o processo. Ainda não há prova com libc/pthreads, alternância repetitiva de várias threads, entrega de sinais durante interrupções nem migração entre APs.
- Esta evidência fecha apenas a fatia inicial da entrada Linux e TLS. Não constitui suporte completo Linux nem conclui os gates musl/glibc, threads, sinais, VM sob demanda ou dynamic linker.
- O ensaio foi executado com SMAP ativo em QEMU de quatro CPUs; isso valida a inicialização SMP e as rotas testadas sob SMAP. A suíte ainda não migra tarefas de usuário entre APs, então preservação TLS sob migração multicore permanece sem evidência. Também não foi validado TLB shootdown remoto para tasks concorrentes em APs.
