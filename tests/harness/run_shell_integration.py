#!/usr/bin/env python3
"""Exercise the real userspace shell through QEMU's serial console."""

from pathlib import Path
import os
import select
import shutil
import subprocess
import sys
import tempfile
import time


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    artifact = root / "build/external-test.lxe"
    if not artifact.is_file():
        raise SystemExit("missing build/external-test.lxe; run make external-sdk-build")

    logs = root / "test-logs"
    logs.mkdir(exist_ok=True)
    log_path = logs / "shell-integration.serial"
    transcript = bytearray()
    pending = bytearray()

    with tempfile.TemporaryDirectory(prefix="liwus-shell-qemu-") as temp:
        temp_path = Path(temp)
        image_tree = temp_path / "isodir"
        shutil.copytree(root / "isodir", image_tree)
        initrd = temp_path / "initrd.tar"
        shutil.copy2(root / "initrd.tar", initrd)
        subprocess.run([
            "tar", "-rf", str(initrd), "--format=ustar",
            "-C", str(artifact.parent), artifact.name,
        ], check=True)
        subprocess.run([
            "tar", "-rf", str(initrd), "--format=ustar",
            f"--transform=s|{artifact.name}|bin/{artifact.name}|",
            "-C", str(artifact.parent), artifact.name,
        ], check=True)
        shutil.copy2(initrd, image_tree / "boot/initrd.tar")
        iso = temp_path / "liwus-shell-test.iso"
        subprocess.run(["grub-mkrescue", "-o", str(iso), str(image_tree)],
                       check=True, stdout=subprocess.DEVNULL)

        command = [
            "qemu-system-x86_64", "-machine", "q35", "-m", "2048",
            "-display", "none", "-monitor", "none", "-serial", "stdio",
            "-no-reboot", "-cdrom", str(iso),
        ]
        process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   bufsize=0)

        def expect(marker: bytes, timeout: float = 90.0) -> None:
            deadline = time.monotonic() + timeout
            while marker not in pending:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(f"timed out waiting for {marker!r}")
                ready, _, _ = select.select([process.stdout], [], [], remaining)
                if not ready:
                    continue
                chunk = os.read(process.stdout.fileno(), 4096)
                if not chunk:
                    raise RuntimeError("QEMU closed the serial console")
                transcript.extend(chunk)
                pending.extend(chunk)
            end = pending.index(marker) + len(marker)
            del pending[:end]

        def send(command_bytes: bytes) -> None:
            process.stdin.write(command_bytes)
            process.stdin.flush()

        try:
            expect(b"liwus> ")

            send(b"help\n")
            expect(b"Built-ins: help, version, exit\n")
            expect(b"liwus> ")

            send(b"help \"unsupported\"\n")
            expect(b"liwsh: quotes, escapes, and shell operators are unsupported\n")
            expect(b"liwus> ")

            send(b"x" * 300 + b"\n")
            expect(b"liwsh: input line too long\n")
            expect(b"liwus> ")

            send(b"native-hello.lxe\n")
            expect(b"Hello from LIWUS OS SDK v0.1!\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            send(b"external-test.lxe demo\n")
            expect(b"[EXTERNAL SDK] running; pid=")
            expect(b"External LIWUS application running.\n")
            expect(b"[liwsh] exit status 23\n")
            expect(b"liwus> ")

            send(b"external-test.lxe extra\n")
            expect(b"[liwsh] exit status 91\n")
            expect(b"liwus> ")

            send(b"missing-program.lxe\n")
            expect(b"liwsh: executable not found or invalid: missing-program.lxe\n")
            expect(b"[liwsh] exit status 127\n")
            expect(b"liwus> ")

            send(b"exit\n")
            expect(b"LIWUS OS\nNative Shell v0.1\nType help for commands.\n")
            expect(b"liwus> ")
            send(b"\x04")
            expect(b"LIWUS OS\nNative Shell v0.1\nType help for commands.\n")

            transcript.extend(b"\n[SHELL TEST] console input, commands, child lifecycle, EOF, restart PASS\n")
            log_path.write_bytes(transcript)
            print(f"SHELL QEMU INTEGRATION PASS: {log_path}")
            return 0
        except Exception as error:  # retain the serial evidence on every failure
            transcript.extend(f"\n[SHELL TEST FAIL] {error}\n".encode())
            log_path.write_bytes(transcript)
            print(f"SHELL QEMU INTEGRATION FAIL: {error}\nserial log: {log_path}",
                  file=sys.stderr)
            return 1
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
