#!/usr/bin/env python3
"""Exercise the real userspace shell through QEMU's serial console."""

from pathlib import Path
import os
import select
import socket
import shutil
import subprocess
import sys
import threading
import tempfile
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


def send_hmp(sock: socket.socket, command: str) -> None:
    sock.sendall(command.encode("ascii") + b"\n")
    time.sleep(0.2)
    try:
        sock.recv(4096)
    except TimeoutError:
        pass


def send_usb_text(sock: socket.socket, value: str) -> None:
    key_names = {" ": "spc", "/": "slash", ".": "dot", "-": "minus",
                 "\n": "ret"}
    commands = []
    for char in value:
        key = key_names.get(char)
        if key is None:
            key = f"shift-{char.lower()}" if char.isupper() else char.lower()
        commands.append(f"sendkey {key} 40\n")
    sock.sendall("".join(commands).encode("ascii"))
    time.sleep(max(0.2, len(value) * 0.05))
    try:
        sock.recv(4096)
    except TimeoutError:
        pass


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
    kilo_usb_only = "--kilo-usb-save-only" in sys.argv

    page = (b"<!doctype html><html><head><title>Webtext JS test</title></head>"
            b"<body><p id=\"result\">waiting &amp; ready</p><script>"
            b"var result=document.getElementById('result');"
            b"if(result.textContent!=='waiting & ready')"
            b"throw new Error('source HTML entities were not decoded');"
            b"result.textContent='WEBTEXT_JS_NETWORK_OK';"
            b"if(result.textContent!=='WEBTEXT_JS_NETWORK_OK')"
            b"throw new Error('mutated textContent changed on readback');"
            b"</script></body></html>")

    class PageHandler(BaseHTTPRequestHandler):
        def do_GET(self) -> None:
            if self.path != "/page":
                self.send_error(404)
                return
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(page)))
            self.end_headers()
            self.wfile.write(page)

        def log_message(self, _format: str, *_args: object) -> None:
            pass

    http_server = ThreadingHTTPServer(("0.0.0.0", 0), PageHandler)
    http_thread = threading.Thread(target=http_server.serve_forever, daemon=True)
    http_thread.start()
    http_port = http_server.server_address[1]

    with tempfile.TemporaryDirectory(prefix="liwus-shell-qemu-") as temp:
        temp_path = Path(temp)
        image_tree = temp_path / "isodir"
        shutil.copytree(root / "isodir", image_tree)
        grub_cfg = image_tree / "boot/grub/grub.cfg"
        grub_text = grub_cfg.read_text()
        if '"LiwusOS x86_64 (Live USB / Modo Texto)"' not in grub_text:
            raise RuntimeError("text-mode GRUB entry is missing from the test ISO")
        grub_cfg.write_text(grub_text.replace("mode=live", "mode=live mode=text"))
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
            head_lines = b"".join(f"line-{number:02d}\n".encode()
                                  for number in range(1, 13))
            (files_path / "head-lines.txt").write_bytes(head_lines)
            (files_path / "kilo-existing-save.txt").write_bytes(b"Old data\n")
            (files_path / "kilo-root-save.txt").write_bytes(b"Protected\n")
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
            subprocess.run([
                "tar", "-rf", str(initrd), "--format=ustar",
                "-C", str(files_path), "head-lines.txt",
            ], check=True)
            subprocess.run([
                "tar", "-rf", str(initrd), "--format=ustar",
                "-C", str(files_path), "kilo-existing-save.txt",
            ], check=True)
            subprocess.run([
                "tar", "-rf", str(initrd), "--format=ustar",
                "--owner=0", "--group=0", "--numeric-owner",
                "-C", str(files_path), "kilo-root-save.txt",
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

        monitor_path = temp_path / "qemu-monitor.sock"
        machine = "pc-q35-11.1,i8042=off" if kilo_usb_only else "q35"
        command = [
            "qemu-system-x86_64", "-machine", machine, "-m", "2048",
            "-display", "none",
            "-monitor", f"unix:{monitor_path},server=on,wait=off" if kilo_usb_only else "none",
            "-serial",
            f"tcp:127.0.0.1:{serial_port},server=on,wait=off",
            "-no-reboot", "-drive", f"file={disk},format=raw,if=none,id=disk",
            "-device", "ahci,id=ahci", "-device", "ide-hd,drive=disk,bus=ahci.0",
            "-netdev", "user,id=net0", "-device", "e1000,netdev=net0",
            "-cdrom", str(iso),
        ]
        if kilo_usb_only:
            command.extend([
                "-device", "ich9-usb-ehci1,id=ehci1,addr=1a.0",
                "-device", "ich9-usb-ehci2,id=ehci2,addr=1d.0",
                "-device", "usb-kbd,bus=ehci1.0,port=1",
            ])
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
        monitor = None
        if kilo_usb_only:
            monitor_deadline = time.monotonic() + 5
            while monitor is None and time.monotonic() < monitor_deadline:
                try:
                    monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    monitor.settimeout(0.5)
                    monitor.connect(str(monitor_path))
                except OSError:
                    if monitor:
                        monitor.close()
                    monitor = None
                    time.sleep(0.05)
            if not monitor:
                process.terminate()
                process.wait(timeout=5)
                raise TimeoutError("QEMU HMP monitor did not become available")
            try:
                monitor.recv(4096)
            except TimeoutError:
                pass

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

            # Focused mode is useful when another integration check is stale;
            # the real Kilo -> SDFS read-back regression can still run alone.
            if "--kilo-save-only" in sys.argv or kilo_usb_only:
                if kilo_usb_only:
                    send_usb_text(monitor, "kilo kilo-save-test.txt\n")
                else:
                    send(b"kilo /kilo-save-test.txt\n")
                expect(b"^O/^S Salvar")
                if kilo_usb_only:
                    send_usb_text(monitor, "Kilo save regression")
                    send_hmp(monitor, "sendkey ctrl-s 100")
                else:
                    send(b"Kilo save regression")
                    send(b"\x13")  # Ctrl-S through serial.
                expect(b"salvo (21 bytes)")
                if kilo_usb_only:
                    send_hmp(monitor, "sendkey ctrl-q 100")
                else:
                    send(b"\x11")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")
                if kilo_usb_only:
                    send_usb_text(monitor, "cat kilo-save-test.txt\n")
                else:
                    send(b"cat /kilo-save-test.txt\n")
                expect(b"Kilo save regression\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")
                if kilo_usb_only:
                    send_usb_text(monitor, "kilo kilo-existing-save.txt\n")
                else:
                    send(b"kilo /kilo-existing-save.txt\n")
                expect(b"^O/^S Salvar")
                if kilo_usb_only:
                    send_usb_text(monitor, "New ")
                    send_hmp(monitor, "sendkey ctrl-s 100")
                else:
                    send(b"New ")
                    send(b"\x13")
                expect(b"salvo (13 bytes)")
                if kilo_usb_only:
                    send_hmp(monitor, "sendkey ctrl-q 100")
                else:
                    send(b"\x11")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")
                if kilo_usb_only:
                    send_usb_text(monitor, "cat kilo-existing-save.txt\n")
                else:
                    send(b"cat /kilo-existing-save.txt\n")
                expect(b"New Old data\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")
                if kilo_usb_only:
                    send_usb_text(monitor, "kilo kilo-root-save.txt\n")
                else:
                    send(b"kilo kilo-root-save.txt\n")
                expect(b"^O/^S Salvar")
                if kilo_usb_only:
                    send_usb_text(monitor, "x")
                    send_hmp(monitor, "sendkey ctrl-s 100")
                else:
                    send(b"x")
                    send(b"\x13")
                expect(b"Nao foi possivel abrir 'kilo-root-save.txt' (erro 13)")
                if kilo_usb_only:
                    send_hmp(monitor, "sendkey ctrl-q 100")
                else:
                    send(b"\x11")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")
                if kilo_usb_only:
                    send_usb_text(monitor, "cat kilo-root-save.txt\n")
                else:
                    send(b"cat kilo-root-save.txt\n")
                expect(b"Protected\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")
                transcript.extend(b"[KILO SAVE TEST] new/existing file write and read-back PASS\n")
                log_path.write_bytes(transcript)
                label = "USB-HID" if kilo_usb_only else "serial"
                print(f"KILO QEMU {label} SAVE INTEGRATION PASS: {log_path}")
                return 0

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
            expect(b"Built-ins: help, version, pwd, cd, ifconfig, ping, date, ntp, nslookup, wget, exit, shutdown\n")
            expect(b"liwus> ")

            # Wait for DHCP, then exercise HTTP download -> ring-3 QuickJS child
            # -> DOM serialization -> terminal text using a host-local fixture.
            dhcp_deadline = time.monotonic() + 15.0
            while b"inet addr: 10.0.2.15" not in transcript and time.monotonic() < dhcp_deadline:
                send(b"ifconfig\n")
                expect(b"liwus> ")
                if b"inet addr: 10.0.2.15" not in transcript:
                    time.sleep(0.5)
            if b"inet addr: 10.0.2.15" not in transcript:
                raise RuntimeError("QEMU guest did not obtain its DHCP address")
            web_url = f"http://10.0.2.2:{http_port}/page".encode()
            send(b"webtext --js " + web_url + b" /webtext-js-result.html\n")
            expect(b"WEBTEXT_JS_NETWORK_OK\n", timeout=60.0)
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")
            send(b"cat /webtext-js-result.html\n")
            expect(b"waiting &amp; ready")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            if "--webtext-js-only" in sys.argv:
                transcript.extend(
                    b"[WEBTEXT JS NETWORK TEST] DHCP, HTTP fetch, inline script, "
                    b"DOM serialization, raw-source save PASS\n"
                )
                log_path.write_bytes(transcript)
                print(f"WEBTEXT JS QEMU NETWORK INTEGRATION PASS: {log_path}")
                return 0

            send(b"pwd\n")
            expect(b"/\n")
            expect(b"liwus> ")
            send(b"cd /bin\n")
            expect(b"liwus> ")
            send(b"pwd\n")
            expect(b"/bin\n")
            expect(b"liwus> ")
            send(b"cd /\n")
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
                         b"cp.lxe", b"touch.lxe", b"echo.lxe", b"rmdir.lxe",
                         b"stat.lxe", b"head.lxe"):
                expect(name + b"\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            for command in (b"ls", b"ls.lxe", b"/bin/ls", b"/bin/ls.lxe"):
                send(command + b" /bin\n")
                expect(b"cat.lxe\n")
                expect(b"[liwsh] exit status 0\n")
                expect(b"liwus> ")

            send(b"stat /readme.txt\n")
            expect(b"Path: /readme.txt\n")
            expect(b"Resource: [TEXT ] Text Document\n")
            expect(b"Size: 21 bytes\n")
            expect(b"Mode: 644\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            send(b"head /head-lines.txt\n")
            expect(b"line-01\nline-02\nline-03\nline-04\nline-05\n"
                   b"line-06\nline-07\nline-08\nline-09\nline-10\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            send(b"mkdir /rmdir-empty\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")
            send(b"rmdir /rmdir-empty\n")
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

            # Test redirection and pipelines
            send(b"echo redir_teste > /work/redir.txt\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")
            send(b"cat /work/redir.txt\n")
            expect(b"redir_teste\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            send(b"echo linha_dois >> /work/redir.txt\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")
            send(b"cat /work/redir.txt\n")
            expect(b"redir_teste\nlinha_dois\n")
            expect(b"[liwsh] exit status 0\n")
            expect(b"liwus> ")

            send(b"cat /work/redir.txt | head\n")
            expect(b"redir_teste\nlinha_dois\n")
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
            if monitor:
                monitor.close()
            serial.close()
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            http_server.shutdown()
            http_server.server_close()
            http_thread.join(timeout=2.0)


if __name__ == "__main__":
    raise SystemExit(main())
