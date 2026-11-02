# Offline installation of native applications

LIWUS v0.1 installs an external static ELF64 x86-64 application into an
existing SDFS V2 raw image with a host-side tool. The resulting file remains a
normal LIWUS ELF executable; `.lxe` is a naming convention. The tool does not
run in LIWUS ring 3 and does not change the kernel or shell.

## Build and install

Build the application with the public Native SDK as described in
[SDK_LXE.md](SDK_LXE.md). Then stop QEMU and install into an existing disk
image:

```sh
make app-install APP=build/my-app.lxe DISK=/path/to/liwus.img
```

The input must have a `.lxe` suffix and be a little-endian ELF64 `ET_EXEC` for
x86-64. The installer checks the ELF header and bounded program-header table,
the file extents and user virtual ranges of `PT_LOAD`, a loadable executable
entrypoint, static linkage (no `PT_INTERP` or `PT_DYNAMIC`), and W^X, including
overlapping load pages. The kernel ELF loader remains the final execution
authority.

The destination is `/bin/<input-name>`. `/bin` must already exist; the tool
does not invent or recursively create directories. A name collision fails
unless `REPLACE=1` is supplied:

```sh
make app-install APP=build/my-app.lxe DISK=/path/to/liwus.img REPLACE=1
```

The installer validates the SDFS V2 superblock CRC, metadata ranges, bitmap,
directory records, file-chain lengths, allocation bits, and block ownership
before editing. It allocates new data blocks, changes only the selected
directory entry and bitmap, then validates the resulting image. It writes a
full temporary copy beside the disk image, flushes it, and publishes it with
`os.replace`; files outside the target entry are preserved byte-for-byte.

## Offline and crash-safety limits

The tool takes a nonblocking file lock and scans `/proc/*/fd` by device/inode.
It refuses an image currently open by another process, including QEMU. Keep
QEMU stopped throughout installation. This is a host-side guard, not a lock
protocol shared with all possible hypervisors or storage tools.

The temporary-copy-and-rename publication protects the original image from
ordinary validation, allocation, and write failures before publication. It
does not make LIWUS SDFS transactions crash-safe: SDFS's journal logs one
metadata block at a time, allocation bitmap writes are separate, and the
filesystem has no multi-block transaction. Keep a backup before updating
important images. V1 images are currently refused. The tool does not create a
disk, format SDFS, recover corrupt filesystems, or repair a bitmap.

The kernel's first-boot `/.system_installed` marker causes initrd files to be
copied only once. Later boots retain the SDFS contents and do not reinstall
the initrd over applications. If the marker is absent, the existing boot code
copies the initrd contents, which can replace same-named files; a failed SDFS
mount can also trigger format. Do not remove the marker or use a damaged image
as an install target.

## Reproduce install, update, and persistence

```sh
make app-install-test
```

This builds two external SDK consumers in a temporary directory and uses one
temporary 64 MiB image through four normal boots. Boot one establishes SDFS
from the normal initrd and confirms the new program is absent. The host tool is
also asked to install while QEMU holds the image open and must refuse. After
QEMU exits, version A is installed and run from `/bin` on the same disk. A
larger version B replaces it; subsequent boots execute B and verify its exit
status. The integration test never touches `liwus_disk.img`.

The regular interactive `make run` uses `liwus_disk.img`, which persists across
boots and is preserved by `make clean`. To install into that image, exit QEMU
first, then pass `DISK=liwus_disk.img` explicitly.

## Current guarantees

- New install and replacement use fresh file blocks; old data blocks are
  released only after the new directory entry is prepared in the private copy.
- A file already present requires an explicit replacement request.
- Input ELF content and file size are validated before any disk publication.
- Existing unrelated directory entries and file data are retained.
- Runtime execution uses the existing shell lookup, `execve`, ELF loader, and
  ring-3 process path.
- No package database, dependency resolver, network transport, Linux ABI call,
  or kernel-specific application registry is added.

The filesystem does not provide atomic multi-block transactions or a crash
recovery guarantee for this operation. The v0.1 tool is intentionally offline.
