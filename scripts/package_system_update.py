#!/usr/bin/env python3
"""Create the uncompressed system TAR and manifest for the LIWUS OTA feed."""

import argparse
import pathlib
import shutil
import tarfile
import zlib


def crc32(path: pathlib.Path) -> int:
    value = 0
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            value = zlib.crc32(chunk, value)
    return value & 0xFFFFFFFF


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--boot-image", required=True, type=pathlib.Path)
    parser.add_argument("--initrd", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--date", required=True)
    parser.add_argument("--base-url", required=True)
    parser.add_argument(
        "--notes",
        default="Atualizacao completa do sistema LIWUS",
        help="short user-facing summary included in the OTA manifest",
    )
    args = parser.parse_args()

    args.output.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(args.boot_image, args.output / "hd_boot.img")
    archive_path = args.output / "system-update.tar"
    with tarfile.open(args.initrd, "r:*") as source, tarfile.open(
        archive_path, "w", format=tarfile.USTAR_FORMAT
    ) as target:
        for member in source:
            name = member.name
            while name.startswith("./"):
                name = name[2:]
            if not name or name in (".", "boot/hd_boot.img", "hd_boot.img"):
                continue
            if not (member.isfile() or member.isdir()):
                continue
            source_member = member
            member = tarfile.TarInfo(name)
            member.type = source_member.type
            member.mode = source_member.mode
            member.size = source_member.size
            member.linkname = source_member.linkname
            member.pax_headers = source_member.pax_headers.copy()
            member.uid = 0
            member.gid = 0
            member.uname = "root"
            member.gname = "root"
            member.mtime = 0
            data = source.extractfile(source_member) if source_member.isfile() else None
            target.addfile(member, data)

    boot_size = args.boot_image.stat().st_size
    archive_size = archive_path.stat().st_size
    base = args.base_url.rstrip("/")
    manifest = "\n".join(
        (
            f"VERSION={args.version}",
            f"DATE={args.date}",
            "PACKAGES=kilo,c_demo,neofetch,sysbench",
            f"NOTES={args.notes}",
            f"BOOT_IMAGE_URL={base}/hd_boot.img?v={args.version}",
            f"BOOT_IMAGE_SIZE={boot_size}",
            f"BOOT_IMAGE_CRC32={crc32(args.boot_image):08x}",
            f"SYSTEM_ARCHIVE_URL={base}/system-update.tar?v={args.version}",
            f"SYSTEM_ARCHIVE_SIZE={archive_size}",
            f"SYSTEM_ARCHIVE_CRC32={crc32(archive_path):08x}",
            "",
        )
    )
    (args.output / "system-v2.txt").write_text(manifest, encoding="ascii")
    print(f"Feed pronto: {args.output}")
    print(f"  Boot: {args.boot_image} ({boot_size} bytes, CRC32 {crc32(args.boot_image):08x})")
    print(f"  Sistema: {archive_path} ({archive_size} bytes, CRC32 {crc32(archive_path):08x})")


if __name__ == "__main__":
    main()
