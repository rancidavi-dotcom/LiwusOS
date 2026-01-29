#!/bin/bash
# ============================================================
# LiwusOS - Teste de boot UEFI em QEMU + OVMF (headless)
#
# Sobe o ISO em firmware UEFI (OVMF), captura o serial e verifica:
#   - ausencia de excecoes de CPU / panic
#   - presenca de LIWUS_BOOT_READY
#
# Uso: bash scripts/uefi_boot_check.sh [iso]
# ============================================================
set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ISO="${1:-$ROOT_DIR/liwusos-uefi.iso}"
LOG="${UEFI_LOG:-/tmp/uefi_serial.log}"
VARS="${UEFI_VARS:-/tmp/OVMF_VARS_test.fd}"
DISK="${UEFI_DISK:-/tmp/uefi_disk.img}"
TIMEOUT="${UEFI_TIMEOUT:-120}"

OVMF_CODE="/usr/share/OVMF/OVMF_CODE_4M.fd"
OVMF_VARS="/usr/share/OVMF/OVMF_VARS_4M.fd"

if [ ! -f "$ISO" ]; then
    echo "[uefi] ISO nao encontrado: $ISO" >&2
    exit 2
fi
if [ ! -f "$OVMF_CODE" ]; then
    echo "[uefi] OVMF nao encontrado em $OVMF_CODE (instale o pacote ovmf)" >&2
    exit 2
fi

: > "$LOG"
cp "$OVMF_VARS" "$VARS"
[ -f "$DISK" ] || dd if=/dev/zero of="$DISK" bs=1M count=64 2>/dev/null

qemu-system-x86_64 \
    -machine q35 \
    -drive if=pflash,format=raw,readonly=on,file="$OVMF_CODE" \
    -drive if=pflash,format=raw,file="$VARS" \
    -cdrom "$ISO" \
    -drive id=disk,file="$DISK",if=none,format=raw \
    -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
    -m 512 \
    -display none -monitor none -no-reboot -accel tcg \
    -netdev user,id=net0 -device rtl8139,netdev=net0 \
    -serial "file:$LOG" &
QPID=$!

READY=0
ELAPSED=0
while [ $ELAPSED -lt $TIMEOUT ]; do
    sleep 2
    ELAPSED=$((ELAPSED + 2))
    if grep -aq "LIWUS_BOOT_READY" "$LOG" 2>/dev/null; then
        READY=1
        break
    fi
    if ! kill -0 $QPID 2>/dev/null; then
        echo "[uefi] QEMU encerrou em ${ELAPSED}s"
        break
    fi
done

sleep 2
kill $QPID 2>/dev/null
wait $QPID 2>/dev/null

FAULTS=$(grep -acE "CPU exception|Unhandled|kernel_panic" "$LOG" 2>/dev/null || true)
FAULTS=${FAULTS:-0}

echo "============================================"
echo "  UEFI boot check"
echo "============================================"
echo "  ISO:       $ISO"
echo "  boot:      $([ $READY -eq 1 ] && echo SIM || echo NAO) (${ELAPSED}s)"
echo "  excecoes:  $FAULTS"

if [ "$READY" -eq 1 ] && [ "$FAULTS" -eq 0 ]; then
    echo "  RESULTADO: PASS"
    exit 0
fi

echo "  RESULTADO: FAIL"
echo "  --- marcos ---"
grep -aE "BOOT-STAGE|compatible framebuffer|LIWUS_BOOT_READY|CPU exception|Unhandled" "$LOG" | tail -15
echo "  --- ultimas linhas ---"
tail -10 "$LOG"
exit 1
