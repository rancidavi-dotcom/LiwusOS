#!/bin/bash
cd /mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/third_party/tcc-liwusos-build
SDK_DIR=/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/sdk

TARGET_CFLAGS="-std=gnu99 -ffreestanding -O2 -Wall -Wextra -I$SDK_DIR/include -m64 -mno-red-zone -fno-pie -fno-pic"
TARGET_LDFLAGS="-nostdlib -static"
CRT0=$SDK_DIR/lib/libgloss.a
LIBC=$SDK_DIR/lib/libc.a
LIBM=$SDK_DIR/lib/libm.a

gcc $TARGET_CFLAGS $TARGET_LDFLAGS -o tcc-target \
    tcc-target.o libtcc-target.o tccpp-target.o tccgen-target.o tccdbg-target.o tccelf-target.o tccasm-target.o tccrun-target.o x86_64-gen-target.o x86_64-link-target.o i386-asm-target.o \
    $CRT0 $LIBC $LIBM -lgcc

echo "Copying to install-target..."
cp tcc-target install-target/bin/tcc
ls -la install-target/bin/tcc