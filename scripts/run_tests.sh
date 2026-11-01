#!/bin/bash
# ============================================================
# LiwusOS - runner de testes do kernel (SDFS, scheduler)
#
# Uso:
#   scripts/run_tests.sh
#
# Compila um kernel de teste (-DKERNEL_TEST), cria uma ISO com o
# marcador test_mode no initrd, boota no QEMU headless e valida o
# serial em busca de "RESULT: ALL PASS".
# ============================================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT_DIR"

make test
