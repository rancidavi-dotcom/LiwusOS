# LIWUS Native Shell v0.1

`liwsh.lxe` is a freestanding C ring-3 program linked against the Native SDK.
It uses the native `read`, `write`, `fork`, `execve`, `waitpid`, and shutdown
interfaces. The kernel does not parse shell commands. The normal boot path
starts the userspace `liwus_init` supervisor, which restarts the shell after
unexpected process exits.

## Build, install, and start

```sh
make sdk
make liwsh
make sdk-install SDK_PREFIX=/tmp/liwus-sdk
make all
make run
```

`make run` displays the guest's text console in the host terminal through QEMU
curses. Its Q35 profile disables i8042 and attaches the emulated keyboard and
mouse to EHCI, exercising the USB input path used by the shell. The same shell
also accepts the kernel's PS/2 fallback on machines that provide it; a physical
H61/EHCI boot keyboard is validated separately in `docs/USB.md`. Type at the
`liwus>` prompt and press Enter. The cooked kernel input echoes characters.
While waiting for input, the kernel sleeps in 10 ms timer intervals rather
than spinning continuously.

Example commands:

```text
liwus> help
liwus> native-hello
Hello from LIWUS OS SDK v0.1!
[liwsh] exit status 0
liwus> external-test demo
```

The shell provides `help`, `version`, `pwd`, `cd`, and `exit`. `exit` requests
ACPI poweroff so QEMU closes and returns to the host terminal; Ctrl-D exits
only the shell and the init supervisor restarts it. `cd` changes the
shell process directory, so later child applications inherit it. For a bare name without a
suffix, it tries `name`, `name.lxe`, `/bin/name`, and `/bin/name.lxe`, in that
order. A name already ending in `.lxe` is tried as-is in the current directory
and then under `/bin`, without adding another suffix. A path containing `/`
is tried as written, followed by the same path with `.lxe` only when the suffix
is absent; it is never searched under `/bin`. Only `-ENOENT` advances to the
next candidate. An invalid executable or another execution error is reported
and stops resolution. The original `argv[0]` and all arguments are passed
unchanged. `.lxe` is not used to select an ABI or loader: the ELF header does.
The native VFS enforces SDFS owner/group/other read, write, and execute bits on
path traversal, open, creation, removal, rename, and execution. User processes
share uid/gid 1000; uid 0 is reserved for kernel-created contexts. This is a
single-user DAC model without credential-changing calls or multi-user login.

`ls.lxe`, `cat.lxe`, `mkdir.lxe`, `rmdir.lxe`, `stat.lxe`, `head.lxe`,
`rm.lxe`, `mv.lxe`, `cp.lxe`, `touch.lxe`, and `echo.lxe` are independent SDK
applications. `ls` enumerates a directory
through `liw_getdents`; without a path it uses the current directory. `cat` and
`cp` copy file bytes through bounded buffers. `mv` handles regular files when
the destination is absent; directory moves are unsupported. `cp` accepts a
destination directory and writes the source basename inside it; it refuses to
overwrite an existing file. `touch` creates a file or leaves an existing
regular file unchanged. `rm` removes regular files; `mkdir -p` creates missing
parent directories and succeeds when the requested directory already exists.
Without `-p`, `mkdir` creates one directory whose parent must exist. `echo`
writes its arguments and a newline. `rmdir` removes an empty directory, `stat`
prints type, size, and permission bits, and `head FILE` prints at most the first
ten lines. These are not shell or kernel built-ins.

## Command language

The parser supports an executable and up to 14 arguments (15 words total)
separated by spaces or tabs. Empty lines are ignored. Input is limited to 255 characters; an
overlong line is drained through Enter before the next prompt. A read error is
reported and the prompt remains available. Ctrl-D on an empty line exits the
shell; the init supervisor then starts a fresh shell. Ctrl-D after text submits
that text as a command line.

Quotes, escapes, pipes, redirection, environment expansion, globbing, scripts,
background jobs, and job control are not implemented. Quote, escape, and
operator characters are rejected explicitly. There is no configurable `PATH`;
`ls` and `cat` are ordinary programs installed in `/bin`, not shell built-ins.

## Planned v0.2 milestone

The next recommended increment is deliberately smaller than a POSIX shell:
quoted/escaped arguments, bounded pipelines, and `<`, `>`, `>>`, and `2>`
redirection using the existing native `pipe`, `dup2`, `fork`, `execve`,
`close`, and `waitpid` calls. Stream-oriented utilities should accept standard
input where appropriate. Background execution and job control remain deferred
until signals and process-group semantics exist. Completion requires host
parser cases plus QEMU integration that proves descriptor closure, command
failure recovery, and repeated pipelines without leaking file or pipe state.

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
make app-install-test # persistent SDFS install/update across boots
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
`test-logs/shell-integration.serial`. The intended utility sequence covers a
temporary SDFS disk, including cwd changes, metadata display, first-ten-line
output, empty-directory removal, multi-block and empty-file copies, rename,
no-overwrite behavior, and preservation when touching an existing file. The
test includes a 100-command numbered echo sequence through the real
serial console to detect missing, repeated, stale, or reordered responses. A
previous harness bug double-waited for the exit marker on commands with no
stdout and therefore stopped before sending the next command. That expectation
is corrected; the serial transcript and repeat count are recorded in the
stabilization report.

This is a minimal interactive process launcher, not a general-purpose shell.
The kernel's current serial input is polled at a bounded 10 ms sleep cadence;
interrupt-driven UART input and hardware EOF/disconnect semantics remain future
work. Kernel and ring-3 baseline manifests remain separate from this dedicated
interactive integration test.
