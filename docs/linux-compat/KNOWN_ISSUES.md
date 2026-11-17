# Linux Compatibility - Known Issues

All items below are real limitations observed or documented. Hypotheses are marked as such.

## 1) Broader glibc and x86 feature coverage remain incomplete
- **Boundary:** The former static glibc `#UD` at `vpxor` was resolved by enabling XSAVE/XCR0=7 and saving/restoring AVX state per thread. A static glibc `puts` smoke ELF now passes in QEMU `-cpu max`; this does not establish broad glibc compatibility or AVX-512 support.
- **Evidence:** `make smap-test LINUX_STATIC_ELF=/tmp/liwus-static-glibc-v3-hello` passes on QEMU `-cpu max`. The same x86-64-v3 binary traps at `vpxor` under default `ring3-test` QEMU, which does not expose AVX; a static baseline musl hello passes there. `linux_syscall_probe` preserves a YMM pattern across `nanosleep`, `fork`, and `wait4` when XSAVE+AVX are available.
- **Remaining limitations:** Only x87/SSE/AVX components (XCR0 bits 0–2) are managed. AVX-512, AMX, heterogeneous CPU feature policy, and exhaustive FPU/SIMD use from kernel context are not validated. The glibc smoke ELF is not a dynamic-linking or general libc test.
- **Dependencies:** Broader CPU feature policy, advanced xstate components, kernel SIMD bracketing and SMP migration tests.
- **Resolve when:** Separate tests cover every advertised xstate feature and migration/CPU-feature boundaries, while dynamic glibc runs through PT_INTERP.

## 2) Partial Linux memory mapping
- In QEMU, glibc loads shared libraries through private file mappings and fixed replacement. The original Unity player also loads its Mono library.
- Mappings copy file data into private frames at map time. Shared files, demand paging and SIGBUS for accesses past EOF require additional work.

## 3) Incomplete pthreads/libc support
- **Status:** The reproduced default-stack TLS fault is fixed by Linux `mprotect(10)` translation to generic LIWUS VMA and page-table protection updates. Musl's `pthread_create` maps stack plus guard as `PROT_NONE`, then enables the usable stack with `mprotect` before copying TLS; its libc tolerates `ENOSYS`, so the previously missing syscall silently left the stack inaccessible.
- **Evidence:** `tests/linux/musl_pthread_default_stack.c` and `tests/linux/musl_pthread_smp.c` are ordinary static Linux-targeted musl ELFs with no `PT_INTERP`. Default-stack `pthread_create/join` and a four-thread mutex/counter/join probe passed `make smap-test LINUX_STATIC_ELF=...` with SMAP and a four-vCPU QEMU configuration. This does not prove CPU migration; scheduler placement remains a separate limitation.
- **Remaining limitations:** Condvar tests, larger thread counts, cancellation, robust lists, signals, races in concurrent VMA changes, and broad libc behavior remain unvalidated. W+X is rejected by kernel policy, and the VM is eagerly backed.
- **Reproduce:** Follow `tests/linux/README.md` for exact source and build commands.
- **Resolve when:** Expand the musl pthread matrix (including condvars and error paths), validate scheduler placement/migration separately, and keep all observed gaps explicit.

## 4) No validated cross-CPU thread migration
- **Symptom:** Threads may run only on BSP currently.
- **Cause:** Scheduler/userspace tasks execute on BSP per documentation; SMP APs initialized but migration not validated.
- **Evidence:** ABI_LINUX.md states "usuários executam no BSP" and migration/SMP stress not proven.
- **Possible solution:** Ensure scheduler can migrate threads, test on smp-test, validate TLS/state across migration.
- **Dependencies:** Scheduler SMP support, per-CPU state.
- **Reproduce:** smap-test shows APs initialized but thread migration untested.
- **Resolve when:** Multi-threaded program runs correctly across CPUs with validated behavior.

## 5) No validated remote TLB shootdown
- **Symptom:** TLB consistency across CPUs not validated for concurrent mappings.
- **Cause:** Not implemented/tested.
- **Evidence:** ABI_LINUX.md notes "Também não foi validado TLB shootdown remoto para tasks concorrentes em APs."
- **Possible solution:** Implement IPI-based TLB shootdown on page table changes affecting other CPUs.
- **Dependencies:** SMP, IPIs, page table management.
- **Reproduce:** Concurrent mmap/munmap/mprotect on SMP.
- **Resolve when:** TLB shootdown validated on SMP.

## 6) Futex coverage remains partial
- **Symptom:** Shared futexes and robust lists remain unsupported. Private WAIT_BITSET/WAKE_BITSET and absolute timeout handling have only initial probe/handshake coverage. Private `FUTEX_REQUEUE`/`FUTEX_CMP_REQUEUE` now cover the tested pthread condition-variable path, with limited race coverage. Relative timeout support for private `FUTEX_WAIT` and `set_tid_address` clear-and-wake pass ring-3 QEMU probes at the kernel's 10 ms tick resolution. The Linux personality now also implements the single-CPU owner-word path for `FUTEX_LOCK_PI`, `FUTEX_TRYLOCK_PI` and `FUTEX_UNLOCK_PI`, which Granny's Unity bootstrap uses.
- **Cause:** Incremental implementation per roadmap; shared futex keying needs shared backing identity.
- **Evidence:** `linux_clone_probe` checks timed expiry, invalid timespec, bad pointer, child-TID clearing and futex wake; it passes in QEMU. Shared/robust-list behavior remains unimplemented; private requeue has a basic probe and a static musl condvar broadcast/join integration test; mismatched bitset filtering and broader waiter race cases remain untested.
- **Possible solution:** Validate relative timeout behavior first, then expand bitset clock/mask race tests, broader private requeue race tests, shared-keying and robust-list owner-death handling independently.
- **Dependencies:** Generic wait queues and timer infrastructure exist; shared futex identity and robust-list lifecycle still need design.
- **Reproduce:** Run the updated Linux clone probe; try unsupported operations to confirm `-ENOSYS`.
- **Resolve when:** Each implemented operation passes Linux ABI probes and multi-waiter/race coverage.

## 7) Partial signals and incomplete process-wait semantics
- **Symptom:** The Linux personality now has the ABI plumbing for `rt_sigaction`, `sigaltstack`, a kernel-layout x86-64 `SIGSEGV` frame and `rt_sigreturn`, plus basic per-thread `tgkill` queuing/wakeup and narrowly-scoped Mono startup handling for the legacy null-probe path. Process-directed signals, process groups, SIGCHLD and resource accounting remain absent.
- **Cause:** Generic LIWUS signal state and full process-signal routing are not implemented; the current frame path exists to unblock Mono's synchronous fault handler and the `tgkill` path only covers a queued signal to a thread in the current Linux process.
- **Evidence:** `linux_syscall_probe` now validates fork, WNOHANG, normal exit encoding, EFAULT without child reaping, zeroed rusage and ECHILD after reap in QEMU. The Granny-specific probe can now be skipped at the known Mono instruction before signal delivery, and the fallback restorer path restores the saved FPU image. ROADMAP and implementation status document remaining semantics.
- **Possible solution:** Expand the frame path into generic LIWUS signal state/delivery and process-group/accounting abstractions, then translate Linux wait4/SIGCHLD behaviors inside the personality.
- **Dependencies:** Process/thread model and wait queues exist; signal delivery and accounting do not.
- **Reproduce:** Run `make ring3-test`; the freestanding Linux probe exercises the supported wait4 subset.
- **Resolve when:** Signal, stop/continue and process-group cases plus real rusage accounting pass Linux ABI probes.

## 8) Basic auxv exists; vDSO and libc completeness remain absent
- **Symptom:** Linux programs requiring `AT_SYSINFO_EHDR` or shared libraries can fail; libc behavior is not broadly validated.
- **Current boundary:** `AT_HWCAP` reports CPUID.1:EDX and `AT_HWCAP2` is zero. LIWUS does not enable ring-3 MWAIT/FSGSBASE; it enables x87/SSE/AVX state only when XSAVE+AVX are available. Broader feature masking, AVX-512/AMX, and heterogeneous CPU behavior are not validated.
- **Cause:** LIWUS supplies a tested basic ELF64 auxv but lacks a vDSO and a complete CPU capability contract.
- **Evidence:** `linux_syscall_probe` validates the initial stack/auxv and conditionally tests YMM context preservation; static musl and glibc `puts` smokes pass with `LINUX_STATIC_ELF`. The glibc archive carries a GNU property note for x86-64-v3/v4, but a single greeting does not show that AVX-512 code paths execute.
- **Program break boundary:** `AT_BRK` and Linux `brk(12)` now share a tested page-aligned initial value and support basic eager grow/shrink. Concurrent `brk`/`mmap`, `RLIMIT_DATA` enforcement, and memory pressure races are not verified.
- **Possible solution:** Add a generic CPU capability policy and vDSO.
- **Dependencies:** VM, ELF loader, FPU/XSAVE and feature policy.
- **Reproduce:** `make ring3-test`; optional external static musl hello via `LINUX_STATIC_ELF=/tmp/liwus-musl-hello`.
- **Resolve when:** Static and dynamic libc programs pass reproducible external tests with supported CPU feature requirements.

## 9) Broad syscall surface gaps
- **Symptom:** Many Linux syscalls return ENOSYS.
- **Cause:** Only minimal set implemented.
- **Evidence:** Dispatcher has default ENOSYS.
- **Possible solution:** Add syscalls as needed by real programs, translate correctly.
- **Dependencies:** Varies.
- **Reproduce:** Run complex binaries.
- **Resolve when:** Target programs run without unexpected ENOSYS.

## 10) AVX-512/AMX and kernel SIMD context policy remain unsupported
- **Symptom:** Programs that execute AVX-512/AMX or depend on those state components across task switches are not supported; unbracketed kernel SIMD use could corrupt a user's extended state.
- **Current implementation:** x87/SSE/AVX are enabled with CR4.OSXSAVE and XCR0=7 when CPUID permits. Per-thread buffers are 64-byte aligned and saved/restored with XSAVE/XRSTOR; CPUs without XSAVE+AVX retain the FXSAVE/FXRSTOR path.
- **Evidence:** `make smap-test` runs the conditional YMM preservation probe on `-cpu max`; static glibc `puts` smoke passes. The context format deliberately excludes XCR0 components above bit 2.
- **Possible solution:** Add per-CPU feature intersection, AVX-512/AMX state lifecycle only when needed, kernel FPU bracketing, and migration tests before advertising those features.
- **Dependencies:** CPU capability policy, scheduler and interrupt FPU ownership, aligned context lifecycle.
- **Reproduce:** `make smap-test`; static glibc smoke: `make smap-test LINUX_STATIC_ELF=/tmp/liwus-static-glibc-v3-hello`.
- **Resolve when:** Every enabled xstate component is preserved across kernel entry, interrupts, thread/process switches and CPU migration.

## 11) Granny Unity/Mono bootstrap still stops before the first scene
- **Symptom:** The original Granny player loads the dynamic ELF, Mono, X11/XInput and the ScreenSelector plugin, reaches the display probe, creates its PlayerPrefs directories and calls the ScreenSelector `Setup`/`Load` hooks. The Unity graphics device can initialize under llvmpipe in the successful fixture, but the current clean replay stops in Mono's cooperative thread-registration barrier before exposing a mapped Unity child window; scene initialization and input are still not validated.
- **Evidence:** The LIWUS launcher reaches `Initialize engine version: 2018.2.19f1`, `Forcing GfxDevice: OpenGL Core`, `GfxDevice: creating device client`, and the Mesa llvmpipe renderer before `Begin MonoManager ReloadAssembly`. The known Mono probe can now be advanced directly at page-fault delivery (`0x5a4b3` → `0x5a543`), including 2 MiB-aligned ASLR layouts (`rip & 0x1fffff`), avoiding the prior “unhandled NULL exception”/restorer loop; the process remains alive for the probe window. A complete X11 capture from the clean-kernel fixture is reproducible, but no viewable Unity child window is exposed yet, so the scene/menu and input path remain unvalidated.
- **Cause boundary:** The common private futex probes and static musl pthread/condvar tests pass, and clone3 thread creation reaches the Unity worker-registration path. The remaining stall is a Mono cooperative futex/barrier sequence (the creator waits for registrar wakeups while workers park on their per-thread words), not a generic futex failure proven by the existing probes. The signal workaround is intentionally limited to the known Mono probe (`libmonobdwgc` offset `0x5a4b3` → `0x5a543`); Unity's remaining graphics and managed-runtime paths still exercise unvalidated loader, X11 and rendering contracts.
- **Reproduce:** Use the original Granny fixture and `/bin/launchnoshot.lxe` under the QEMU serial probe; compare the LIWUS log with the native Xvfb log.
- **Resolve when:** The unmodified player reaches Unity initialization and renders its first frame without special user-space or kernel mutex hacks; then validate input and scene loading separately.

## Notes
- None of these indicate fundamental design flaws - they are planned incremental work.
- Do not guess; verify against code/tests when investigating.
