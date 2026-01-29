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

# Build flags for TCC itself (HOST)
HOST_CFLAGS="-std=gnu99 -O2 -Wall -Wextra -I$TCC_SRC -DONE_SOURCE=0"
HOST_LDFLAGS="-lm -ldl -lpthread"

# Build flags for TARGET (LiwusOS) - these get embedded in TCC
TARGET_CFLAGS="-std=gnu99 -ffreestanding -O2 -Wall -Wextra -I$LIWUSOS_INCLUDE -m64 -mno-red-zone -fno-pie -fno-pic"
TARGET_LDFLAGS="-nostdlib -static"

mkdir -p $TCC_BUILD/obj
cd $TCC_BUILD/obj

# Compile TCC source files with HOST includes (not LiwusOS SDK)
for f in $TCC_SRC/tcc.c $TCC_SRC/libtcc.c $TCC_SRC/tccpp.c $TCC_SRC/tccgen.c $TCC_SRC/tccdbg.c $TCC_SRC/tccelf.c $TCC_SRC/tccasm.c $TCC_SRC/tccrun.c $TCC_SRC/x86_64-gen.c $TCC_SRC/x86_64-link.c $TCC_SRC/i386-asm.c; do
    echo "Compiling $f..."
    $CC $HOST_CFLAGS -c $f -o $(basename $f .c).o
done

# Link TCC host binary
$CC $HOST_CFLAGS $HOST_LDFLAGS -o tcc \
    tcc.o libtcc.o tccpp.o tccgen.o tccdbg.o tccelf.o tccasm.o tccrun.o x86_64-gen.o x86_64-link.o i386-asm.o

# Now build libtcc1.a for LiwusOS target using the host tcc we just built
# Use HOST headers for libtcc1.a (it's a runtime library, not target code)
cd $TCC_BUILD
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/libtcc1.c -o libtcc1.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/stdatomic.c -o stdatomic.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/atomic.S -o atomic.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/builtin.c -o builtin.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/alloca.S -o alloca.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/alloca-bt.S -o alloca-bt.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/tcov.c -o tcov.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/va_list.c -o va_list.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/dsohandle.c -o dsohandle.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/runmain.c -o runmain.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/bt-exe.c -o bt-exe.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/bt-log.c -o bt-log.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include
$TCC_BUILD/obj/tcc -c $TCC_SRC/lib/bcheck.c -o bcheck.o -B$TCC_BUILD/obj -I$TCC_SRC -I$TCC_SRC/include -bt

$AR rcs libtcc1.a libtcc1.o stdatomic.o atomic.o builtin.o alloca.o alloca-bt.o tcov.o va_list.o dsohandle.o runmain.o bt-exe.o bt-log.o bcheck.o

$AR rcs libtcc1.a libtcc1.o stdatomic.o atomic.o builtin.o alloca.o alloca-bt.o tcov.o va_list.o dsohandle.o runmain.o bt-exe.o bt-log.o bcheck.o

# Create target install directory
mkdir -p $TCC_BUILD/install-target/bin
mkdir -p $TCC_BUILD/install-target/lib/tcc
mkdir -p $TCC_BUILD/install-target/lib/tcc/include

# Copy target TCC binary (rebuilt with target flags)
cd $TCC_BUILD/obj
$CC $TARGET_CFLAGS -c $TCC_SRC/tcc.c -o tcc-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/libtcc.c -o libtcc-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/tccpp.c -o tccpp-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/tccgen.c -o tccgen-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/tccdbg.c -o tccdbg-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/tccelf.c -o tccelf-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/tccasm.c -o tccasm-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/tccrun.c -o tccrun-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/x86_64-gen.c -o x86_64-gen-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/x86_64-link.c -o x86_64-link-target.o
$CC $TARGET_CFLAGS -c $TCC_SRC/i386-asm.c -o i386-asm-target.o

$CC $TARGET_CFLAGS $TARGET_LDFLAGS -o tcc-target \
    tcc-target.o libtcc-target.o tccpp-target.o tccgen-target.o tccdbg-target.o tccelf-target.o tccasm-target.o tccrun-target.o x86_64-gen-target.o x86_64-link-target.o i386-asm-target.o \
    $CRT0 $LIBC $LIBM -lgcc

# Copy target files
cp tcc-target $TCC_BUILD/install-target/bin/tcc
cp $TCC_BUILD/libtcc1.a $TCC_BUILD/install-target/lib/tcc/
cp $TCC_SRC/include/*.h $TCC_BUILD/install-target/lib/tcc/include/
cp $TCC_SRC/tcclib.h $TCC_BUILD/install-target/lib/tcc/include/

echo "=========================================="
echo "TCC for LiwusOS built successfully!"
echo "Binary: $TCC_BUILD/install-target/bin/tcc"
echo "libtcc1.a: $TCC_BUILD/install-target/lib/tcc/libtcc1.a"
echo "Headers: $TCC_BUILD/install-target/lib/tcc/include/"
echo "=========================================="