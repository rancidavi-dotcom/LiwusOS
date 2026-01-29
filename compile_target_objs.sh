#!/bin/bash
cd /mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/third_party/tcc-liwusos-build
TCC_SRC=/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/third_party/tcc
SDK_DIR=/mnt/c/Users/davivbr/Desktop/LiwusOS/LiwusOS/sdk

# Compile target TCC objects with HOST headers (not SDK)
# The target paths will be embedded in the binary via config.h
HOST_CFLAGS="-std=gnu99 -O2 -Wall -Wextra -I$TCC_SRC -DONE_SOURCE=0"
TARGET_LDFLAGS="-nostdlib -static"
CRT0=$SDK_DIR/lib/libgloss.a
LIBC=$SDK_DIR/lib/libc.a
LIBM=$SDK_DIR/lib/libm.a

for f in libtcc.c tccpp.c tccgen.c tccdbg.c tccelf.c tccasm.c tccrun.c x86_64-gen.c x86_64-link.c i386-asm.c; do
    echo "Compiling $f..."
    gcc $HOST_CFLAGS -c $TCC_SRC/$f -o ${f%.c}-target.o
done
ls -la *-target.o