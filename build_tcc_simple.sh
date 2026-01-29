#!/bin/bash
set -e

TCC_SRC=/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/third_party/tcc
TCC_BUILD=/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/third_party/tcc-liwusos-build
SDK_DIR=/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/sdk

CC=gcc
AR=ar

LIWUSOS_INCLUDE=$SDK_DIR/include
LIWUSOS_LIB=$SDK_DIR/lib
CRT0=$SDK_DIR/lib/libgloss.a
LIBC=$SDK_DIR/lib/libc.a
LIBM=$SDK_DIR/lib/libm.a

USER_CFLAGS="-std=gnu99 -ffreestanding -O2 -Wall -Wextra -I$LIWUSOS_INCLUDE -m64 -mno-red-zone -fno-pie -fno-pic"
USER_LDFLAGS="-nostdlib -static"

mkdir -p $TCC_BUILD/obj
cd $TCC_BUILD/obj

# Compile TCC source files
for f in $TCC_SRC/tcc.c $TCC_SRC/libtcc.c $TCC_SRC/tccpp.c $TCC_SRC/tccgen.c $TCC_SRC/tccdbg.c $TCC_SRC/tccelf.c $TCC_SRC/tccasm.c $TCC_SRC/tccrun.c $TCC_SRC/x86_64-gen.c $TCC_SRC/x86_64-link.c $TCC_SRC/i386-asm.c; do
    echo "Compiling $f..."
    $CC $USER_CFLAGS -c $f -o $(basename $f .c).o
done

# Link TCC
$CC $USER_CFLAGS $USER_LDFLAGS -o tcc \
    tcc.o libtcc.o tccpp.o tccgen.o tccdbg.o tccelf.o tccasm.o tccrun.o x86_64-gen.o x86_64-link.o i386-asm.o \
    $CRT0 $LIBC $LIBM -lgcc

echo "TCC built at $TCC_BUILD/obj/tcc"