#!/usr/bin/env python3
"""Validate xHCI startup and basic device-slot discovery under QEMU."""

import argparse
from pathlib import Path
import re
import sys


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

    errors = []
    if args.qemu_exit != 124:
        errors.append(f"expected QEMU timeout after boot (124), got {args.qemu_exit}")
    if "USB: xHCI capability probe OK" not in log:
        errors.append("xHCI capability probe did not succeed")
    connected = len(re.findall(r"USB: xHCI root port connected:", log))
    if connected < 2:
        errors.append(f"expected at least 2 connected HID ports, found {connected}")
    if "USB: xHCI controller running; command-ring NOOP PASS" not in log:
        errors.append("xHCI command/event rings did not complete a NOOP command")
    device_match = re.search(r"addressed-devices=0x([0-9a-fA-F]+)", log)
    if not device_match or int(device_match.group(1), 16) < 2:
        errors.append("xHCI did not address both attached USB devices")
    if "LIWUS_BOOT_READY" not in log:
        errors.append("kernel did not reach LIWUS_BOOT_READY")
    if "KERNEL PANIC" in log:
        errors.append("kernel panicked")

    if errors:
        print("USB PROBE TEST FAIL")
        for error in errors:
            print(f" - {error}")
        print(f"serial log: {args.log}")
        return 1

    print(f"USB PROBE TEST PASS: xHCI started, completed a NOOP, and addressed "
          f"two devices; {connected} connected ports detected, configuration "
          "and HID input pending")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
