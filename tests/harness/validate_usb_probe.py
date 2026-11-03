#!/usr/bin/env python3
"""Validate xHCI startup and basic device-slot discovery under QEMU."""

import argparse
from pathlib import Path
import re
import sys


def validate_log(log: str, qemu_exit: int) -> int:
    errors = []
    if qemu_exit not in (0, 124, -15):
        errors.append(f"expected QEMU normal exit/termination (0, 124, -15), got {qemu_exit}")
    if "USB: xHCI capability probe OK" not in log:
        errors.append("xHCI capability probe did not succeed")
    connected = len(re.findall(r"USB: xHCI root port connected:", log))
    if connected < 2:
        errors.append(f"expected at least 2 connected HID ports, found {connected}")
    if "USB: xHCI controller running; command-ring NOOP PASS" not in log:
        errors.append("xHCI command/event rings did not complete a NOOP command")
    device_match = re.search(r"configured-hid=0x([0-9a-fA-F]+)", log)
    if not device_match or int(device_match.group(1), 16) < 2:
        errors.append("xHCI did not configure both attached boot HID devices")
    report_slots = len(re.findall(r"USB: HID interrupt report received, slot", log))
    if report_slots < 2:
        errors.append(f"expected reports from both USB HID devices, got {report_slots}")
    if "USB: keyboard input event observed" not in log:
        errors.append("USB keyboard input did not reach LIWUS")
    if "USB: mouse input event observed" not in log:
        errors.append("USB mouse input did not reach LIWUS")
    if "USB: keyboard input event observed\na" not in log:
        errors.append("injected USB key was not echoed by the native shell")
    if "LIWUS_BOOT_READY" not in log:
        errors.append("kernel did not reach LIWUS_BOOT_READY")
    if "KERNEL PANIC" in log:
        errors.append("kernel panicked")

    if errors:
        print("USB INPUT TEST FAIL")
        for error in errors:
            print(f" - {error}")
        return 1

    print(f"USB INPUT TEST PASS: xHCI configured both HID devices; keyboard and "
          f"mouse input reached LIWUS; {connected} connected ports detected")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", required=True, type=Path)
    parser.add_argument("--qemu-exit", required=True, type=int)
    args = parser.parse_args()

    try:
        log = args.log.read_text(errors="replace")
    except OSError as error:
        print(f"USB PROBE TEST FAIL: cannot read serial log: {error}",
              file=sys.stderr)
        return 2

    result = validate_log(log, args.qemu_exit)
    if result:
        print(f"serial log: {args.log}")
    return result


if __name__ == "__main__":
    raise SystemExit(main())
