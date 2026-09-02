#!/bin/bash
# ============================================================
# LiwusOS Automated Test Runner
#
# Builds a test-mode ISO, boots QEMU headless, captures serial
# output, and validates test results.
#
# Usage:
#   bash scripts/run_tests.sh --sdfs    # SDFS kernel tests only
#   bash scripts/run_tests.sh --user    # Userspace tests only
#   bash scripts/run_tests.sh --full    # Both (default)
# ============================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
SERIAL_LOG="$ROOT_DIR/test_serial.log"
TIMEOUT=45
MODE="${1:---full}"

cd "$ROOT_DIR"

echo "============================================"
echo "  LiwusOS Test Suite"
echo "  Mode: $MODE"
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

# ---- Step 2: Build userspace test runner (for --user/--full) ----
if [ "$MODE" = "--user" ] || [ "$MODE" = "--full" ]; then
    echo "[2/4] Building userspace test runner..."
    make -j$(nproc 2>/dev/null || echo 4) tests/test_runner.elf 2>&1 | tail -3
    if [ ! -f tests/test_runner.elf ]; then
        echo "[ERROR] test_runner.elf build failed"
        exit 1
    fi
    echo "      test_runner.elf OK"
else
    echo "[2/4] Skipping userspace tests (mode: $MODE)"
fi
echo ""

# ---- Step 3: Build test ISO ----
echo "[3/4] Building test ISO..."

mkdir -p repo

# Create test_mode marker for sdfs/user/full tests
if [ "$MODE" = "--sdfs" ] || [ "$MODE" = "--user" ] || [ "$MODE" = "--full" ]; then
    touch repo/test_mode
fi

# Copy userspace test runner into initrd if building user tests
if [ "$MODE" = "--user" ] || [ "$MODE" = "--full" ]; then
    if [ -f tests/test_runner.elf ]; then
        cp tests/test_runner.elf repo/test_runner
    fi
else
    # Ensure no stale test_runner from a previous --user/--full run remains
    rm -f repo/test_runner
fi

# Build ISO (kernel + initrd + test_mode marker)
make liwusos.iso 2>&1 | tail -5
if [ ! -f liwusos.iso ]; then
    echo "[ERROR] liwusos.iso build failed"
    exit 1
fi
echo "      liwusos.iso OK"
echo ""

# ---- Step 4: Run QEMU and capture results ----
echo "[4/4] Running tests in QEMU (timeout: ${TIMEOUT}s)..."
echo ""

# Clean previous log
rm -f "$SERIAL_LOG"

# Create a temporary disk for tests (isolated, no persistent data)
TEST_DISK="$ROOT_DIR/test_disk.img"
dd if=/dev/zero of="$TEST_DISK" bs=1M count=64 2>/dev/null

# Launch QEMU headless with serial to file
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

# Wait for test output or timeout
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
        if grep -q "RESULT: ALL PASS" "$SERIAL_LOG" 2>/dev/null; then
            RESULT="ALL_PASS"
            break
        fi
        if grep -q "RESULT: FAIL" "$SERIAL_LOG" 2>/dev/null; then
            RESULT="FAIL"
            break
        fi
    fi
done

# Kill QEMU
kill $QEMU_PID 2>/dev/null || true
wait $QEMU_PID 2>/dev/null || true

# ---- Report ----
echo ""
echo "============================================"
echo "  Test Results"
echo "============================================"
echo ""

if [ "$RESULT" = "ALL_PASS" ]; then
    PASS_COUNT=$(grep -c "  PASS:" "$SERIAL_LOG" 2>/dev/null || echo "0")
    echo "  [PASS] Todos os $PASS_COUNT testes passaram"
    echo ""
    grep -E "\[TEST\]|  PASS:" "$SERIAL_LOG" 2>/dev/null || true
    echo ""
    rm -f "$TEST_DISK"
    exit 0

elif [ "$RESULT" = "FAIL" ]; then
    echo "  [FAIL] Alguns testes falharam"
    echo ""
    echo "  --- Detalhes ---"
    grep -E "\[TEST\]|  PASS:|  FAIL:|  ASSERT FAIL:" "$SERIAL_LOG" 2>/dev/null || true
    echo ""
    rm -f "$TEST_DISK"
    exit 1

else
    echo "  [TIMEOUT] Testes nao completaram em ${TIMEOUT}s"
    echo ""
    echo "  --- Ultimas 30 linhas do serial ---"
    tail -30 "$SERIAL_LOG" 2>/dev/null || echo "  (nenhuma saida)"
    echo ""
    rm -f "$TEST_DISK"
    exit 1
fi
