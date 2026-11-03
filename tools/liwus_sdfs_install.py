#!/usr/bin/env python3
"""Install a static LIWUS ELF64 executable into an offline SDFS image.

The tool intentionally edits a temporary copy and publishes it with os.replace.
It is not a substitute for SDFS journaling and must only run while QEMU is off.
"""

import argparse
import fcntl
import os
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import time
import zlib

BLOCK = 4096
DATA = BLOCK - 4
MAX_BLOCKS = 4 * 1024 * 1024
USER_MIN = 0x400000
USER_MAX = 0x0000800000000000
ENTRY_FILE = 1
ENTRY_DIR = 2
ENTRY_DEL = 3


class InstallError(Exception):
    pass


def u16(data, off):
    return struct.unpack_from("<H", data, off)[0]


def u32(data, off):
    return struct.unpack_from("<I", data, off)[0]


def validate_elf(blob):
    if len(blob) < 64 or len(blob) > 0xFFFFFFFF:
        raise InstallError("ELF size is outside the SDFS file-size limit")
    if blob[:4] != b"\x7fELF" or blob[4:7] != b"\x02\x01\x01":
        raise InstallError("expected little-endian ELF64 version 1")
    etype, machine = struct.unpack_from("<HH", blob, 16)
    entry, phoff = struct.unpack_from("<QQ", blob, 24)
    ehsize, phentsize, phnum = struct.unpack_from("<HHH", blob, 52)
    if etype != 2 or machine != 62 or ehsize != 64:
        raise InstallError("expected static x86_64 ELF64 ET_EXEC")
    if phentsize != 56 or phnum == 0 or phnum > 128:
        raise InstallError("invalid ELF program-header table")
    if phoff > len(blob) or phnum * phentsize > len(blob) - phoff:
        raise InstallError("ELF program-header table is truncated")
    loads = []
    entry_ok = False
    for i in range(phnum):
        off = phoff + i * phentsize
        p_type, p_flags = struct.unpack_from("<II", blob, off)
        p_offset, p_vaddr, _, p_filesz, p_memsz, p_align = struct.unpack_from("<QQQQQQ", blob, off + 8)
        if p_type == 3:
            raise InstallError("PT_INTERP is unsupported; executable must be static")
        if p_type == 2:
            raise InstallError("PT_DYNAMIC is outside Native SDK v0.1")
        if p_type != 1 or p_memsz == 0:
            continue
        if p_filesz > p_memsz or p_offset > len(blob) or p_filesz > len(blob) - p_offset:
            raise InstallError("invalid or truncated PT_LOAD segment")
        if p_vaddr < USER_MIN or p_memsz > USER_MAX - p_vaddr:
            raise InstallError("PT_LOAD lies outside the current LIWUS user range")
        if p_align not in (0, 1) and (p_align & (p_align - 1)):
            raise InstallError("PT_LOAD alignment is not a power of two")
        if (p_flags & 3) == 3:
            raise InstallError("writable and executable PT_LOAD is rejected (W^X)")
        loads.append((p_vaddr, p_vaddr + p_memsz, p_flags))
        if p_flags & 1 and p_vaddr <= entry < p_vaddr + p_memsz:
            entry_ok = True
    if not loads or not entry_ok:
        raise InstallError("ELF has no loadable executable segment containing its entrypoint")
    for i, (start, end, flags) in enumerate(loads):
        for other_start, other_end, other_flags in loads[i + 1:]:
            page_start = max(start & ~0xFFF, other_start & ~0xFFF)
            page_end = min((end + 0xFFF) & ~0xFFF, (other_end + 0xFFF) & ~0xFFF)
            if page_start < page_end and ((flags | other_flags) & 3) == 3:
                raise InstallError("overlapping PT_LOAD pages would violate W^X")


class SDFS:
    def __init__(self, image):
        self.image = bytearray(image)
        if len(self.image) < BLOCK:
            raise InstallError("disk image is shorter than one SDFS block")
        sb = self.image[:BLOCK]
        if sb[:8] != b"SDFS0020":
            raise InstallError("unsupported SDFS superblock (expected SDFS V2)")
        (version, block_size, self.total, self.bitmap_start, self.bitmap_blocks,
         self.journal_start, self.journal_blocks, self.root) = struct.unpack_from("<8I", sb, 8)
        if version != 2 or block_size != BLOCK:
            raise InstallError("invalid SDFS V2 version or block size")
        if zlib.crc32(sb[:52]) & 0xFFFFFFFF != u32(sb, 52):
            raise InstallError("SDFS superblock CRC mismatch")
        if not (10 <= self.total <= MAX_BLOCKS) or len(self.image) < self.total * BLOCK:
            raise InstallError("SDFS block count exceeds image size or supported limit")
        if self.bitmap_blocks < 1 or self.bitmap_start < 1 or self.root >= self.total:
            raise InstallError("invalid SDFS metadata ranges")
        if self.bitmap_start + self.bitmap_blocks > self.total:
            raise InstallError("SDFS bitmap is outside the filesystem")
        if self.bitmap_blocks * BLOCK < (self.total + 7) // 8:
            raise InstallError("SDFS bitmap cannot represent every filesystem block")
        if self.journal_blocks and (self.journal_start < 1 or
                self.journal_start + self.journal_blocks > self.total):
            raise InstallError("SDFS journal is outside the filesystem")
        if self.bitmap_start + self.bitmap_blocks > self.journal_start or \
                self.root != self.journal_start + self.journal_blocks:
            raise InstallError("unsupported or overlapping SDFS V2 metadata layout")
        self.bitmap = bytearray(self._read_blocks(self.bitmap_start, self.bitmap_blocks))
        self.reserved = set(range(0, self.root + 1))
        # The root directory is a reserved-format block, but it is also walked
        # below and recorded as reachable like every other directory block.
        self.owned = set(range(0, self.root))
        for b in self.reserved:
            if not self.is_used(b):
                raise InstallError(f"SDFS metadata block {b} is marked free")
        self._walk_dir(self.root, set())

    def _range(self, block):
        if block < 0 or block >= self.total:
            raise InstallError(f"SDFS block {block} is out of range")
        return block * BLOCK

    def _read_blocks(self, start, count):
        if start < 0 or count < 0 or start + count > self.total:
            raise InstallError("SDFS block range is invalid")
        return self.image[start * BLOCK:(start + count) * BLOCK]

    def read_block(self, block):
        off = self._range(block)
        return self.image[off:off + BLOCK]

    def write_block(self, block, data):
        if len(data) != BLOCK:
            raise InstallError("internal error: SDFS block write size mismatch")
        off = self._range(block)
        self.image[off:off + BLOCK] = data

    def is_used(self, block):
        return bool(self.bitmap[block >> 3] & (1 << (block & 7)))

    def set_used(self, block, used):
        mask = 1 << (block & 7)
        if used:
            self.bitmap[block >> 3] |= mask
        else:
            self.bitmap[block >> 3] &= ~mask

    @staticmethod
    def _deleted_entry_size(block, pos):
        if pos + 2 > BLOCK:
            raise InstallError("truncated SDFS deleted entry")
        size = 2 + block[pos + 1] + 24
        if pos + size > BLOCK:
            raise InstallError("invalid SDFS deleted entry length")
        return size

    def allocate(self):
        for block in range(self.root + 1, self.total):
            if not self.is_used(block):
                if block in self.owned:
                    raise InstallError(f"SDFS block {block} is reachable but marked free")
                self.set_used(block, True)
                self.owned.add(block)
                return block
        raise InstallError("SDFS has no free blocks")

    def _walk_dir(self, first, directory_stack):
        if first in directory_stack:
            raise InstallError("SDFS directory cycle detected")
        directory_stack = set(directory_stack)
        directory_stack.add(first)
        chain = set()
        cur = first
        while cur:
            if cur >= self.total or cur in chain or cur in self.owned:
                raise InstallError("invalid or aliased SDFS directory chain")
            if not self.is_used(cur):
                raise InstallError(f"SDFS directory block {cur} is marked free")
            chain.add(cur)
            self.owned.add(cur)
            block = self.read_block(cur)
            nxt, count = struct.unpack_from("<IH", block, 0)
            pos = 6
            for _ in range(count):
                if pos + 2 > BLOCK:
                    raise InstallError("truncated SDFS directory entry")
                kind, nlen = block[pos], block[pos + 1]
                if kind == 0:
                    pos += 2
                    continue
                if kind == ENTRY_DEL:
                    pos += self._deleted_entry_size(block, pos)
                    continue
                size = 2 + nlen + 24
                if nlen == 0 or pos + size > BLOCK:
                    raise InstallError("invalid SDFS directory entry length")
                name_bytes = block[pos + 2:pos + 2 + nlen]
                try:
                    name = name_bytes.decode("utf-8")
                except UnicodeDecodeError as exc:
                    raise InstallError("SDFS name is not UTF-8") from exc
                start, fsize = struct.unpack_from("<II", block, pos + 2 + nlen)
                if kind == ENTRY_FILE:
                    self._walk_file(start, fsize)
                elif kind == ENTRY_DIR:
                    self._walk_dir(start, directory_stack)
                else:
                    raise InstallError(f"unknown SDFS entry type {kind}")
                pos += size
            if nxt and nxt >= self.total:
                raise InstallError("SDFS directory next-block pointer is invalid")
            cur = nxt

    def _walk_file(self, first, size):
        count = max(1, (size + DATA - 1) // DATA)
        cur = first
        for index in range(count):
            if cur == 0 or cur >= self.total or cur in self.owned:
                raise InstallError("invalid, short, or aliased SDFS file chain")
            if not self.is_used(cur):
                raise InstallError(f"SDFS file block {cur} is marked free")
            self.owned.add(cur)
            nxt = u32(self.read_block(cur), 0)
            if index + 1 == count:
                if nxt != 0:
                    raise InstallError("SDFS file chain is longer than file_size")
            else:
                cur = nxt

    def find_dir(self, parent, name):
        cur = parent
        while cur:
            block = self.read_block(cur)
            nxt, count = struct.unpack_from("<IH", block, 0)
            pos = 6
            for _ in range(count):
                if pos + 2 > BLOCK:
                    raise InstallError("truncated SDFS directory entry")
                typ, nlen = block[pos], block[pos + 1]
                if typ == 0:
                    pos += 2
                    continue
                if typ == ENTRY_DEL:
                    pos += self._deleted_entry_size(block, pos)
                    continue
                size = 2 + nlen + 24
                if nlen == 0 or pos + size > BLOCK:
                    raise InstallError("invalid SDFS directory entry length")
                raw = block[pos + 2:pos + 2 + nlen]
                if raw.decode("utf-8") == name:
                    return cur, pos, typ, u32(block, pos + 2 + nlen), u32(block, pos + 6 + nlen), size
                pos += size
            cur = nxt
        return None

    def resolve_dir(self, path):
        cur = self.root
        for component in path.strip("/").split("/") if path != "/" else []:
            found = self.find_dir(cur, component)
            if not found or found[2] != ENTRY_DIR:
                raise InstallError(f"required directory {path!r} does not exist")
            cur = found[3]
        return cur

    def new_file_chain(self, data):
        count = max(1, (len(data) + DATA - 1) // DATA)
        blocks = [self.allocate() for _ in range(count)]
        for index, block in enumerate(blocks):
            chunk = data[index * DATA:(index + 1) * DATA]
            nxt = blocks[index + 1] if index + 1 < count else 0
            self.write_block(block, struct.pack("<I", nxt) + chunk + bytes(DATA - len(chunk)))
        return blocks[0], blocks

    def install(self, name, data, replace, directory="/bin"):
        if not name or len(name.encode("utf-8")) > 255 or name in (".", ".."):
            raise InstallError("application filename exceeds SDFS name constraints")
        if "/" in name:
            raise InstallError("target filename must be one path component")
        parent = self.resolve_dir(directory)
        found = self.find_dir(parent, name)
        if found and found[2] != ENTRY_FILE:
            raise InstallError(f"/bin/{name} exists and is not a regular file")
        if found and not replace:
            raise InstallError(f"/bin/{name} already exists; pass --replace to update")
        old_chain = []
        if found:
            cur = found[3]
            old_size = found[4]
            for _ in range(max(1, (old_size + DATA - 1) // DATA)):
                old_chain.append(cur)
                cur = u32(self.read_block(cur), 0)
        first, new_chain = self.new_file_chain(data)
        if found:
            block = bytearray(self.read_block(found[0]))
            struct.pack_into("<II", block, found[1] + 2 + len(name.encode()), first, len(data))
            # Preserve created time, uid, gid and permissions; update modified time.
            struct.pack_into("<I", block, found[1] + 2 + len(name.encode()) + 12, int(time.time()))
            self.write_block(found[0], block)
            for old in old_chain:
                self.set_used(old, False)
                self.owned.discard(old)
        else:
            encoded = name.encode("utf-8")
            entry = struct.pack("<BB", ENTRY_FILE, len(encoded)) + encoded + struct.pack(
                "<IIIIHHHH", first, len(data), 0, int(time.time()), 0, 0, 0o755, 0)
            cur = parent
            while True:
                block = bytearray(self.read_block(cur))
                nxt, entry_count = struct.unpack_from("<IH", block, 0)
                pos, slot = 6, None
                for _ in range(entry_count):
                    if pos + 2 > BLOCK:
                        raise InstallError("truncated SDFS directory entry")
                    typ, nlen = block[pos], block[pos + 1]
                    if typ == 0:
                        size = 2
                    elif typ == ENTRY_DEL:
                        size = self._deleted_entry_size(block, pos)
                    else:
                        size = 2 + nlen + 24
                        if nlen == 0 or pos + size > BLOCK:
                            raise InstallError("invalid SDFS directory entry length")
                    if typ == 0 and size >= len(entry):
                        slot = pos
                        break
                    if typ == ENTRY_DEL and size == len(entry):
                        slot = pos
                        break
                    pos += size
                if slot is None and pos + len(entry) <= BLOCK:
                    slot = pos
                    struct.pack_into("<H", block, 4, entry_count + 1)
                if slot is not None:
                    block[slot:slot + len(entry)] = entry
                    self.write_block(cur, block)
                    break
                if nxt:
                    cur = nxt
                    continue
                newdir = self.allocate()
                newblock = bytearray(BLOCK)
                struct.pack_into("<IH", newblock, 0, 0, 1)
                newblock[6:6 + len(entry)] = entry
                self.write_block(newdir, newblock)
                struct.pack_into("<I", block, 0, newdir)
                self.write_block(cur, block)
                break
        bitmap_off = self.bitmap_start * BLOCK
        bitmap_length = self.bitmap_blocks * BLOCK
        self.image[bitmap_off:bitmap_off + bitmap_length] = self.bitmap
        # Validate the resulting image by reconstructing the ownership graph.
        image = bytes(self.image)
        checked = SDFS(image)
        if not all(checked.is_used(b) for b in checked.owned):
            raise InstallError("post-install SDFS allocation check failed")
        return len(new_chain), bool(found)


def reject_open_image(path, source_stat):
    proc = Path("/proc")
    if not proc.is_dir():
        raise InstallError("cannot verify whether the disk image is open (missing /proc)")
    for process in proc.iterdir():
        if not process.name.isdigit():
            continue
        fd_dir = process / "fd"
        try:
            fds = list(fd_dir.iterdir())
        except (PermissionError, FileNotFoundError, ProcessLookupError):
            continue
        for fd in fds:
            try:
                st = fd.stat()
            except (PermissionError, FileNotFoundError, ProcessLookupError):
                continue
            if (st.st_dev, st.st_ino) == (source_stat.st_dev, source_stat.st_ino):
                if process.name == str(os.getpid()):
                    continue
                try:
                    command = (process / "comm").read_text().strip()
                except OSError:
                    command = "unknown process"
                raise InstallError(f"disk image is open by PID {process.name} ({command}); stop QEMU first")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument("--app", type=Path, help="input static LIWUS .lxe")
    source.add_argument("--file", type=Path, help="input regular file data")
    parser.add_argument("--path", help="absolute SDFS destination for --file")
    parser.add_argument("--disk", required=True, type=Path, help="existing offline SDFS V2 raw image")
    parser.add_argument("--replace", action="store_true", help="replace existing /bin executable")
    args = parser.parse_args()
    if args.app:
        if args.app.suffix != ".lxe" or not args.app.is_file():
            raise InstallError("APP must name an existing .lxe file")
        destination = f"/bin/{args.app.name}"
    else:
        if not args.file.is_file() or not args.path or not args.path.startswith("/"):
            raise InstallError("FILE must be regular data and PATH an absolute SDFS path")
        components = args.path.rstrip("/").split("/")
        if len(components) < 2 or any(part in ("", ".", "..") for part in components[1:]):
            raise InstallError("PATH must name a file without empty, . or .. components")
        destination = args.path.rstrip("/")
    disk = args.disk.resolve(strict=True)
    if not disk.is_file():
        raise InstallError("DISK must be a regular disk image file")
    file_data = (args.app or args.file).read_bytes()
    if args.app:
        validate_elf(file_data)
    st = disk.stat()
    reject_open_image(disk, st)
    with disk.open("r+b", buffering=0) as source:
        try:
            fcntl.lockf(source, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as exc:
            raise InstallError("could not lock disk image; it may be in use") from exc
        reject_open_image(disk, st)
        original = source.read()
        sdfs = SDFS(original)
        parent, name = destination.rsplit("/", 1)
        parent = parent or "/"
        blocks, updated = sdfs.install(name, file_data, args.replace, parent)
        temp_fd, temp_name = tempfile.mkstemp(prefix=f".{disk.name}.install-", dir=disk.parent)
        try:
            with os.fdopen(temp_fd, "wb") as target:
                target.write(sdfs.image)
                target.flush()
                os.fsync(target.fileno())
                os.fchmod(target.fileno(), st.st_mode & 0o7777)
            current = disk.stat()
            if (current.st_dev, current.st_ino, current.st_size, current.st_mtime_ns) != \
                    (st.st_dev, st.st_ino, st.st_size, st.st_mtime_ns):
                raise InstallError("disk image changed while installer was preparing update")
            reject_open_image(disk, st)
            os.replace(temp_name, disk)
            dir_fd = os.open(disk.parent, os.O_RDONLY)
            try:
                os.fsync(dir_fd)
            finally:
                os.close(dir_fd)
        finally:
            if os.path.exists(temp_name):
                os.unlink(temp_name)
    action = "updated" if updated else "installed"
    print(f"SDFS {action}: {destination} ({len(file_data)} bytes, {blocks} blocks)")
    print("Disk image published from a validated temporary copy; crash safety of SDFS metadata is not implied.")


if __name__ == "__main__":
    try:
        main()
    except (InstallError, OSError) as error:
        print(f"liwus-sdfs-install: error: {error}", file=sys.stderr)
        raise SystemExit(2)
