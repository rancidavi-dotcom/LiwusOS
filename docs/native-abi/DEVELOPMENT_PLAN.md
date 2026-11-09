# Native Liwus ABI - Development Plan

## Priorities (incremental)
1. **Consolidate UAPI** - Audit and stabilize existing syscall contracts
2. **Audit contracts** - Verify return conventions, error handling consistency
3. **Create C syscall wrappers** - Easy calling from C without manual assembly
4. **Define minimal app runtime** - crt0/startup, basic environment
5. **Public headers** - UAPI headers independent of kernel internals
6. **Example native program** - Working example
7. **Document compilation/linking/execution** - Clear docs
8. **Test ABI and version compatibility**
9. **Expand runtime** as needed by real apps
10. **Evolve native dev tools** gradually

## Next steps (immediate)
- External SDK consumer build and ring-3 execution
- First Native SDK userspace shell with real serial input
- Incremental filesystem/application provisioning for installed SDFS images

## Constraints
- Preserve binary compatibility
- Don't break Linux personality
- Follow existing conventions or justify changes
- Keep changes minimal and testable

## Progress
- [x] Syscall wrappers and public headers created
- [x] Minimal crt0 and runtime to call main(argc,argv,envp)
- [x] First native C program (native-hello) built and executed in ring 3 via normal loader
- [x] Integrated into test flow with automated validation
- [x] SDK installed to an isolated prefix and used by an external C project outside the repository
- [x] External static ELF64 `.lxe` executed in ring 3 with validated output and exit status
- [x] `liwsh.lxe` built using the SDK; QEMU serial integration covers commands, arguments, child lifecycle, errors, and restart
- [x] Parser host tests run with ASan/UBSan

The shell v0.1 supports `help`, `version`, `exit`, and launching applications.
It accepts whitespace-separated words only (no quotes, operators, pipelines,
redirection, or job control). COM1 input uses the existing kernel input path;
the current blocking read sleeps between polls. See `docs/NATIVE_SHELL.md` for
the actual behavior and limits. Linux personality work remains paused.
