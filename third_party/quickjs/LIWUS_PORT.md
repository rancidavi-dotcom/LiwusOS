# QuickJS on LIWUS

This directory carries the upstream QuickJS 2026-06-04 sources and their
upstream MIT license. The application is built by `programs/qjs/Makefile` as a
static native LIWUS ELF64 executable (`qjs.lxe`); it does not link to Linux or
host glibc.

## Port boundary

`programs/qjs/main.c` supplies the LIWUS allocator and process-facing adapters.
The runtime uses a 64 MiB QuickJS allocation limit, 1 MiB stack limit, and an
execution interrupt budget. Atomics are disabled for `CONFIG_LIWUS`, because
the kernel does not provide the thread primitives required by JavaScript
Atomics.

For LiwusWeb, QuickJS runs in ring 3 as a child process. Its HTML document is
parsed by the native LIWUS DOM library, and the process receives bounded DOM,
event, timer, and browser-global shims. The page's resulting DOM is serialized
back to the kernel renderer. Network `fetch()` requests use a bounded IPC
channel serviced by the kernel browser worker; QuickJS itself does not include
an HTTP or TLS implementation.

The current integration is a compatibility subset, not a complete web browser
runtime. The kernel inlines bounded external scripts and preserves their type
and source URL. In persistent browser mode, QuickJS resolves HTTP(S) relative,
root-relative, and scheme-relative static imports through the kernel's browser
worker IPC. It limits a page to 64 imported files and 4 MiB of imported source,
caps one response at 1 MiB, and uses a shared 20-second wait budget. Other
in-flight `fetch()` responses are deferred until module loading returns.

The first `type="importmap"` block supports up to 32 top-level string entries
from `imports`, including exact and longest-prefix matches. `scopes`, map
merging, dynamic `import()`, `modulepreload`, CORS checks, non-HTTP(S) modules,
and standards-complete script timing remain unsupported. Module requests are
synchronous within the script execution task, so a slow network can delay page
presentation until the shared deadline. These bounds and the IPC loader are
LIWUS browser behavior; they are not a claim of full web platform conformance.

## Upstream changes

- `CONFIG_ATOMICS` is excluded for `CONFIG_LIWUS` in `quickjs.c`.
- Build flags use freestanding native LIWUS headers and runtime support.
- The upstream source snapshot was downloaded from
  <https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz>.

When updating upstream, review these changes and the LIWUS application API
bindings, then rerun the QuickJS ring-3 and browser-script regressions.
