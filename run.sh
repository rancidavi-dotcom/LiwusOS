#!/bin/bash
set -e

# Build usando Docker (cross-compiler + ferramentas de ISO)
docker build -t liwus-builder .

# Cria disco se nao existir
if [ ! -f liwus_disk.img ]; then
    qemu-img create -f raw liwus_disk.img 100M
fi

# Compila tudo dentro do Docker e gera a ISO
docker run --rm \
    -v "$(pwd)":/os-build \
    liwus-builder make all

# Roda o QEMU nativamente (fora do Docker)
exec qemu-system-i386 \
    -nodefaults \
    -cdrom liwusos.iso \
    -drive file=liwus_disk.img,format=raw,index=0,media=disk \
    -m 512M \
    -boot d \
    -vga std \
    -serial stdio \
    -net nic,model=rtl8139 \
    -net user,hostfwd=tcp::2222-:2222 \
    -usb -device usb-ehci,id=ehci -device usb-kbd,bus=ehci.0 -device usb-mouse,bus=ehci.0
