#!/usr/bin/env python3
"""Host regression tests for offline SDFS application installation."""

import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "liwus_sdfs_install", ROOT / "tools/liwus_sdfs_install.py")
INSTALLER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(INSTALLER)


def empty_sdfs(blocks=128):
    image = bytearray(blocks * INSTALLER.BLOCK)
    bitmap_start, bitmap_blocks = 1, 1
    journal_start, journal_blocks = 2, 16
    root = journal_start + journal_blocks
    struct.pack_into("<8s8I", image, 0, b"SDFS0020", 2, 4096, blocks,
                     bitmap_start, bitmap_blocks, journal_start,
                     journal_blocks, root)
    struct.pack_into("<I", image, 52, zlib.crc32(image[:52]) & 0xFFFFFFFF)
    bitmap = bytearray(INSTALLER.BLOCK)
    for block in range(root + 1):
        bitmap[block >> 3] |= 1 << (block & 7)
    image[INSTALLER.BLOCK:2 * INSTALLER.BLOCK] = bitmap
    return image


def static_elf(payload=b"\xc3", extra=0):
    phoff = 64
    phentsize = 56
    phnum = 3
    content = payload + (b"B" * extra)
    text_offset = 0x1000
    ro_offset = 0x2000
    rw_offset = 0x3000
    blob = bytearray(rw_offset + 1)
    blob[:16] = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into("<HHIQQQIHHHHHH", blob, 16, 2, 62, 1, 0x400000,
                     phoff, 0, 0, 64, phentsize, phnum, 0, 0, 0)
    phdrs = [
        (1, 5, text_offset, 0x400000, len(content), len(content), 0x1000),
        (1, 4, ro_offset, 0x402000, 1, 1, 0x1000),
        (1, 6, rw_offset, 0x403000, 1, 1, 0x1000),
    ]
    for i, ph in enumerate(phdrs):
        struct.pack_into("<IIQQQQQQ", blob, phoff + i * phentsize, *ph[:2],
                         ph[2], ph[3], 0, ph[4], ph[5], ph[6])
    blob[text_offset:text_offset + len(content)] = content
    blob[ro_offset] = 0x52
    blob[rw_offset] = 0x57
    return bytes(blob)


def add_bin(image):
    sdfs = INSTALLER.SDFS(image)
    root = bytearray(sdfs.read_block(sdfs.root))
    bin_block = sdfs.root + 1
    # Allocate the first block after the format-reserved region for /bin.
    sdfs.set_used(bin_block, True)
    struct.pack_into("<I", root, 6, bin_block)
    struct.pack_into("<H", root, 4, 1)
    name = b"bin"
    entry = struct.pack("<BB", 2, len(name)) + name + struct.pack(
        "<IIIIHHHH", bin_block, 0, 0, 0, 0, 0, 0x1C9, 0)
    root[6:6 + len(entry)] = entry
    sdfs.write_block(sdfs.root, root)
    sdfs.write_block(bin_block, bytes(INSTALLER.BLOCK))
    off = sdfs.bitmap_start * INSTALLER.BLOCK
    sdfs.image[off:off + len(sdfs.bitmap)] = sdfs.bitmap
    return bytes(sdfs.image)


class InstallerTests(unittest.TestCase):
    def test_elf_validation_static_x86_64(self):
        INSTALLER.validate_elf(static_elf())

    def test_rejects_wrong_suffix_semantics_or_truncation(self):
        for blob in (b"no elf", static_elf()[:80]):
            with self.assertRaises(INSTALLER.InstallError):
                INSTALLER.validate_elf(blob)
        bad = bytearray(static_elf())
        struct.pack_into("<H", bad, 16, 3)
        with self.assertRaises(INSTALLER.InstallError):
            INSTALLER.validate_elf(bytes(bad))

    def test_install_and_replace_preserves_neighbors_and_validates_chains(self):
        image = add_bin(empty_sdfs())
        fs = INSTALLER.SDFS(image)
        fs.install("demo.lxe", static_elf(b"A"), False)
        installed = INSTALLER.SDFS(fs.image)
        bin_block = installed.resolve_dir("/bin")
        demo = installed.find_dir(bin_block, "demo.lxe")
        self.assertIsNotNone(demo)
        self.assertEqual(INSTALLER.u16(installed.read_block(demo[0]),
                                      demo[1] + 2 + len("demo.lxe") + 20),
                         0x314)
        old_size = demo[4]
        neighbor = static_elf(b"neighbor")
        installed.install("neighbor.lxe", neighbor, False)
        before_replace = bytes(installed.image)
        installed.install("demo.lxe", static_elf(b"B", 9000), True)
        after = INSTALLER.SDFS(installed.image)
        bin_block = after.resolve_dir("/bin")
        new_demo = after.find_dir(bin_block, "demo.lxe")
        self.assertGreater(new_demo[4], old_size)
        self.assertIsNotNone(after.find_dir(bin_block, "neighbor.lxe"))
        self.assertGreater(len(before_replace), 0)
        after.install("demo.lxe", static_elf(b"C"), True)
        final = INSTALLER.SDFS(after.image)
        self.assertEqual(final.find_dir(final.resolve_dir("/bin"), "demo.lxe")[4],
                         len(static_elf(b"C")))

    def test_rejects_no_replace_and_corrupt_superblock(self):
        fs = INSTALLER.SDFS(add_bin(empty_sdfs()))
        fs.install("demo.lxe", static_elf(), False)
        with self.assertRaises(INSTALLER.InstallError):
            fs.install("demo.lxe", static_elf(), False)
        corrupt = bytearray(fs.image)
        corrupt[52] ^= 0x80
        with self.assertRaises(INSTALLER.InstallError):
            INSTALLER.SDFS(corrupt)

    def test_full_filesystem_leaves_original_copy_unchanged(self):
        image = add_bin(empty_sdfs(24))
        original = bytes(image)
        fs = INSTALLER.SDFS(image)
        with self.assertRaises(INSTALLER.InstallError):
            fs.install("large.lxe", static_elf(b"x", 100000), False)
        self.assertEqual(bytes(image), original)


if __name__ == "__main__":
    unittest.main(verbosity=2)
