#!/bin/bash
set -e

# Autoriza o Docker a acessar sua tela quando houver um servidor X disponivel.
if [ -n "${DISPLAY:-}" ] && command -v xhost >/dev/null 2>&1; then
    xhost +local:docker > /dev/null
fi

# Rebuild para garantir o qemu-system-gui
docker build -t liwus-builder .

# Roda o QEMU garantindo que ele use a interface GTK ou SDL do container
DOCKER_TTY_FLAGS=""
if [ -t 0 ] && [ -t 1 ]; then
    DOCKER_TTY_FLAGS="-it"
fi

docker run --rm ${DOCKER_TTY_FLAGS} \
    --network host \
    --user "$(id -u):$(id -g)" \
    -v "$(pwd)":/os-build \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    --device /dev/dri:/dev/dri \
    liwus-builder bash -c "if [ ! -f liwus_disk.img ]; then qemu-img create -f raw liwus_disk.img 100M; fi && make all && qemu-system-i386 -cdrom liwusos.iso -drive file=liwus_disk.img,format=raw,index=0,media=disk -m 512M -boot d -vga std -device virtio-gpu-pci -serial stdio -net nic,model=rtl8139 -net user,hostfwd=tcp::2222-:2222"
