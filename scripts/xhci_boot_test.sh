#!/bin/bash
# ============================================================
# LiwusOS xHCI (USB 3.0) Boot / Regression Test
# Boots the OS with a qemu-xhci controller and USB keyboard +
# mouse attached to it, then confirms the kernel enumerates both
# HID devices and reaches LIWUS_BOOT_READY without a kernel panic.
# Usage: bash scripts/xhci_boot_test.sh
# ============================================================
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SERIAL_LOG="$ROOT_DIR/xhci_boot_serial.log"
TIMEOUT=150
cd "$ROOT_DIR"

echo "== LiwusOS xHCI Boot Test =="

echo "[1/3] Building kernel..."
make -j$(nproc 2>/dev/null || echo 4) kernel.bin 2>&1 | tail -2
[ -f kernel.bin ] || { echo "build failed"; exit 1; }

echo "[2/3] Building ISO (normal, non-test initrd)..."
mkdir -p repo
rm -f repo/test_mode repo/test_img repo/test_tcc
rm -f liwusos.iso
make liwusos.iso 2>&1 | tail -3
[ -f liwusos.iso ] || { echo "iso failed"; exit 1; }

echo "[3/3] Booting xHCI QEMU (timeout ${TIMEOUT}s)..."
rm -f "$SERIAL_LOG"
TEST_DISK="$ROOT_DIR/xhci_boot_disk.img"
dd if=/dev/zero of="$TEST_DISK" bs=1M count=64 2>/dev/null
qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -drive id=disk,file="$TEST_DISK",if=none,format=raw \
    -device ahci,id=ahci \
    -device ide-hd,drive=disk,bus=ahci.0 \
    -device qemu-xhci,id=xhci \
    -device usb-kbd,bus=xhci.0 \
    -device usb-mouse,bus=xhci.0 \
    -m 512 \
    -display none -monitor none -no-reboot \
    -serial "file:$SERIAL_LOG" -accel tcg 2>/dev/null &
QP=$!
ELAPSED=0; RESULT=""
while [ $ELAPSED -lt $TIMEOUT ]; do
    sleep 1; ELAPSED=$((ELAPSED+1))
    if ! kill -0 $QP 2>/dev/null; then break; fi
    if [ -f "$SERIAL_LOG" ]; then
        grep -aq "LIWUS_BOOT_READY" "$SERIAL_LOG" && { RESULT="OK"; break; }
        grep -aq "KERNEL PANIC" "$SERIAL_LOG" 2>/dev/null && { RESULT="PANIC"; break; }
    fi
done
kill $QP 2>/dev/null || true; wait $QP 2>/dev/null || true

echo ""
echo "== Result =="
HID_OK=0
if grep -aq "XHCI: HID KEYBOARD ready" "$SERIAL_LOG" 2>/dev/null &&
   grep -aq "XHCI: HID MOUSE ready" "$SERIAL_LOG" 2>/dev/null; then
    HID_OK=1
fi

if [ "$RESULT" = "OK" ] && [ "$HID_OK" = "1" ]; then
    echo "  [PASS] xHCI boot: keyboard + mouse enumerated, LIWUS_BOOT_READY"
    rm -f "$TEST_DISK"; exit 0
elif [ "$RESULT" = "OK" ]; then
    echo "  [FAIL] booted but HID devices not fully enumerated"
    grep -a "XHCI" "$SERIAL_LOG" | tail -30 || true
    rm -f "$TEST_DISK"; exit 1
else
    echo "  [FAIL] result=$RESULT"
    tail -40 "$SERIAL_LOG" 2>/dev/null || echo "  (no serial)"
    rm -f "$TEST_DISK"; exit 1
fi
