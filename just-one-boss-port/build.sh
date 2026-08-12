#!/bin/bash
#
# build.sh - build Just One Boss for the Classic Macintosh with Retro68.
# Reuses the Retro68 toolchain already installed at ~/Retro68-build
# (same one the ~/Downloads Mac projects use).  Output: JustOneBoss.dsk
#
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
RETRO68_BUILD="$HOME/Retro68-build"
TOOLCHAIN="$RETRO68_BUILD/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"

if [ ! -f "$TOOLCHAIN" ]; then
  echo "!! Retro68 toolchain not found at $TOOLCHAIN"
  echo "   Install Retro68 (see BUILD.md §2) or point RETRO68_BUILD at it."
  exit 1
fi

echo "==> Compiling Just One Boss..."
BUILD_DIR="$PROJECT_DIR/build"
rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"
cmake .. -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -GNinja
cmake --build .

if [ -f "$BUILD_DIR/JustOneBoss.dsk" ]; then
  cp "$BUILD_DIR/JustOneBoss.dsk" "$PROJECT_DIR/JustOneBoss.dsk"
  cp "$BUILD_DIR/JustOneBoss.bin" "$PROJECT_DIR/JustOneBoss.bin"
  echo ""
  echo "==> Success!  $PROJECT_DIR/JustOneBoss.dsk"
  echo "    Boot a System disk in Mini vMac, then drag JustOneBoss.dsk on to run it."
else
  echo "!! Build finished but JustOneBoss.dsk not found. Check output above."
  exit 1
fi
