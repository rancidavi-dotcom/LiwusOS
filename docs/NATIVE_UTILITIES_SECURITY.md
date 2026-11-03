# Native utilities and filesystem security v0.1

This milestone keeps the LIWUS kernel and native `int 0x80` ABI independent.
The utilities are static ELF64 applications built with the Native SDK; they
call generic native syscalls and do not add shell built-ins or Linux ABI calls.

## Credential and permission contract

- Ring-3 LIWUS tasks start with uid/euid/gid/egid 1000. Forked tasks inherit
  credentials. Linux-personality tasks remain separate.
- UID 0 is reserved for kernel-created contexts and bypasses DAC checks. There
  is no user-facing way to become uid 0 or change credentials.
- SDFS V2 mode fields use conventional 9-bit octal owner/group/other bits.
  Legacy stored encodings `0x0314` and `0x0315` are read as `0644` and `0755`.
- New regular files use `0666 & ~umask`; new directories use the requested mode
  masked by umask. The default umask is `0022`.
- The SDFS root is `0777` to permit work in this single-user system. Installed
  `/bin` is `0755`; initrd seeding preserves tar uid/gid/mode.
- Path lookup requires execute/search permission on each traversed directory.
  File open checks read/write mode; directory enumeration requires read and
  execute. `stat` needs path search but does not require file read permission.
- Create, unlink, rmdir, and rename require write and execute on the parent
  directory. `execve` requires an execute bit on SDFS or initrd files.
- File descriptors preserve the read/write rights granted by `open`, even if
  metadata later changes. `read` and `write` reject a descriptor lacking the
  requested right.

This is discretionary access control for one unprivileged user identity, not
multi-user isolation. There is no `chmod`, `chown`, login, ACL, setuid/setgid,
sticky bit, symlink, or device-node support. SDFS stores 16-bit uid/gid fields;
the current boot contract only assigns 0 and 1000.

## VFS and SDFS operations

Native syscall numbers remain unchanged: `open` 5, `unlink` 24, `mkdir` 25,
`rmdir` 29, `getdents` 30, and `rename` 38. VFS provides access-aware lookup,
create-as, mkdir, unlink, rmdir, and rename callbacks. SDFS implements those
generic operations; applications use the published Native SDK wrappers.

- `mkdir` creates one directory. Parent directories must already exist.
- `rm` removes regular files only. It does not recurse and directs users to
  `rmdir` for directories; there is no `rm -rf`.
- `mv` renames regular files to absent destinations. Directory moves and
  replacement of existing destinations are rejected. Cross-mount VFS rename
  returns `-EXDEV`.
- `cp` copies binary data in bounded blocks, creates the destination
  exclusively, and removes it after an ordinary read/write failure. It does
  not copy directories or overwrite existing destinations.
- `touch` creates an empty file. An existing regular file is left byte-for-byte
  unchanged; it does not update mtime because the native ABI has no timestamp
  update operation. `O_TRUNC` uses a copy-on-write metadata switch when the
  file is not open; it returns `-EBUSY` while another descriptor references
  that file, preserving the old bytes.
- `echo` writes its arguments separated by spaces and a newline to stdout.

Open-file unlink is supported: the directory entry is removed immediately,
while the file's block chain remains allocated and accessible through open
descriptors. The chain is reclaimed when the final descriptor closes.
`rmdir` rejects a non-empty directory.

## Integrity limits

SDFS journaling durably updates one metadata block at a time. Creating a
directory may leak its newly allocated block if metadata commit fails. Unlink
removes the name before releasing blocks, so interrupted cleanup can leak
space rather than leave a live entry pointing at freed storage. Open-file
unlink defers block release until the last close.

`rename` is not crash-atomic: SDFS removes the old entry and adds the new entry
as separate metadata operations, with an in-memory rollback attempt if the
addition fails. The utility restricts `mv` to regular files and an absent
destination, but a crash between metadata writes can still leave an incomplete
rename. There is no multi-block SDFS transaction or garbage collector.

`cp` writes the new destination through ordinary descriptor writes. An
application failure triggers cleanup, but a crash can leave a partial file.
These operations are not described as crash-safe. See
[`SDFS_TRANSACTIONS.md`](SDFS_TRANSACTIONS.md) for the journal's actual commit
ordering and limits.

## Validation

The ring-3 permission probe uses known SDFS metadata and verifies real syscall
allow/deny results, descriptor access, parent-directory rules, unlink lifetime,
truncate behavior, and execute-bit handling. `make app-install-test` ran
`mkdir`, `cp`, `mv`, `touch`, and `echo` as shell-launched `.lxe` applications
and verified the resulting files, uid, and modes across four boots of one
temporary SDFS image. The initial full shell-test timeout was caused by the
test harness waiting twice for a single child-exit marker on commands with no
stdout; serial diagnostics confirmed it had not sent the next command and
that the guest received every byte sent so far. The harness now distinguishes
command output from child status. Its full QEMU session passes after deleting
and creating directory entries, which also exposed and verified the SDFS
tombstone traversal fix. The native shell resolves the applications through
the same generic `.lxe` lookup used for other programs. Repeated 100-command
stability runs are recorded separately; one successful run is not a soak-test
claim. The final integration scenario passed ten paced QEMU runs plus one
default-paced run; this is a bounded regression run, not an indefinite soak
test.
