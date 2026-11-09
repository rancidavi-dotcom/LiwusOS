#!/usr/bin/env python3
"""Boot LIWUS with a QEMU EHCI controller and verify safe host startup."""

import argparse
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time


def wait_for_log(path: pathlib.Path, marker: str,
                 process: subprocess.Popen, timeout: float) -> str:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        log = path.read_text(errors="replace") if path.exists() else ""
        if marker in log:
            return log
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

    with tempfile.TemporaryDirectory(prefix="liwus-ehci-test-") as temp:
        root = pathlib.Path(temp)
        log_path = root / "serial.log"
        monitor_path = root / "monitor.sock"
        command = [
            args.qemu,
            "-machine", "q35,i8042=off", "-m", "2048", "-no-reboot",
            "-drive", f"file={args.disk},format=raw,if=none,id=disk",
            "-device", "ahci,id=ahci",
            "-device", "ide-hd,drive=disk,bus=ahci.0",
            "-snapshot",
            "-device", "ich9-usb-ehci1,id=ehci1,addr=1a.0",
            "-device", "ich9-usb-ehci2,id=ehci2,addr=1d.0",
            "-device", "usb-kbd,bus=ehci1.0,port=1",
            "-cdrom", str(args.iso), "-display", "none",
            "-monitor", f"unix:{monitor_path},server=on,wait=off",
            "-serial", f"file:{log_path}",
        ]
        process = subprocess.Popen(command, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE)
        failure = None
        monitor = None
        try:
            wait_for_log(log_path,
                         "USB EHCI: asynchronous DMA schedule active; enumeration pending",
                         process, 20)
            wait_for_log(log_path,
                         "USB EHCI: periodic keyboard schedule active",
                         process, 20)
            wait_for_log(log_path, "LIWUS_BOOT_READY", process, 15)
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
                monitor.recv(4096)
            except TimeoutError:
                pass
            send_hmp(monitor, "sendkey a 250")
            wait_for_log(log_path,
                         "USB EHCI: keyboard input event observed",
                         process, 5)
        except (OSError, RuntimeError) as error:
            failure = str(error)
        finally:
            if monitor:
                monitor.close()
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

        log = log_path.read_text(errors="replace") if log_path.exists() else ""
        errors = []
        if failure:
            errors.append(failure)
        if "USB EHCI: asynchronous DMA schedule active; enumeration pending" not in log:
            errors.append("EHCI asynchronous schedule did not become active")
        if "USB EHCI: configured boot keyboard address" not in log:
            errors.append("EHCI did not configure the attached boot keyboard")
        if "USB EHCI: periodic keyboard schedule active" not in log:
            errors.append("EHCI periodic keyboard schedule did not become active")
        state = re.search(
            r"\[ehci\] [0-9a-f]{2}:[0-9a-f]{2}\.[0-7] "
            r"CMD=([0-9a-f]{8}) STS=([0-9a-f]{8}) CF=([0-9a-f]{8}) "
            r"ASYNC=([0-9a-f]{8}) PERIODIC=([0-9a-f]{8}) "
            r"INTR=([0-9a-f]{8})", log)
        if not state:
            errors.append("post-start EHCI register state was not logged")
        else:
            command, status, config, async_base, periodic_base, intr = (
                int(value, 16) for value in state.groups())
            if not command & 1:
                errors.append("EHCI USBCMD.RUN is clear after startup")
            if status & (1 << 12):
                errors.append("EHCI USBSTS.HCHalted remains set after startup")
            if not command & (1 << 5):
                errors.append("EHCI USBCMD.Asynchronous Schedule Enable is clear")
            if not status & (1 << 15):
                errors.append("EHCI USBSTS.Asynchronous Schedule Status is clear")
            if not config & 1:
                errors.append("EHCI CONFIGFLAG.CF is clear after startup")
            if not async_base or async_base & 0x1f:
                errors.append("EHCI ASYNCLISTADDR is zero or misaligned")
            if periodic_base:
                errors.append("EHCI periodic list started unexpectedly")
            if intr:
                errors.append("EHCI USBINTR changed before interrupt support")
        if "LIWUS_BOOT_READY" not in log:
            errors.append("kernel did not reach the shell")
        if "KERNEL PANIC" in log:
            errors.append("kernel panicked")

        if errors:
            print("USB EHCI START TEST FAIL", file=sys.stderr)
            for error in errors:
                print(f" - {error}", file=sys.stderr)
            print("--- serial evidence ---", file=sys.stderr)
            print("\n".join(log.splitlines()[-120:]), file=sys.stderr)
            if process.stderr:
                stderr = process.stderr.read().decode(errors="replace")
                if stderr:
                    print(stderr, file=sys.stderr)
            return 1

        print("USB EHCI INPUT TEST PASS: endpoint-zero transfers, boot "
              "keyboard configuration and translated HID input verified")
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
