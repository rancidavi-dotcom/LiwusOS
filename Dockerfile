FROM debian:bookworm

# Instala dependências de build necessárias para compilar a toolchain
RUN apt-get update && apt-get install -y \
    build-essential \
    bison \
    ca-certificates \
    flex \
    libgmp3-dev \
    libmpc-dev \
    libmpfr-dev \
    texinfo \
    nasm \
    grub-pc-bin \
    grub-common \
    xorriso \
    mtools \
    qemu-system-gui \
    qemu-system-x86 \
    qemu-utils \
    gzip \
    tar \
    curl \
    wget \
    --no-install-recommends && \
    rm -rf /var/lib/apt/lists/*

# Compila o cross-compiler i686-elf dentro do container
RUN mkdir -p /build && cd /build && \
    wget https://ftp.gnu.org/gnu/binutils/binutils-2.42.tar.gz && \
    tar xf binutils-2.42.tar.gz && \
    mkdir build-binutils && cd build-binutils && \
    ../binutils-2.42/configure --target=i686-elf --prefix=/usr/local --with-sysroot --disable-nls --disable-werror && \
    make -j$(nproc) && make install && \
    cd /build && \
    wget https://ftp.gnu.org/gnu/gcc/gcc-13.2.0/gcc-13.2.0.tar.gz && \
    tar xf gcc-13.2.0.tar.gz && \
    mkdir build-gcc && cd build-gcc && \
    ../gcc-13.2.0/configure --target=i686-elf --prefix=/usr/local --disable-nls --enable-languages=c --without-headers && \
    make all-gcc -j$(nproc) && make install-gcc && \
    make all-target-libgcc -j$(nproc) && make install-target-libgcc && \
    rm -rf /build

WORKDIR /os-build
CMD ["make", "all"]
