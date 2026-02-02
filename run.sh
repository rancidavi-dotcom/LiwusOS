#!/bin/bash
# Autoriza o Docker a acessar sua tela
xhost +local:docker > /dev/null

# Rebuild para garantir o qemu-system-gui
docker build -t liwus-builder .

# Roda o QEMU garantindo que ele use a interface GTK ou SDL do container
docker run --rm -it \
    -v "$(pwd)":/os-build \
    -e DISPLAY=$DISPLAY \
    -v /tmp/.X11-unix:/tmp/.X11-unix \
    --device /dev/dri:/dev/dri \
    liwus-builder make run