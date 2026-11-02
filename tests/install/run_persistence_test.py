#!/usr/bin/env python3
"""End-to-end app install, update, and reboot test on one temporary disk."""

import os
from pathlib import Path
import select
import shutil
import socket
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "tools/liwus_sdfs_install.py"
LOG_DIR = ROOT / "test-logs"


def run(command, cwd=ROOT):
    subprocess.run(command, cwd=cwd, check=True)


def compile_app(prefix, output, version, status, filler):
    source = output.parent / f"{version}.c"
    source.write_text(f'''#include <liwus/liwus.h>
int main(int argc, char **argv, char **envp) {{
    (void)argv; (void)envp;
    if (argc != 1) return 80;
    static const char message[] = "LIWUS persistent demo — version {version}\\n";
    static volatile unsigned char payload[{filler}] = {{ 7 }};
    unsigned int checksum = 0;
    for (unsigned int i = 0; i < sizeof(payload); ++i) checksum += payload[i];
    if (checksum != 7) return 81;
    if (liw_write_all(1, message, sizeof(message) - 1) < 0) return 82;
    return {status};
}}
''')
    obj = output.parent / f"{version}.o"
    run([
        "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
        "-ffreestanding", "-fno-builtin", "-fno-pie", "-fno-pic",
        "-fno-stack-protector", "-m64", "-mno-red-zone",
        f"-I{prefix / 'include'}", "-c", str(source), "-o", str(obj),
    ], output.parent)
    run([
        "ld", "-m", "elf_x86_64", "-nostdlib", "-static", "--build-id=none",
        "-T", str(prefix / "share/liwus/elf64.ld"), "-o", str(output),
        str(prefix / "lib/crt0.o"), str(obj), str(prefix / "lib/libliwus.a"),
    ], output.parent)


class QemuShell:
    def __init__(self, iso, disk, qmp):
        self.command = [
            "qemu-system-x86_64", "-machine", "q35", "-m", "2048",
            "-display", "none", "-monitor", "none", "-serial", "stdio",
            "-no-reboot", "-drive", f"file={disk},format=raw,if=none,id=disk",
            "-device", "ahci,id=ahci", "-device", "ide-hd,drive=disk,bus=ahci.0",
            "-qmp", f"unix:{qmp},server=on,wait=off", "-cdrom", str(iso),
        ]
        self.proc = subprocess.Popen(self.command, stdin=subprocess.PIPE,
                                     stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                     bufsize=0)
        self.pending = bytearray()
        self.log = bytearray()

    def expect(self, marker, timeout=120):
        deadline = time.monotonic() + timeout
        while marker not in self.pending:
            if self.proc.poll() is not None:
                raise RuntimeError(f"QEMU exited before {marker!r}")
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError(f"timeout waiting for serial marker {marker!r}")
            ready, _, _ = select.select([self.proc.stdout], [], [], remaining)
            if not ready:
                continue
            chunk = os.read(self.proc.stdout.fileno(), 8192)
            if not chunk:
                raise RuntimeError("QEMU closed serial output")
            self.log.extend(chunk)
            self.pending.extend(chunk)
        end = self.pending.index(marker) + len(marker)
        del self.pending[:end]

    def send(self, data):
        self.proc.stdin.write(data)
        self.proc.stdin.flush()

    def quit(self, qmp_path):
        deadline = time.monotonic() + 20
        while not Path(qmp_path).exists() and time.monotonic() < deadline:
            time.sleep(0.05)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(5)
            client.connect(str(qmp_path))
            stream = client.makefile("rwb", buffering=0)
            stream.readline()  # greeting
            stream.write(b'{"execute":"qmp_capabilities"}\r\n')
            stream.readline()
            stream.write(b'{"execute":"quit"}\r\n')
            stream.readline()
        rc = self.proc.wait(timeout=20)
        if rc != 0:
            raise RuntimeError(f"QEMU clean quit returned status {rc}")


def boot_run(iso, disk, temp, command, markers, boot_number):
    qmp = temp / f"qmp-{boot_number}.sock"
    vm = QemuShell(iso, disk, qmp)
    try:
        vm.expect(b"SDFS: system already installed.\n")
        vm.expect(b"liwus> ")
        vm.send(command.encode() + b"\n")
        for marker in markers:
            vm.expect(marker)
        vm.expect(b"liwus> ")
        vm.quit(qmp)
        result = bytes(vm.log)
        LOG_DIR.mkdir(exist_ok=True)
        (LOG_DIR / f"persistent-boot-{boot_number}.serial").write_bytes(result)
        return result
    except Exception:
        if vm.proc.poll() is None:
            vm.proc.terminate()
            try:
                vm.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                vm.proc.kill()
                vm.proc.wait()
        raise


def main():
    with tempfile.TemporaryDirectory(prefix="liwus-persistent-install-") as raw:
        temp = Path(raw)
        prefix = temp / "sdk"
        run(["make", "sdk-install", f"SDK_PREFIX={prefix}"])
        app_a = temp / "persistent-demo.lxe"
        variant_b = temp / "variant-b"
        variant_b.mkdir()
        app_b = variant_b / "persistent-demo.lxe"
        compile_app(prefix, app_a, "A", 17, 64)
        compile_app(prefix, app_b, "B", 19, 12000)
        disk = temp / "persistent-disk.img"
        with disk.open("wb") as image:
            image.truncate(64 * 1024 * 1024)

        # Use the already-built normal ISO. The demo is deliberately not in its initrd.
        iso = ROOT / "liwusos.iso"
        if not iso.is_file():
            run(["make", "all"])
        vm = QemuShell(iso, disk, temp / "qmp-open.sock")
        try:
            vm.expect(b"liwus> ")
            if b"First boot: copying initrd files to SDFS..." not in vm.log:
                raise RuntimeError("first boot did not take the initrd installation path")
            busy = subprocess.run([
                "python3", str(INSTALLER), "--app", str(app_a), "--disk", str(disk),
            ], text=True, capture_output=True)
            if busy.returncode == 0 or "open by PID" not in busy.stderr:
                raise RuntimeError("installer did not refuse an image open by QEMU")
            vm.send(b"persistent-demo.lxe\n")
            vm.expect(b"liwsh: command not found: persistent-demo.lxe\n")
            vm.expect(b"[liwsh] exit status 127\n")
            vm.expect(b"liwus> ")
            vm.send(b"native-hello.lxe\n")
            vm.expect(b"Hello from LIWUS OS SDK v0.1!\n")
            vm.expect(b"[liwsh] exit status 0\n")
            vm.expect(b"liwus> ")
            vm.quit(temp / "qmp-open.sock")
            LOG_DIR.mkdir(exist_ok=True)
            (LOG_DIR / "persistent-boot-1.serial").write_bytes(bytes(vm.log))
        except Exception:
            if vm.proc.poll() is None:
                vm.proc.terminate()
                vm.proc.wait(timeout=5)
            raise

        run(["make", "app-install", f"APP={app_a}", f"DISK={disk}"])
        log_a = boot_run(iso, disk, temp, "persistent-demo.lxe", [
            "LIWUS persistent demo — version A\n".encode(), b"[liwsh] exit status 17\n"], 2)
        if b"First boot: copying initrd files to SDFS..." in log_a:
            raise RuntimeError("second boot unexpectedly ran first-boot initrd installation")

        run(["make", "app-install", f"APP={app_b}", f"DISK={disk}", "REPLACE=1"])
        for number in (3, 4):
            log_b = boot_run(iso, disk, temp, "persistent-demo.lxe", [
                "LIWUS persistent demo — version B\n".encode(), b"[liwsh] exit status 19\n"], number)
            if b"First boot: copying initrd files to SDFS..." in log_b:
                raise RuntimeError("updated image unexpectedly reran initrd installation")
        print("PERSISTENT APP INSTALL PASS: same SDFS image across four boots; A->B update persisted")
        print("PERSISTENT APP SAFETY PASS: installer refused image while QEMU held it open")
        (LOG_DIR / "persistent-install.summary").write_text(
            "PERSISTENT APP INSTALL PASS: same SDFS image across four boots; A->B update persisted\n"
            "PERSISTENT APP SAFETY PASS: installer refused image while QEMU held it open\n"
            "Boot 2: version A, status 17. Boots 3 and 4: larger version B, status 19.\n"
        )


if __name__ == "__main__":
    main()
