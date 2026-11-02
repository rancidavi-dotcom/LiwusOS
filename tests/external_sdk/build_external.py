#!/usr/bin/env python3
"""Build a LIWUS SDK consumer in a temporary tree outside the source checkout."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


SOURCE = r'''#include <liwus/liwus.h>

static volatile unsigned long writable_probe = 0x4c49575553414249UL;

int main(int argc, char **argv, char **envp) {
    if (!envp || envp[0] != 0 || argc != 2 ||
        liw_strcmp(argv[0], "external-test.lxe") != 0 ||
        liw_strcmp(argv[1], "demo") != 0) return 91;
    if (writable_probe != 0x4c49575553414249UL) return 95;
    char message[128] = "[EXTERNAL SDK] running; pid=";
    size_t used = liw_strlen(message);
    int pid = liw_getpid();
    if (pid <= 0) return 92;
    char digits[16];
    size_t count = 0;
    unsigned int value = (unsigned int)pid;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value && count < sizeof(digits));
    while (count && used + 2 < sizeof(message)) message[used++] = digits[--count];
    message[used++] = '\n';
    if (liw_write_all(1, message, used) < 0) return 93;
    static const char hello[] = "External LIWUS application running.\n";
    if (liw_write_all(1, hello, sizeof(hello) - 1) < 0) return 94;
    return 23;
}
'''


def run(command, cwd):
    subprocess.run(command, cwd=cwd, check=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    args.output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="liwus-sdk-validation-") as tmp:
        root = Path(tmp)
        prefix = root / "installed-sdk"
        project = root / "external-app"
        project.mkdir()
        run(["make", "sdk-install", f"SDK_PREFIX={prefix}"], repo)
        source = project / "main.c"
        source.write_text(SOURCE)
        run([
            "gcc", "-std=gnu99", "-O2", "-Wall", "-Wextra", "-Werror",
            "-ffreestanding", "-fno-builtin", "-fno-pie", "-fno-pic",
            "-fno-stack-protector", "-m64", "-mno-red-zone",
            f"-I{prefix / 'include'}", "-c", "main.c", "-o", "main.o",
        ], project)
        binary = project / "external-test.lxe"
        run([
            "ld", "-m", "elf_x86_64", "-nostdlib", "-static",
            "--build-id=none", "-T", str(prefix / "share/liwus/elf64.ld"),
            "-o", str(binary), str(prefix / "lib/crt0.o"), "main.o",
            str(prefix / "lib/libliwus.a"),
        ], project)

        header = subprocess.check_output(["readelf", "-h", str(binary)], text=True)
        program = subprocess.check_output(["readelf", "-l", str(binary)], text=True)
        if "ELF64" not in header or "Advanced Micro Devices X86-64" not in header:
            raise SystemExit("external artifact is not x86-64 ELF64")
        if "EXEC (Executable file)" not in header or "INTERP" in program:
            raise SystemExit("external artifact is not a static ET_EXEC image")
        if "Entry point address:               0x400000" not in header:
            raise SystemExit("external artifact entrypoint is not the SDK CRT entry")
        if program.count(" LOAD ") != 3 or "R E" not in program or \
                "RW " not in program:
            raise SystemExit("external ELF load segments do not match RX/R/RW layout")
        undefined = subprocess.check_output(["nm", "-u", str(binary)], text=True).strip()
        if undefined:
            raise SystemExit(f"external artifact has unresolved symbols:\n{undefined}")
        shutil.copy2(binary, args.output)
    print(f"EXTERNAL SDK BUILD PASS: {args.output}")


if __name__ == "__main__":
    main()
