#!/usr/bin/env python3
"""
Driver de teste interativo para LiwusOS TCC.
Roda QEMU via subprocess, detecta prompt por substring (sem readline),
envia comandos char-a-char com pacing, e valida o ciclo tcc -> run.
"""

import os
import sys
import time
import subprocess
import select
import signal

QEMU_CMD = [
    "qemu-system-x86_64",
    "-cdrom", "liwusos.iso",
    "-hda", "preinstalled_disk.img",
    "-m", "64M",
    "-display", "none",
    "-serial", "stdio",
    "-no-reboot",
    "-no-shutdown",
]

# Se KVM funcionar (raro no WSL sem config), descomente:
# QEMU_CMD.insert(-1, "-enable-kvm")

PROMPT = "root@liwusos# "
TIMEOUT_BOOT = 60
TIMEOUT_TCC = 300
TIMEOUT_RUN = 180
INTER_RETRY = 15
CHAR_DELAY = 0.05

def log(msg):
    print(f"[driver] {msg}", flush=True)

def wait_for_prompt(proc, timeout):
    """Lê stdout byte a byte até achar PROMPT."""
    buf = b""
    start = time.time()
    while time.time() - start < timeout:
        r, _, _ = select.select([proc.stdout], [], [], 0.1)
        if r:
            chunk = os.read(proc.stdout.fileno(), 4096)
            if not chunk:
                break
            buf += chunk
            sys.stdout.buffer.write(chunk)
            sys.stdout.buffer.flush()
            if PROMPT.encode() in buf:
                return True, buf
    return False, buf

def send_cmd(proc, cmd):
    """Envia comando char-a-char com delay, depois \n."""
    log(f"Enviando: {cmd}")
    for ch in cmd:
        proc.stdin.write(ch.encode())
        proc.stdin.flush()
        time.sleep(CHAR_DELAY)
    proc.stdin.write(b"\n")
    proc.stdin.flush()
    time.sleep(0.5)

def main():
    log("Iniciando QEMU...")
    proc = subprocess.Popen(
        QEMU_CMD,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        cwd=os.path.dirname(os.path.abspath(__file__)) + "/..",
    )

    try:
        # 1. Boot até prompt
        log(f"Esperando prompt ({TIMEOUT_BOOT}s)...")
        ok, boot_log = wait_for_prompt(proc, TIMEOUT_BOOT)
        if not ok:
            log("TIMEOUT: prompt não apareceu")
            # dump stderr
            err = proc.stderr.read()
            if err:
                log(f"stderr: {err.decode(errors='ignore')}")
            return 1
        log("Prompt detectado!")

        # 2. tcc hello_tcc.c -o hello
        send_cmd(proc, "tcc hello_tcc.c -o hello")

        # 3. Poll run hello até sucesso
        log(f"Polling 'run hello' a cada {INTER_RETRY}s (timeout {TIMEOUT_RUN}s)...")
        run_start = time.time()
        full_log = boot_log
        while time.time() - run_start < TIMEOUT_RUN:
            # envia run hello
            send_cmd(proc, "run hello")

            # coleta output por alguns segundos
            collect_end = time.time() + INTER_RETRY
            while time.time() < collect_end:
                r, _, _ = select.select([proc.stdout], [], [], 0.2)
                if r:
                    chunk = os.read(proc.stdout.fileno(), 4096)
                    if not chunk:
                        break
                    full_log += chunk
                    sys.stdout.buffer.write(chunk)
                    sys.stdout.buffer.flush()
                    # verifica sucesso
                    if b"pid=" in chunk or b"Hello World!" in chunk:
                        log("Detectado launch/resultado!")
                        # continua lendo até ver Hello World ou terminar
                        pass

            # checa se chegou ao sucesso
            if b"Hello World!" in full_log and b"argv[0]" in full_log:
                log(">>> PASS: Hello World! + argv[0] detectados")
                break

        # salva log completo
        log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "interactive_test_log.txt")
        with open(log_path, "wb") as f:
            f.write(full_log)
        log(f"Log salvo em {log_path}")

        # análise final
        if b"Hello World!" in full_log and b"argv[0]" in full_log:
            if b"rip=" in full_log or b"Exception" in full_log or b"GPF" in full_log:
                log(">>> FAIL: Exception dump detectado (pid=2 crashou)")
                return 2
            log(">>> SUCESSO COMPLETO: tcc + link + run funcionaram, sem exception")
            return 0
        else:
            log(">>> FAIL: Hello World! não apareceu no log")
            return 3

    except KeyboardInterrupt:
        log("Interrompido pelo usuário")
        return 130
    finally:
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except:
            proc.kill()

if __name__ == "__main__":
    sys.exit(main())