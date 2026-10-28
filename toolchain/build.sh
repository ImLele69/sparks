#!/usr/bin/env bash
set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/env.sh"

BINUTILS_VER=2.46.1
GCC_VER=16.1.0
JOBS=$(nproc)

mkdir -p "$SPARKS_ROOT/toolchain"/{src,build-binutils,build-gcc}
cd "$SPARKS_ROOT/toolchain/src"

echo "==> downloading sources"
[ -f binutils-$BINUTILS_VER.tar.xz ] || curl -LO https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz
[ -f gcc-$GCC_VER.tar.xz ] || curl -LO https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz

echo "==> extracting sources"
[ -d binutils-$BINUTILS_VER ] || tar xf binutils-$BINUTILS_VER.tar.xz
[ -d gcc-$GCC_VER ] || tar xf gcc-$GCC_VER.tar.xz

echo "==> downloading gcc prerequisites (gmp/mpfr/mpc/isl)"
(cd gcc-$GCC_VER && ./contrib/download_prerequisites)

echo "==> building binutils"
cd "$SPARKS_ROOT/toolchain/build-binutils"
../src/binutils-$BINUTILS_VER/configure \
    --target="$TARGET" --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror
make -j"$JOBS"
make install

echo "==> building gcc (cross, stage: gcc + libgcc, no libc/headers yet)"
cd "$SPARKS_ROOT/toolchain/build-gcc"
../src/gcc-$GCC_VER/configure \
    --target="$TARGET" --prefix="$PREFIX" \
    --disable-nls --enable-languages=c,c++ --without-headers
make -j"$JOBS" all-gcc
make -j"$JOBS" all-target-libgcc
make install-gcc
make install-target-libgcc

echo "==> done. cross compiler installed at $PREFIX/bin/$TARGET-gcc"
"$PREFIX/bin/$TARGET-gcc" --version
