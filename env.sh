# source this before toolchain/build.sh or to use the cross compiler by hand
SPARKS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export SPARKS_ROOT
export TARGET=x86_64-elf
export PREFIX="$SPARKS_ROOT/toolchain/cross"
export PATH="$PREFIX/bin:$PATH"
