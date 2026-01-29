#!/usr/bin/env bash
set -euo pipefail

TARGET=x86_64-liwusos
PREFIX="${PREFIX:-/opt/liwusos-toolchain}"
JOBS="${JOBS:-$(nproc)}"
WORKDIR="${WORKDIR:-/tmp/liwusos-toolchain-build}"
BINUTILS_VERSION="${BINUTILS_VERSION:-2.42}"
GCC_VERSION="${GCC_VERSION:-14.1.0}"
NEWLIB_VERSION="${NEWLIB_VERSION:-4.4.0.20231231}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mkdir -p "$WORKDIR/src" "$WORKDIR/build" "$PREFIX"
cd "$WORKDIR/src"

if [ ! -f "binutils-$BINUTILS_VERSION.tar.xz" ]; then
  wget "https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VERSION.tar.xz"
fi
if [ ! -f "gcc-$GCC_VERSION.tar.xz" ]; then
  wget "https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz"
fi
if [ ! -f "newlib-$NEWLIB_VERSION.tar.xz" ]; then
  if [ -f "/usr/src/newlib/newlib-$NEWLIB_VERSION.tar.xz" ]; then
    cp "/usr/src/newlib/newlib-$NEWLIB_VERSION.tar.xz" .
  else
    wget "https://sourceware.org/pub/newlib/newlib-$NEWLIB_VERSION.tar.gz" -O "newlib-$NEWLIB_VERSION.tar.gz"
  fi
fi

rm -rf "binutils-$BINUTILS_VERSION" "gcc-$GCC_VERSION" "newlib-$NEWLIB_VERSION"
tar xf "binutils-$BINUTILS_VERSION.tar.xz"
tar xf "gcc-$GCC_VERSION.tar.xz"
if [ -f "newlib-$NEWLIB_VERSION.tar.xz" ]; then
  tar xf "newlib-$NEWLIB_VERSION.tar.xz"
else
  tar xf "newlib-$NEWLIB_VERSION.tar.gz"
fi

patch_config_sub() {
  local file="$1"
  if ! grep -q 'liwusos\*' "$file"; then
    sed -i 's/| fiwix\* )/| fiwix* | liwusos* )/' "$file"
  fi
}

patch_config_sub "binutils-$BINUTILS_VERSION/config.sub"
patch_config_sub "gcc-$GCC_VERSION/config.sub"
patch_config_sub "newlib-$NEWLIB_VERSION/config.sub"

if ! grep -q 'x86_64-.*liwusos' "newlib-$NEWLIB_VERSION/newlib/configure.host"; then
  sed -i '/^  x86_64-\*-rtems\*)/i\  x86_64-*-liwusos*)\n\tsys_dir=liwus\n\tnewlib_cflags="${newlib_cflags} -DMISSING_SYSCALL_NAMES -D__LARGE64_FILES"\n\t;;\n' \
    "newlib-$NEWLIB_VERSION/newlib/configure.host"
fi

mkdir -p "newlib-$NEWLIB_VERSION/newlib/libc/sys/liwus/include/sys"
cp "$REPO_ROOT/libgloss/syscalls.c" "newlib-$NEWLIB_VERSION/newlib/libc/sys/liwus/syscalls.c"
cp "$REPO_ROOT/libgloss/crt0.S" "newlib-$NEWLIB_VERSION/newlib/libc/sys/liwus/crt0.S"
cat > "newlib-$NEWLIB_VERSION/newlib/libc/sys/liwus/Makefile.inc" <<'EOF'
libc_a_SOURCES += \
	%D%/syscalls.c
EOF

if ! grep -q 'x86_64-\*-liwusos\*' "gcc-$GCC_VERSION/gcc/config.gcc"; then
  sed -i '/^x86_64-\*-elf\*)/i\x86_64-*-liwusos*)\n\ttm_file="${tm_file} i386/unix.h i386/att.h dbxelf.h elfos.h newlib-stdint.h i386/i386elf.h i386/x86-64.h liwusos.h"\n\ttmake_file="${tmake_file} i386/t-i386elf t-slibgcc"\n\t;;\n' \
    "gcc-$GCC_VERSION/gcc/config.gcc"
fi

cat > "gcc-$GCC_VERSION/gcc/config/liwusos.h" <<'EOF'
#undef TARGET_LIWUSOS
#define TARGET_LIWUSOS 1

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()        \
  do {                                  \
    builtin_define("__liwusos__");      \
    builtin_define("__unix__");         \
    builtin_assert("system=liwusos");   \
    builtin_assert("system=unix");      \
  } while (0)

#undef LIB_SPEC
#define LIB_SPEC "-lc -lm -lgloss"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC ""
EOF

export PATH="$PREFIX/bin:$PATH"

rm -rf "$WORKDIR/build/binutils"
mkdir -p "$WORKDIR/build/binutils"
cd "$WORKDIR/build/binutils"
"$WORKDIR/src/binutils-$BINUTILS_VERSION/configure" \
  --target="$TARGET" \
  --prefix="$PREFIX" \
  --with-sysroot="$PREFIX/$TARGET" \
  --disable-nls \
  --disable-werror
make -j"$JOBS"
make install

rm -rf "$WORKDIR/build/gcc-stage1"
mkdir -p "$WORKDIR/build/gcc-stage1"
cd "$WORKDIR/build/gcc-stage1"
"$WORKDIR/src/gcc-$GCC_VERSION/configure" \
  --target="$TARGET" \
  --prefix="$PREFIX" \
  --with-sysroot="$PREFIX/$TARGET" \
  --with-newlib \
  --without-headers \
  --enable-languages=c \
  --disable-nls \
  --disable-shared \
  --disable-threads \
  --disable-libssp \
  --disable-libgomp \
  --disable-libquadmath \
  --disable-libatomic \
  --disable-multilib
make -j"$JOBS" all-gcc all-target-libgcc
make install-gcc install-target-libgcc

rm -rf "$WORKDIR/build/newlib"
mkdir -p "$WORKDIR/build/newlib"
cd "$WORKDIR/build/newlib"
"$WORKDIR/src/newlib-$NEWLIB_VERSION/configure" \
  --target="$TARGET" \
  --prefix="$PREFIX" \
  --disable-multilib \
  CFLAGS_FOR_TARGET='-O2 -ffreestanding -mno-red-zone -fno-pie -fno-pic'
make -j"$JOBS" all-target-newlib
make install-target-newlib

"$TARGET-gcc" -c "$REPO_ROOT/libgloss/crt0.S" -o "$PREFIX/$TARGET/lib/crt0.o" \
  -O2 -ffreestanding -mno-red-zone -fno-pie -fno-pic
"$TARGET-gcc" -c "$REPO_ROOT/libgloss/syscalls.c" -o "$WORKDIR/build/liwusos-syscalls.o" \
  -O2 -ffreestanding -mno-red-zone -fno-pie -fno-pic
"$TARGET-ar" rcs "$PREFIX/$TARGET/lib/libgloss.a" "$WORKDIR/build/liwusos-syscalls.o"

rm -rf "$WORKDIR/build/gcc-final"
mkdir -p "$WORKDIR/build/gcc-final"
cd "$WORKDIR/build/gcc-final"
"$WORKDIR/src/gcc-$GCC_VERSION/configure" \
  --target="$TARGET" \
  --prefix="$PREFIX" \
  --with-sysroot="$PREFIX/$TARGET" \
  --with-newlib \
  --enable-languages=c,c++ \
  --disable-nls \
  --disable-shared \
  --disable-threads \
  --disable-libssp \
  --disable-libgomp \
  --disable-libquadmath \
  --disable-libatomic \
  --disable-multilib
make -j"$JOBS" all-gcc all-target-libgcc
make install-gcc install-target-libgcc

"$TARGET-gcc" --version
"$TARGET-ld" --version | head -1
