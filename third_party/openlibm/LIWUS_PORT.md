# OpenLibm source in LIWUS

This is the x86-64 source subset of OpenLibm used to provide the standard
mathematical functions required by the ring-3 QuickJS runner. The upstream
project is maintained by JuliaMath; see `README.upstream.md`, `LICENSE.md`,
and `UPSTREAM_COMMIT` for provenance and licensing. Individual files retain
their original notices.

The copy contains upstream sources and headers. LIWUS will build only the
`src`, `amd64`, and `bsdsrc` components needed for x86-64. Host shared libraries
are never linked into native `.lxe` programs.
