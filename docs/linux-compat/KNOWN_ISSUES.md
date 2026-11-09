# Linux Compatibility - Known Issues

All items below are real limitations observed or documented. Hypotheses are marked as such.

## 1) glibc static binary fails with #UD (AVX)
- **Symptom:** Attempt to run a GNU/Linux static ELF (built on this machine) fails with `#UD` in `_dl_aux_init` at `vpxor` instruction.
- **Cause known/hypothesis:** Binary built with GNU_PROPERTY requiring x86-64 v3/v4, uses AVX instructions; kernel does not enable/save/restore AVX/XSAVE state and may not announce support.
- **Evidence:** Documented in `docs/ABI_LINUX.md` "Evidência do milestone atual".
- **Possible solution:** Enable XSAVE/XRESTOR, save/restore FPU/SSE/AVX per thread, set CR4.OSXSAVE, configure XCR0, announce CPU features via CPUID/auxv HWCAP appropriately.
- **Dependencies:** CPU feature detection, FPU context save/restore per thread, XSAVE infrastructure.
- **Reproduce:** Try to run glibc static ELF on smap-test or ring3 environment; see #UD.
- **Resolve when:** Binary runs without #UD or is cleanly rejected with explanation; AVX state properly handled.

## 2) No PT_INTERP support
- **Symptom:** Dynamically linked ELF (with PT_INTERP) not loaded/executed.
- **Cause:** Loader does not parse PT_INTERP or load dynamic linker.
- **Evidence:** Documented in ABI_LINUX.md and ROADMAP.
- **Possible solution:** Implement PT_INTERP parsing in ELF loader, load interpreter, set up auxv (AT_BASE), transfer control to interpreter.
- **Dependencies:** VM/demand paging may help, proper auxv, file loading.
- **Reproduce:** Try to run glibc dynamic binary.
- **Resolve when:** ld.so loads and can execute simple dynamic binary.

## 3) Incomplete pthreads/libc support
- **Symptom:** Full libc programs (especially with threads) not validated beyond freestanding probes.
- **Cause:** Missing signals, full clone semantics, robust futexes, proper TLS, auxv, possibly other syscalls.
- **Evidence:** ROADMAP states Gate 2 not met for musl static with 8 pthreads.
- **Possible solution:** Address blockers in dependency order (futex extensions, signals, clone completeness, auxv).
- **Dependencies:** Many (see RESUME_PLAN).
- **Reproduce:** Attempt to run musl static with pthreads.
- **Resolve when:** 8 pthreads with mutex/condvar pass tests.

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

## 6) Futexes limited (private only, no timeout/REQUEUE/shared/robust)
- **Symptom:** Advanced futex operations return ENOSYS; timeouts not supported.
- **Cause:** Incremental implementation per roadmap.
- **Evidence:** linux_clone_probe tests rejection of shared/timeout cases; code returns ENOSYS.
- **Possible solution:** Extend futex implementation with proper keying, timeouts integrated with wait queues, REQUEUE, robust lists.
- **Dependencies:** Wait queues (done for basic case), timer infrastructure.
- **Reproduce:** Try FUTEX_WAIT with timeout or shared futex.
- **Resolve when:** Supported operations work per Linux semantics.

## 7) Missing signals/wait4/credentials full semantics
- **Symptom:** No signal delivery, incomplete wait4/status, credentials partial.
- **Cause:** Not implemented yet (Fase 2/4 gaps).
- **Evidence:** ROADMAP and status docs.
- **Possible solution:** Implement signal subsystem, wait4 with proper status, credentials/groups/sessions.
- **Dependencies:** Process/thread model (done), wait queues (done).
- **Reproduce:** Programs using signals/wait4 fail.
- **Resolve when:** Basic signal/wait4 cases work.

## 8) No vDSO/auxv completeness
- **Symptom:** Linux binaries expecting vDSO/auxv entries may fail.
- **Cause:** Not implemented.
- **Evidence:** ROADMAP step 34, ABI_LINUX notes missing.
- **Possible solution:** Provide vDSO page with needed functions, build proper auxv.
- **Dependencies:** VM, loader.
- **Reproduce:** Binaries checking auxv/vDSO fail.
- **Resolve when:** Required auxv entries present and vDSO works if used.

## 9) Broad syscall surface gaps
- **Symptom:** Many Linux syscalls return ENOSYS.
- **Cause:** Only minimal set implemented.
- **Evidence:** Dispatcher has default ENOSYS.
- **Possible solution:** Add syscalls as needed by real programs, translate correctly.
- **Dependencies:** Varies.
- **Reproduce:** Run complex binaries.
- **Resolve when:** Target programs run without unexpected ENOSYS.

## Notes
- None of these indicate fundamental design flaws - they are planned incremental work.
- Do not guess; verify against code/tests when investigating.
