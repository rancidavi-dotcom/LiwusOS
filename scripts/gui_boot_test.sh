#!/bin/bash
# ============================================================
# LiwusOS GUI Boot / Regression Test
# Boots the OS in its normal (GUI) mode and confirms it reaches
# LIWUS_BOOT_READY without a kernel panic. Validates that the
# image-viewer app links/registers without boot-time regressions.
# Usage: bash scripts/gui_boot_test.sh
# ============================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SERIAL_LOG="$ROOT_DIR/gui_boot_serial.log"
TIMEOUT=120
cd "$ROOT_DIR"

echo "== LiwusOS GUI Boot Test =="

echo "[1/3] Building kernel..."
make -j$(nproc 2>/dev/null || echo 4) kernel.bin 2>&1 | tail -2
[ -f kernel.bin ] || { echo "build failed"; exit 1; }

echo "[2/3] Building ISO (normal, non-test initrd)..."
mkdir -p repo
rm -f repo/test_mode repo/test_img repo/test_tcc
rm -f liwusos.iso
make liwusos.iso 2>&1 | tail -3
[ -f liwusos.iso ] || { echo "iso failed"; exit 1; }

echo "[3/3] Booting GUI QEMU (timeout ${TIMEOUT}s)..."
rm -f "$SERIAL_LOG"
TEST_DISK="$ROOT_DIR/gui_boot_disk.img"
dd if=/dev/zero of="$TEST_DISK" bs=1M count=64 2>/dev/null
qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -drive id=disk,file="$TEST_DISK",if=none,format=raw \
    -device ahci,id=ahci \
    -device ide-hd,drive=disk,bus=ahci.0 \
    -m 512 \
    -display none -monitor none -no-reboot \
    -serial "file:$SERIAL_LOG" -accel tcg 2>/dev/null &
QP=$!
ELAPSED=0; RESULT=""
while [ $ELAPSED -lt $TIMEOUT ]; do
    sleep 1; ELAPSED=$((ELAPSED+1))
    if ! kill -0 $QP 2>/dev/null; then break; fi
    if [ -f "$SERIAL_LOG" ]; then
        grep -q "LIWUS_BOOT_READY" "$SERIAL_LOG" && { RESULT="OK"; break; }
        grep -q "KERNEL PANIC" "$SERIAL_LOG" 2>/dev/null && { RESULT="PANIC"; break; }
    fi
done
kill $QP 2>/dev/null || true; wait $QP 2>/dev/null || true

echo ""
echo "== Result =="
if [ "$RESULT" = "OK" ]; then
    echo "  [PASS] GUI boot reached LIWUS_BOOT_READY (no panic)"
    rm -f "$TEST_DISK"; exit 0
else
    echo "  [FAIL] result=$RESULT"
    tail -40 "$SERIAL_LOG" 2>/dev/null || echo "  (no serial)"
    rm -f "$TEST_DISK"; exit 1
fi
