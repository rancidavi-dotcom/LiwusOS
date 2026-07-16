#!/bin/bash
set -e

# Build usando Docker (cross-compiler + ferramentas de ISO)
docker build -t liwus-builder .

# Cria disco persistente se nao existir (agora 512MB para mais espaco)
if [ ! -f liwus_disk.img ]; then
    qemu-img create -f raw liwus_disk.img 512M
fi

# Compila tudo dentro do Docker e gera a ISO
docker run --rm \
    -v "$(pwd)":/os-build \
    liwus-builder make all

# Roda o QEMU nativamente (fora do Docker)
# Disco ATA nativo (PIIX3/PATA) para persistencia + PS/2 para teclado/mouse
# Sem USB: o chipset PIIX3 ja fornece controladora IDE e PS/2 nativamente
exec qemu-system-i386 \
    -m 512M \
    -boot order=dc \
    -drive file=liwusos.iso,format=raw,if=ide,index=2,media=cdrom \
    -drive file=liwus_disk.img,format=raw,if=ide,index=0,media=disk \
    -vga std \
    -serial stdio \
    -net nic,model=rtl8139 \
    -net user,hostfwd=tcp::2222-:2222
