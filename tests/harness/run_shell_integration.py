#!/usr/bin/env python3
"""Exercise the real userspace shell through QEMU's serial console."""

from pathlib import Path
import os
import select
import socket
import shutil
import subprocess
import sys
import tempfile
import time


def main() -> int:
    root = Path(__file__).resolve().parents[2]
    artifact = root / "build/external-test.lxe"
    native_hello = root / "build/liwus-sdk/bin/native-hello.lxe"
    if not artifact.is_file():
        raise SystemExit("missing build/external-test.lxe; run make external-sdk-build")

    logs = root / "test-logs"
    logs.mkdir(exist_ok=True)
    log_path = logs / "shell-integration.serial"
    transcript = bytearray()
    pending = bytearray()
    host_tx_bytes = 0
    last_command = b""
    send_delay = float(os.environ.get("LIW_SHELL_SEND_DELAY", "0.2"))

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
        subprocess.run([
            "tar", "-rf", str(initrd), "--format=ustar",
            "--transform=s|native-hello.lxe|bin/demo.lxe|",
            "-C", str(native_hello.parent), native_hello.name,
        ], check=True)
        subprocess.run([
            "tar", "-rf", str(initrd), "--format=ustar",
            "--transform=s|native-hello.lxe|bin/collision.lxe|",
            "-C", str(native_hello.parent), native_hello.name,
        ], check=True)
        with tempfile.TemporaryDirectory(prefix="liwus-shell-files-") as files:
            files_path = Path(files)
            (files_path / "collision").write_bytes(b"not an ELF executable")
            (files_path / "collision").chmod(0o755)
            (files_path / "empty.txt").write_bytes(b"")
            payload = bytes(range(256)) * 3
            (files_path / "binary.bin").write_bytes(payload)
            copy_sizes = (0, 1, 15, 16, 255, 256, 4095, 4096, 4097,
                          8192, 65536)
            copy_payloads = {}
            for size in copy_sizes:
                copy_data = bytes((index * 37 + 11) & 0xff
                                  for index in range(size))
                name = f"copy-{size}.bin"
                (files_path / name).write_bytes(copy_data)
                copy_payloads[size] = (name, copy_data)
            for source, destination in (("collision", "bin/collision"),
                                       ("empty.txt", "empty.txt"),
                                       ("binary.bin", "binary.bin")):
                subprocess.run([
                    "tar", "-rf", str(initrd), "--format=ustar",
                    f"--transform=s|{source}|{destination}|",
                    "-C", str(files_path), source,
                ], check=True)
            for name, _ in copy_payloads.values():
                subprocess.run([
                    "tar", "-rf", str(initrd), "--format=ustar",
                    "-C", str(files_path), name,
                ], check=True)
        shutil.copy2(initrd, image_tree / "boot/initrd.tar")
        iso = temp_path / "liwus-shell-test.iso"
        subprocess.run(["grub-mkrescue", "-o", str(iso), str(image_tree)],
                       check=True, stdout=subprocess.DEVNULL)
        disk = temp_path / "shell-test-disk.img"
        with disk.open("wb") as image:
            image.truncate(64 * 1024 * 1024)

        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as probe:
            probe.bind(("127.0.0.1", 0))
            serial_port = probe.getsockname()[1]

        command = [
            "qemu-system-x86_64", "-machine", "q35", "-m", "2048",
            "-display", "none", "-monitor", "none", "-serial",
            f"tcp:127.0.0.1:{serial_port},server=on,wait=off",
            "-no-reboot", "-drive", f"file={disk},format=raw,if=none,id=disk",
            "-device", "ahci,id=ahci", "-device", "ide-hd,drive=disk,bus=ahci.0",
            "-cdrom", str(iso),
        ]
        process = subprocess.Popen(command, stdin=subprocess.DEVNULL,
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   bufsize=0)
        serial = None
        connect_deadline = time.monotonic() + 10.0
        while serial is None and time.monotonic() < connect_deadline:
            if process.poll() is not None:
                raise RuntimeError("QEMU exited before serial socket connected")
            try:
                serial = socket.create_connection(("127.0.0.1", serial_port), timeout=0.2)
            except OSError:
                time.sleep(0.05)
        if serial is None:
            process.terminate()
            process.wait(timeout=5)
            raise TimeoutError("QEMU serial socket did not become available")
        serial.setblocking(False)

        def expect(marker: bytes, timeout: float = 90.0) -> None:
            nonlocal host_tx_bytes
            timeout = float(os.environ.get("LIW_SHELL_EXPECT_TIMEOUT", timeout))
            deadline = time.monotonic() + timeout
            while marker not in pending:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise TimeoutError(
                        f"timed out waiting for {marker!r}; host_tx={host_tx_bytes}; "
                        f"last_command={last_command!r}"
                    )
                ready, _, _ = select.select([serial], [], [], remaining)
                if not ready:
                    continue
                try:
                    chunk = serial.recv(4096)
                except BlockingIOError:
                    continue
                if not chunk:
                    raise RuntimeError("QEMU closed the serial socket")
                transcript.extend(chunk)
                pending.extend(chunk)
            end = pending.index(marker) + len(marker)
            del pending[:end]

        def send(command_bytes: bytes) -> None:
            nonlocal host_tx_bytes, last_command
            # Let the guest reach its blocking input path after each prompt.
            time.sleep(0.15)
            last_command = command_bytes
            for offset in range(0, len(command_bytes), 4):
                # Small groups keep each UART FIFO well below its capacity,
                # while the pause lets the guest's polling reader drain it.
                serial.sendall(command_bytes[offset:offset + 4])
                host_tx_bytes += len(command_bytes[offset:offset + 4])
                time.sleep(send_delay)

        def process_cpu_ticks() -> int:
            stat = Path(f"/proc/{process.pid}/stat").read_text()
            fields = stat[stat.rfind(")") + 2:].split()
            return int(fields[11]) + int(fields[12])

        try:
            expect(b"liwus> ")

            # With no bytes arriving, the shell must block and let the guest
            # halt between bounded input polls instead of spinning in userspace.
            cpu_start = process_cpu_ticks()
            time.sleep(1.0)
            cpu_ticks = process_cpu_ticks() - cpu_start
            cpu_seconds = cpu_ticks / os.sysconf("SC_CLK_TCK")
            if cpu_seconds > 0.75:
                raise RuntimeError(
                    f"idle shell consumed {cpu_seconds:.2f} QEMU CPU seconds in 1 second"
                )

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

            for command in (b"ls", b"ls /"):
                send(command + b"\n")
                expect(b"readme.txt\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")

            for command in (b"native-hello", b"/bin/native-hello",
                            b"/bin/native-hello.lxe"):
                send(command + b"\n")
                expect(b"Hello from LIWUS OS SDK v0.1!\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")

            send(b"ls /bin\n")
            for name in (b"native-hello.lxe", b"liwsh.lxe", b"ls.lxe",
                         b"cat.lxe", b"mkdir.lxe", b"rm.lxe", b"mv.lxe",
                         b"cp.lxe", b"touch.lxe", b"echo.lxe"):
                expect(name + b"\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            for command in (b"ls", b"ls.lxe", b"/bin/ls", b"/bin/ls.lxe"):
                send(command + b" /bin\n")
                expect(b"cat.lxe\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")

            readme = b"Welcome to LIWUS OS!\n"
            for command in (b"cat /readme.txt", b"cat.lxe /readme.txt"):
                send(command + b"\n")
                expect(readme)
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")

            send(b"cat /empty.txt\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            binary_payload = bytes(range(256)) * 3
            send(b"cat /binary.bin\n")
            expect(binary_payload)
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            send(b"demo\n")
            expect(b"Hello from LIWUS OS SDK v0.1!\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            send(b"collision\n")
            expect(b"liwsh: cannot execute collision: invalid executable\n")
            expect(b"[liwsh] exit status 126\n")
            expect(b"liwus> ")

            send(b"ls /missing-directory\n")
            expect(b"ls: cannot open directory\n")
            expect(b"[liwsh] exit status 1\n")
            expect(b"liwus> ")

            send(b"cat /bin\n")
            expect(b"cat: path is a directory\n")
            expect(b"[liwsh] exit status 1\n")
            expect(b"liwus> ")

            send(b"cat /missing-file\n")
            expect(b"cat: cannot open file\n")
            expect(b"[liwsh] exit status 1\n")
            expect(b"liwus> ")

            send(b"external-test.lxe demo\n")
            expect(b"[EXTERNAL SDK] running; pid=")
            expect(b"External LIWUS application running.\n")
            expect(b"[liwsh] exit status 23\n")
            expect(b"liwus> ")

            send(b"external-test.lxe extra\n")
            expect(b"[liwsh] exit status 91\n")
            expect(b"liwus> ")

            utility_commands = (
                (b"echo Native tools ready", b"Native tools ready\n"),
                (b"mkdir /toolbox-test", None),
                (b"mkdir -p /work/projeto", None),
                (b"mkdir -p /work/projeto", None),
                (b"touch /work/hello.txt", None),
                (b"cp /work/hello.txt /work/projeto", None),
                (b"ls /work/projeto", b"hello.txt\n"),
                (b"cp /readme.txt /work/projeto", None),
                (b"cat /work/projeto/readme.txt", readme),
                (b"touch /toolbox-test/empty", None),
                (b"cp /readme.txt /toolbox-test/readme.copy", None),
                (b"mv /toolbox-test/readme.copy /toolbox-test/readme.txt", None),
                (b"cp /empty.txt /toolbox-test/empty.copy", None),
                (b"cp /binary.bin /toolbox-test/binary.copy", None),
                (b"cat /toolbox-test/readme.txt", readme),
                (b"cat /toolbox-test/binary.copy", binary_payload),
                (b"rm /toolbox-test/readme.txt", None),
                (b"ls /toolbox-test", b"binary.copy\n"),
            )
            for shell_command, output in utility_commands:
                send(shell_command + b"\n")
                if output is not None:
                    expect(output)
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")

            send(b"echo\n")
            expect(b"echo\n")  # cooked-input echo
            expect(b"\n")      # echo with no arguments emits a newline
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            # Existing-file touch must preserve content, and cp must refuse
            # replacing an existing destination instead of truncating it.
            send(b"touch /toolbox-test/binary.copy\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")
            send(b"cp /empty.txt /toolbox-test/binary.copy\n")
            expect(b"cp: cannot create destination (it may exist)\n")
            expect(b"[liwsh] exit status 1\n")
            expect(b"liwus> ")
            send(b"cat /toolbox-test/binary.copy\n")
            expect(binary_payload)
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            for size, (name, expected_bytes) in copy_payloads.items():
                source = f"/{name}".encode()
                destination = f"/toolbox-test/{name}.copy".encode()
                send(b"cp " + source + b" " + destination + b"\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")
                send(b"cat " + destination + b"\n")
                if expected_bytes:
                    expect(expected_bytes)
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")

            # Exercise the complete UART -> stdin -> shell -> fork/exec/wait
            # path over a long real guest session. Each reply carries a unique
            # sequence number, so a missing, duplicated, stale, or reordered
            # command cannot satisfy the next check.
            for sequence in range(100):
                token = f"console-stress-{sequence:03d}".encode()
                send(b"echo " + token + b"\n")
                expect(b"echo " + token + b"\n")  # cooked-input echo
                expect(token + b"\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")

            send(b"missing-program.lxe\n")
            expect(b"liwsh: command not found: missing-program.lxe\n")
            expect(b"[liwsh] exit status 127\n")
            expect(b"liwus> ")

            send(b"x" * 250 + b"\n")
            expect(b"liwsh: executable path is too long\n")
            expect(b"[liwsh] exit status 126\n")
            expect(b"liwus> ")

            send(b"exit\n")
            expect(b"LIWUS OS\nNative Shell v0.1\nType help for commands.\n")
            expect(b"liwus> ")
            send(b"\x04")
            expect(b"LIWUS OS\nNative Shell v0.1\nType help for commands.\n")

            transcript.extend(
                f"\n[SHELL IDLE] QEMU CPU seconds per idle second: {cpu_seconds:.3f} PASS\n"
                "[SHELL TEST] console input, 100-command sequence, utilities, child lifecycle, EOF, restart PASS\n"
                .encode()
            )
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
            serial.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    raise SystemExit(main())
