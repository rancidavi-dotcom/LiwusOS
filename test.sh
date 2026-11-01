#!/bin/bash
# ============================================================
# LiwusOS - Script de teste conveniente
# ============================================================

set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT_DIR"

usage() {
    cat <<EOF
Uso: $0 [opcao]

Opcoes:
  build       Compila apenas o kernel (kernel.bin)
  iso         Compila kernel + gera ISO bootavel (liwusos.iso)
  run         Builda ISO, roda QEMU e abre o VNC automaticamente
  run-serial  Builda ISO e roda headless (serial -> terminal, -display none)
  run-log     Builda ISO e roda headless (serial -> qemu_serial.log, debug log)
  test        Roda suite de testes do kernel (SDFS + scheduler)
  test-build  Apenas compila kernel de teste + ISO de teste
  clean       Limpa artefatos de build (preserva liwus_disk.img)
  help        Mostra esta ajuda

Exemplos:
  $0 build
  $0 run
  $0 run-serial
  $0 test
EOF
}

cmd="${1:-help}"

case "$cmd" in
    build)
        echo "[1/1] Compilando kernel..."
        make -j$(nproc 2>/dev/null || echo 4) kernel.bin
        echo "OK: kernel.bin ($(stat -c%s kernel.bin) bytes)"
        ;;

    iso)
        echo "[1/2] Compilando kernel..."
        make -j$(nproc 2>/dev/null || echo 4) kernel.bin
        echo "[2/2] Gerando ISO..."
        make liwusos.iso
        echo "OK: liwusos.iso ($(stat -c%s liwusos.iso) bytes)"
        ;;

    run)
        echo "=== Build + Run (janela grafica via VNC) ==="
        make -j$(nproc 2>/dev/null || echo 4) liwusos.iso
        if command -v vncviewer >/dev/null 2>&1; then
            viewer=vncviewer
        elif command -v tigervncviewer >/dev/null 2>&1; then
            viewer=tigervncviewer
        else
            echo "Visualizador VNC nao encontrado. Instale: sudo apt install tigervnc-viewer" >&2
            exit 1
        fi

        # O QEMU fica em primeiro plano para continuar recebendo a entrada serial.
        # O visualizador aguarda o servidor VNC antes de abrir a janela.
        (
            for ((attempt = 0; attempt < 100; attempt++)); do
                if (echo > /dev/tcp/127.0.0.1/5901) 2>/dev/null; then
                    exec "$viewer" 127.0.0.1:1
                fi
                sleep 0.1
            done
            echo "O servidor VNC nao ficou disponivel em 127.0.0.1:5901." >&2
            exit 1
        ) &
        viewer_pid=$!
        trap 'kill "$viewer_pid" 2>/dev/null || true; wait "$viewer_pid" 2>/dev/null || true' EXIT
        echo "Abrindo VNC automaticamente. Pressione Ctrl+A X para sair do QEMU."
        qemu-system-x86_64 -machine q35 -m 2048 -no-reboot \
            -drive file=liwus_disk.img,format=raw,if=none,id=disk \
            -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
            -cdrom liwusos.iso -vnc 127.0.0.1:1 -serial mon:stdio
        ;;

    run-serial)
        echo "=== Build + Run (headless, serial no terminal) ==="
        make -j$(nproc 2>/dev/null || echo 4) liwusos.iso
        echo "Pressione Ctrl+A X para sair do QEMU"
        qemu-system-x86_64 -machine q35 -m 2048 -display none -monitor none -serial stdio -no-reboot \
            -drive file=liwus_disk.img,format=raw,if=none,id=disk \
            -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
            -cdrom liwusos.iso
        ;;

    run-log)
        echo "=== Build + Run (headless, serial -> qemu_serial.log) ==="
        make -j$(nproc 2>/dev/null || echo 4) liwusos.iso
        rm -f qemu_serial.log qemu_debug.log
        qemu-system-x86_64 -machine q35 -m 2048 -display none -monitor none \
            -serial file:qemu_serial.log -D qemu_debug.log -d int,cpu_reset -no-reboot \
            -drive file=liwus_disk.img,format=raw,if=none,id=disk \
            -device ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0 \
            -cdrom liwusos.iso
        echo ""
        echo "=== Ultimas 30 linhas do serial ==="
        tail -30 qemu_serial.log
        ;;

    test)
        echo "=== Suite de testes do kernel ==="
        make test
        ;;

    test-build)
        echo "[1/2] Compilando kernel de teste..."
        make -j$(nproc 2>/dev/null || echo 4) kernel-test.bin
        echo "[2/2] Gerando ISO de teste..."
        make liwusos-test.iso
        echo "OK: liwusos-test.iso"
        ;;

    clean)
        echo "Limpando artefatos..."
        make clean
        echo "OK (liwus_disk.img preservado)"
        ;;

    help|*)
        usage
        ;;
esac