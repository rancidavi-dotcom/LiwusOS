#!/bin/bash
# ============================================================
# LiwusOS Tiny C Compiler (TCC) Integration Test
#
# Boots a test-mode ISO, launches the userspace TCC to compile
# hello_tcc.c inside the OS, and validates the produced .o file.
#
# Usage: bash scripts/tcc_test.sh
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SERIAL_LOG="$ROOT_DIR/tcc_serial.log"
TIMEOUT=120

cd "$ROOT_DIR"

echo "============================================"
echo "  LiwusOS TCC Integration Test"
echo "============================================"
echo ""

# ---- Step 1: Build tcc.elf ----
echo "[1/5] Building TCC..."
make -j$(nproc 2>/dev/null || echo 4) apps/tcc/tcc.elf 2>&1 | tail -3
if [ ! -f apps/tcc/tcc.elf ]; then
    echo "[ERROR] tcc.elf build failed"
    exit 1
fi
echo "      tcc.elf OK"
echo ""

# ---- Step 2: Build kernel ----
echo "[2/5] Building kernel..."
make -j$(nproc 2>/dev/null || echo 4) kernel.bin 2>&1 | tail -3
if [ ! -f kernel.bin ]; then
    echo "[ERROR] kernel.bin build failed"
    exit 1
fi
echo "      kernel.bin OK"
echo ""

# ---- Step 3: Assemble test initrd ----
echo "[3/5] Assembling test initrd..."
mkdir -p repo
touch repo/test_mode
touch repo/test_tcc
cp apps/tcc/hello_tcc.c repo/hello_tcc.c
echo "      repo ready"
echo ""

# ---- Step 4: Build test ISO ----
echo "[4/5] Building test ISO..."
# Force rebuild since repo/test_tcc was just created
rm -f liwusos.iso
echo "      repo contents before make:"
ls -la repo/
make liwusos.iso 2>&1 | tail -5
echo "      repo contents after make:"
ls -la repo/
if [ ! -f liwusos.iso ]; then
    echo "[ERROR] liwusos.iso build failed"
    exit 1
fi
echo "      liwusos.iso OK"
echo ""

# ---- Step 5: Run QEMU and capture output ----
echo "[5/5] Running TCC test in QEMU (timeout: ${TIMEOUT}s)..."
echo ""

rm -f "$SERIAL_LOG"

TEST_DISK="$ROOT_DIR/tcc_test_disk.img"
dd if=/dev/zero of="$TEST_DISK" bs=1M count=64 2>/dev/null

qemu-system-x86_64 \
    -cdrom liwusos.iso \
    -drive id=disk,file="$TEST_DISK",if=none,format=raw \
    -device ahci,id=ahci \
    -device ide-hd,drive=disk,bus=ahci.0 \
    -m 512 \
    -display none \
    -monitor none \
    -no-reboot \
    -serial "file:$SERIAL_LOG" \
    -accel tcg 2>/dev/null &
QEMU_PID=$!

ELAPSED=0
RESULT=""
while [ $ELAPSED -lt $TIMEOUT ]; do
    sleep 1
    ELAPSED=$((ELAPSED + 1))

    if ! kill -0 $QEMU_PID 2>/dev/null; then
        echo "      QEMU exited at ${ELAPSED}s"
        break
    fi

    if [ -f "$SERIAL_LOG" ]; then
        if grep -q "OK: libtcc compiled successfully" "$SERIAL_LOG" 2>/dev/null; then
            RESULT="OK"
            break
        fi
        if grep -q "FAIL:" "$SERIAL_LOG" 2>/dev/null; then
            RESULT="FAIL"
            break
        fi
    fi
done

# Kill QEMU
kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

echo ""
echo "============================================"
echo "  TCC Test Result"
echo "============================================"
echo ""

if [ "$RESULT" = "OK" ]; then
    echo "  [PASS] libtcc API compilou com sucesso dentro do LiwusOS"
    grep -E "OK:|FAIL:" "$SERIAL_LOG" 2>/dev/null || true
    rm -f "$TEST_DISK"
    exit 0
else
    echo "  [FAIL] TCC test nao completou (result=$RESULT)"
    echo ""
    echo "  --- Conteudo do serial ---"
    tail -60 "$SERIAL_LOG" 2>/dev/null || echo "  (nenhuma saida)"
    echo ""
    rm -f "$TEST_DISK"
    exit 1
fi