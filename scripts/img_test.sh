#!/bin/bash
# ============================================================
# LiwusOS Image Decode Test
#
# Boots a test-mode ISO, decodes the bundled SDFS images with the
# kernel-side stb_image integration, and validates the decode works.
#
# Usage: bash scripts/img_test.sh
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SERIAL_LOG="$ROOT_DIR/img_serial.log"
TIMEOUT=120

cd "$ROOT_DIR"

echo "============================================"
echo "  LiwusOS Image Decode Test"
echo "============================================"
echo ""

# ---- Step 1: Build kernel ----
echo "[1/4] Building kernel..."
make -j$(nproc 2>/dev/null || echo 4) kernel.bin 2>&1 | tail -3
if [ ! -f kernel.bin ]; then
    echo "[ERROR] kernel.bin build failed"
    exit 1
fi
echo "      kernel.bin OK"
echo ""

# ---- Step 2: Generate test images + assemble initrd ----
echo "[2/4] Generating test images + assembling initrd..."
python3 scripts/gen_test_images.py
mkdir -p repo
touch repo/test_mode
touch repo/test_img
echo "      repo contents:"
ls -la repo/teste.png repo/teste.bmp repo/test_mode repo/test_img 2>/dev/null
echo ""

# ---- Step 3: Build test ISO ----
echo "[3/4] Building test ISO..."
rm -f liwusos.iso
make liwusos.iso 2>&1 | tail -5
if [ ! -f liwusos.iso ]; then
    echo "[ERROR] liwusos.iso build failed"
    exit 1
fi
echo "      liwusos.iso OK"
echo ""

# ---- Step 4: Run QEMU and capture output ----
echo "[4/4] Running image test in QEMU (timeout: ${TIMEOUT}s)..."
echo ""

rm -f "$SERIAL_LOG"

TEST_DISK="$ROOT_DIR/img_test_disk.img"
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
        if grep -q "IMG_ALL_OK" "$SERIAL_LOG" 2>/dev/null && grep -q "IMG_NESTED_OK" "$SERIAL_LOG" 2>/dev/null; then
            RESULT="OK"
            break
        fi
        if grep -q "IMGFAIL:\|IMG_NESTED_MISSING" "$SERIAL_LOG" 2>/dev/null; then
            RESULT="FAIL"
            break
        fi
    fi
done

kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

echo ""
echo "============================================"
echo "  Image Test Result"
echo "============================================"
echo ""

if [ "$RESULT" = "OK" ]; then
    echo "  [PASS] stb_image decode funciona no kernel"
    echo "  [PASS] varredura recursiva acha imagens em subdiretorios"
    grep -E "IMGOK:|IMGFAIL:|IMGSCAN:|IMG_NESTED|IMG_ALL_OK" "$SERIAL_LOG" 2>/dev/null || true
    rm -f "$TEST_DISK"
    exit 0
else
    echo "  [FAIL] Image test nao completou (result=$RESULT)"
    echo ""
    echo "  --- Conteudo do serial ---"
    tail -60 "$SERIAL_LOG" 2>/dev/null || echo "  (nenhuma saida)"
    echo ""
    rm -f "$TEST_DISK"
    exit 1
fi
