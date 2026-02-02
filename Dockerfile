FROM debian:bookworm

# Instala todas as dependências do seu Makefile
RUN apt-get update && apt-get install -y \
    build-essential \
    nasm \
    binutils \
    gcc-multilib \
    grub-pc-bin \
    grub-common \
    xorriso \
    mtools \
    qemu-system-x86 \
    qemu-utils \
    qemu-system-gui \
    console-setup-linux \
    gzip \
    tar \
    --no-install-recommends && \
    rm -rf /var/lib/apt/lists/*

# Garante que a fonte que o seu Makefile procura exista no container
RUN mkdir -p /usr/share/consolefonts/ && \
    touch /usr/share/consolefonts/Uni2-Terminus16.psf.gz

WORKDIR /os-build

# Por padrão, não faz nada, os scripts controlarão as chamadas
CMD ["make", "all"]