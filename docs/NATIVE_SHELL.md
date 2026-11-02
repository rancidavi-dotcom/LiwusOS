# LIWUS Native Shell v0.1

`liwsh.lxe` is a freestanding C ring-3 program linked against the Native SDK.
It uses the existing native `read`, `write`, `fork`, `execve`, `waitpid`, and
`exit` interfaces. The kernel does not parse shell commands. The normal boot
path starts the userspace `liwus_init` supervisor, which starts `liwsh.lxe` and
restarts it after it exits.

## Build, install, and start

```sh
make sdk
make liwsh
make sdk-install SDK_PREFIX=/tmp/liwus-sdk
make all
make run
```

`make run` connects the guest COM1 serial console to the host terminal. Type at
the `liwus>` prompt and press Enter. The PS/2 keyboard is also accepted by the
existing kernel input path. The cooked kernel input echoes characters. While
waiting for input, the kernel sleeps in 10 ms timer intervals rather than
spinning continuously.

Example commands:

```text
liwus> help
liwus> native-hello.lxe
Hello from LIWUS OS SDK v0.1!
[liwsh] exit status 0
liwus> external-test.lxe demo
```

The shell provides `help`, `version`, and `exit`. A bare executable name is
tried relative to the current directory, then under `/bin/`; a path containing
`/` is passed as written. `.lxe` is not used to select an ABI or loader: the
normal ELF64 loader decides from the file header.

## Command language

The parser supports an executable and up to 14 arguments (15 words total)
separated by spaces or tabs. Empty lines are ignored. Input is limited to 255 characters; an
overlong line is drained through Enter before the next prompt. A read error is
reported and the prompt remains available. Ctrl-D on an empty line exits the
shell; the init supervisor then starts a fresh shell. Ctrl-D after text submits
that text as a command line.

Quotes, escapes, pipes, redirection, environment expansion, globbing, scripts,
background jobs, and job control are not implemented. Quote, escape, and
operator characters are rejected explicitly. No `PATH`, `cd`, `ls`, or other
filesystem utility is implied.

## Add an application

Build a static LIWUS ELF64 executable with the installed SDK as described in
[SDK_LXE.md](SDK_LXE.md). For a new app, install it into the existing SDFS
image with the host-side offline installer, then boot that same image and run
the bare name through `/bin` lookup:

```sh
make app-install APP=/path/to/my-app.lxe DISK=/path/to/liwus.img
make run
# At liwus>:
my-app.lxe
```

Stop QEMU before installation. Use `REPLACE=1` to update a name already in
`/bin`. See [SDFS_INSTALL.md](SDFS_INSTALL.md) for image validation and the
filesystem's actual replacement and crash-safety limits. There is no
per-application kernel or shell registry.

## Regression coverage

```sh
make test-unit       # parser boundary and syntax cases (host ASan/UBSan)
make external-sdk-build
make shell-test      # scripted bytes sent through QEMU's real serial console
make test-all
make test-stress
```

The external SDK test generates its C project in a temporary directory outside
the repository, installs the SDK to a separate temporary prefix, checks the
linked static ELF, and runs the artifact in ring 3. `shell-test` boots the
normal userspace init path, sends multiple commands through QEMU COM1, and
checks child output/status, invalid-command recovery, and shell restart after
`exit` and Ctrl-D. It also measures QEMU CPU time during one idle second at the
prompt and requires less than 0.75 CPU seconds. Its serial transcript is written to the ignored
`test-logs/shell-integration.serial`.

This is a minimal interactive process launcher, not a general-purpose shell.
The kernel's current serial input is polled at a bounded 10 ms sleep cadence;
interrupt-driven UART input and hardware EOF/disconnect semantics remain future
work. Kernel and ring-3 baseline manifests remain separate from this dedicated
interactive integration test.
