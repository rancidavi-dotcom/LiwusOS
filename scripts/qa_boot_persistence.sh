#!/bin/bash
# Regressao mínima de integração: dois boots no mesmo disco novo.
# Nunca usa nem altera liwus_disk.img do desenvolvedor.
set -euo pipefail
cd "$(dirname "$0")/.."

ISO="${ISO_IMAGE:-liwusos.iso}"
TIMEOUT_SECS="${QA_BOOT_TIMEOUT:-25}"
QA_DISK="$(mktemp /tmp/liwus-qa-disk.XXXXXX)"
QA_LOG_ONE="$(mktemp /tmp/liwus-qa-boot1.XXXXXX)"
QA_LOG_TWO="$(mktemp /tmp/liwus-qa-boot2.XXXXXX)"

cleanup() {
    rm -f -- "$QA_DISK" "$QA_LOG_ONE" "$QA_LOG_TWO"
}
trap cleanup EXIT

[ -f "$ISO" ] || { echo "[FAIL] ISO ausente: $ISO" >&2; exit 1; }
command -v qemu-system-x86_64 >/dev/null || {
    echo "[FAIL] qemu-system-x86_64 ausente" >&2; exit 1;
}

# O initrd atual contém assets grandes; 128 MiB deixa margem para a instalação.
truncate -s 128M "$QA_DISK"

boot_once() {
    local log="$1"
    qemu-system-x86_64 \
        -cdrom "$ISO" \
        -drive id=disk,file="$QA_DISK",if=none,format=raw \
        -device ahci,id=ahci \
        -device ide-hd,drive=disk,bus=ahci.0 \
        -m 512 -display none -monitor none -no-reboot \
        -serial "file:$log" \
        -audiodev none,id=aud0 -device AC97,audiodev=aud0 \
        >/dev/null 2>&1 &
    local qpid=$!
    local elapsed=0
    while [ "$elapsed" -lt "$TIMEOUT_SECS" ]; do
        if grep -q "LIWUS_BOOT_READY" "$log" 2>/dev/null; then
            kill "$qpid" 2>/dev/null || true
            wait "$qpid" 2>/dev/null || true
            return 0
        fi
        if ! kill -0 "$qpid" 2>/dev/null; then
            wait "$qpid" 2>/dev/null || true
            break
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    kill "$qpid" 2>/dev/null || true
    wait "$qpid" 2>/dev/null || true
    return 1
}

echo "[QA] boot 1: instalação em disco temporário"
boot_once "$QA_LOG_ONE" || { cat "$QA_LOG_ONE"; echo "[FAIL] boot 1"; exit 1; }
grep -q "First boot: copying system files" "$QA_LOG_ONE" || {
    cat "$QA_LOG_ONE"; echo "[FAIL] instalação inicial não ocorreu"; exit 1;
}

echo "[QA] boot 2: verificação de persistência"
boot_once "$QA_LOG_TWO" || { cat "$QA_LOG_TWO"; echo "[FAIL] boot 2"; exit 1; }
grep -q "SDFS: system already installed" "$QA_LOG_TWO" || {
    cat "$QA_LOG_TWO"; echo "[FAIL] disco não persistiu entre boots"; exit 1;
}

echo "[PASS] boot e persistência SDFS validados"
