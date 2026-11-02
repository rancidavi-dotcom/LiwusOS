#!/usr/bin/env python3
"""Strictly validate one LiwusOS QEMU test-runner serial log."""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[2]


def read_manifest(name: str) -> list[str]:
    path = ROOT / "tests" / "harness" / name
    return [line.strip() for line in path.read_text().splitlines()
            if line.strip() and not line.lstrip().startswith("#")]


KERNEL_CASES = read_manifest("kernel_cases.txt")
RING3_CASES = read_manifest("ring3_cases.txt")
RING3_RESULTS = [
    "ET_DYN PIE executed at USER_PIE_BASE",
    "Linux x86-64 SYSCALL getpid/fstat/openat/mmap/munmap/FS-TLS/write PASS",
    "Linux clone, private futex WAIT/WAKE, shared-mm, independent FS TLS, thread exit PASS",
    "native LIWUS .lxe ELF via crt0+SDK wrappers executed",
    "external SDK application output and exit 23 PASS",
    "kernel observed clean user exit",
]


def _occurrences(log: str, marker: str) -> int:
    return sum(line == marker for line in log.splitlines())


def validate(log: str, mode: str, qemu_exit: int) -> list[str]:
    errors: list[str] = []
    if mode not in {"test", "ring3", "smap"}:
        return [f"unknown mode: {mode}"]
    if qemu_exit != 1:
        errors.append(f"expected isa-debug-exit PASS status 1, got {qemu_exit}")

    for marker in ("=== TEST RUNNER START ===", "=== TEST RUNNER END ===",
                   "RESULT: ALL PASS", "[TEST HARNESS] QEMU debug-exit PASS"):
        count = _occurrences(log, marker)
        if count != 1:
            errors.append(f"expected exactly one {marker!r}, found {count}")
    if "KERNEL PANIC" in log or "[runner] FATAL:" in log:
        errors.append("kernel panic or fatal runner setup error in serial log")
    failure_patterns = (r"\bASSERT FAIL:", r"\bFAIL:", r"\[RING3 FAIL\]",
                        r"^RESULT: FAIL$")
    for pattern in failure_patterns:
        if re.search(pattern, log, re.MULTILINE):
            errors.append(f"failure marker matched {pattern!r}")
    summaries = re.findall(r"^RESULT: (\d+)/(\d+) PASS$", log, re.MULTILINE)
    expected_summary = (str(len(KERNEL_CASES)), str(len(KERNEL_CASES)))
    if summaries != [expected_summary]:
        errors.append(f"expected one RESULT: {expected_summary[0]}/{expected_summary[1]} PASS, got {summaries}")

    actual_cases = re.findall(r"^\[TEST\] ([A-Za-z0-9_]+)$", log, re.MULTILINE)
    if actual_cases != KERNEL_CASES:
        missing = [name for name in KERNEL_CASES if name not in actual_cases]
        extra = [name for name in actual_cases if name not in KERNEL_CASES]
        duplicates = sorted({name for name in actual_cases if actual_cases.count(name) > 1})
        errors.append(f"kernel case sequence mismatch; missing={missing}, extra={extra}, duplicates={duplicates}")

    # Require one top-level PASS after each case begins. This catches a runner
    # that counts a test without receiving its success return/marker.
    current = None
    per_case_pass: dict[str, int] = {}
    for line in log.splitlines():
        match = re.match(r"^\[TEST\] ([A-Za-z0-9_]+)$", line)
        if match:
            current = match.group(1)
        elif current and line.startswith("  PASS: "):
            per_case_pass[current] = per_case_pass.get(current, 0) + 1
    for name in KERNEL_CASES:
        if per_case_pass.get(name, 0) != 1:
            errors.append(f"{name}: expected exactly one top-level PASS, got {per_case_pass.get(name, 0)}")

    for case in RING3_CASES:
        marker = f"[RING3 PASS] {case}"
        # Kernel and ring-3 tasks share the serial port; an unrelated kernel
        # print can prefix the first bytes of a user marker on the same line.
        count = log.count(marker)
        if count != 1:
            errors.append(f"expected exactly one {marker!r}, found {count}")
    for result in RING3_RESULTS:
        marker = f"[RING3 RESULT] {result}"
        count = log.count(marker)
        if count != 1:
            errors.append(f"expected exactly one {marker!r}, found {count}")
    for marker in ("[EXTERNAL SDK] running; pid=",
                   "External LIWUS application running."):
        count = log.count(marker)
        if count != 1:
            errors.append(f"expected external application output {marker!r} once, found {count}")
    for marker in ("[RING3 RESULT] ALL PASS", "[RING3 RESULT] ET_DYN PIE executed at USER_PIE_BASE"):
        count = log.count(marker)
        if count != 1:
            errors.append(f"expected exactly one {marker!r}, found {count}")

    if mode == "smap":
        if _occurrences(log, "VMM: CR4.SMAP enabled") < 1:
            errors.append("SMAP was not confirmed enabled")
        if _occurrences(log, "APIC: AP core (APIC ID 3) booted and initialized successfully!") != 1:
            errors.append("four-CPU boot completion marker missing or duplicated")
    return errors


def _synthetic_log(mode: str) -> str:
    lines = ["=== TEST RUNNER START ==="]
    for name in KERNEL_CASES:
        lines.extend((f"[TEST] {name}", f"  PASS: {name}"))
    lines.extend(f"[RING3 PASS] {case}" for case in RING3_CASES)
    lines.extend(("[EXTERNAL SDK] running; pid=123",
                  "External LIWUS application running."))
    lines.extend(f"[RING3 RESULT] {result}" for result in RING3_RESULTS)
    lines.extend(("[RING3 RESULT] ALL PASS",
                  f"RESULT: {len(KERNEL_CASES)}/{len(KERNEL_CASES)} PASS",
                  "RESULT: ALL PASS", "=== TEST RUNNER END ===",
                  "[TEST HARNESS] QEMU debug-exit PASS"))
    if mode == "smap":
        lines.extend(("VMM: CR4.SMAP enabled",
                      "APIC: AP core (APIC ID 3) booted and initialized successfully!"))
    return "\n".join(lines) + "\n"


def self_test() -> list[str]:
    errors: list[str] = []
    valid = _synthetic_log("test")
    if validate(valid, "test", 1):
        errors.append("valid synthetic log was rejected")
    mutations = [
        (valid.replace("[TEST] terminal_files\n  PASS: terminal_files\n", "", 1), "missing test"),
        (valid.replace("  PASS: terminal_files\n", "", 1), "test without PASS"),
        (valid + "[TEST] terminal_files\n  PASS: terminal_files\n", "duplicate test"),
        (valid + "KERNEL PANIC: intentional harness fixture\n", "panic"),
        (valid + "  FAIL: intentional harness fixture\n", "failure marker"),
        (valid.replace("[RING3 PASS] NX blocks execution from writable anonymous memory\n", "", 1), "missing ring3 case"),
    ]
    for mutated, label in mutations:
        if not validate(mutated, "test", 1):
            errors.append(f"validator missed {label}")
    if not validate(valid, "test", 0):
        errors.append("validator accepted an incorrect QEMU exit code")
    if validate(_synthetic_log("smap"), "smap", 1):
        errors.append("valid SMAP synthetic log was rejected")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path)
    parser.add_argument("--mode", choices=("test", "ring3", "smap"), default="test")
    parser.add_argument("--qemu-exit", type=int, default=1)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        errors = self_test()
        if errors:
            print("HARNESS SELF-TEST FAIL")
            for error in errors:
                print(f" - {error}")
            return 1
        print("HARNESS SELF-TEST PASS: missing/duplicate/fail/panic/exit-code detection")
        return 0
    if not args.log:
        parser.error("--log is required unless --self-test is selected")
    try:
        log = args.log.read_text(errors="replace")
    except OSError as error:
        print(f"could not read serial log {args.log}: {error}", file=sys.stderr)
        return 2
    errors = validate(log, args.mode, args.qemu_exit)
    if errors:
        print(f"SERIAL VALIDATION FAIL ({args.mode})")
        for error in errors:
            print(f" - {error}")
        print(f"reproduce: make {args.mode if args.mode != 'test' else 'test'}")
        return 1
    print(f"SERIAL VALIDATION PASS ({args.mode}): {len(KERNEL_CASES)} unique kernel cases, {len(RING3_CASES)} ring-3 cases, QEMU exit 1")
    print(f"log: {args.log}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
