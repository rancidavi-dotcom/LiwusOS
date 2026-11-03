#!/usr/bin/env python3
"""Boot LIWUS under QEMU and inject keyboard/mouse events through HMP."""

import argparse
import pathlib
import socket
import subprocess
import sys
import tempfile
import time


def wait_for_log(log_path: pathlib.Path, marker: str, process: subprocess.Popen,
                 timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        current = log_path.read_text(errors="replace") if log_path.exists() else ""
        if marker in current:
            return current
        if process.poll() is not None:
            break
        time.sleep(0.1)
    raise RuntimeError(f"QEMU did not reach {marker!r}")


def send_hmp(sock: socket.socket, command: str) -> None:
    sock.sendall(command.encode("ascii") + b"\n")
    time.sleep(0.2)
    try:
        sock.recv(4096)
    except TimeoutError:
        pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", required=True)
    parser.add_argument("--disk", required=True, type=pathlib.Path)
    parser.add_argument("--iso", required=True, type=pathlib.Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="liwus-usb-test-") as temp:
        root = pathlib.Path(temp)
        log_path = root / "serial.log"
        monitor_path = root / "monitor.sock"
        qemu_command = [
            args.qemu,
            "-machine", "q35,i8042=off", "-m", "2048", "-no-reboot",
            "-drive", f"file={args.disk},format=raw,if=none,id=disk",
            "-device", "ahci,id=ahci",
            "-device", "ide-hd,drive=disk,bus=ahci.0",
            "-snapshot",
            "-device", "qemu-xhci,id=xhci",
            "-device", "usb-kbd,bus=xhci.0",
            "-device", "usb-mouse,bus=xhci.0",
            "-cdrom", str(args.iso), "-display", "none",
            "-monitor", f"unix:{monitor_path},server=on,wait=off",
            "-serial", f"file:{log_path}",
        ]
        process = subprocess.Popen(qemu_command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        monitor = None
        failure = None
        try:
            wait_for_log(log_path, "configured-hid=", process, 20)
            wait_for_log(log_path, "liwus> ", process, 5)
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                try:
                    monitor = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    monitor.settimeout(0.5)
                    monitor.connect(str(monitor_path))
                    break
                except OSError:
                    if monitor:
                        monitor.close()
                    monitor = None
                    time.sleep(0.1)
            if not monitor:
                raise RuntimeError("could not connect to QEMU HMP monitor")
            try:
                monitor.recv(4096)  # HMP greeting and prompt.
            except TimeoutError:
                pass
            send_hmp(monitor, "sendkey a 250")
            wait_for_log(log_path, "USB: keyboard input event observed", process, 4)
            wait_for_log(log_path, "USB: keyboard input event observed\na", process, 4)
            send_hmp(monitor, "mouse_move 40 25 0")
            send_hmp(monitor, "mouse_button 1")
            send_hmp(monitor, "mouse_button 0")
            wait_for_log(log_path, "USB: mouse input event observed", process, 6)
        except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
            failure = str(error)
        finally:
            if monitor:
                monitor.close()
            if process.poll() is None:
                process.terminate()
            try:
                qemu_exit = process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                qemu_exit = process.wait()

        serial_log = log_path.read_text(errors="replace") if log_path.exists() else ""
        if failure:
            print(f"USB INPUT TEST FAIL: {failure}", file=sys.stderr)
            print("--- serial evidence ---", file=sys.stderr)
            print("\n".join(serial_log.splitlines()[-60:]), file=sys.stderr)
            if process.stderr:
                stderr = process.stderr.read().decode(errors="replace")
                if stderr:
                    print(stderr, file=sys.stderr)
            return 1

        from validate_usb_probe import validate_log

        return validate_log(serial_log, qemu_exit)


if __name__ == "__main__":
    raise SystemExit(main())
