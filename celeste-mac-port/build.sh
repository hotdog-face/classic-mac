#!/bin/bash
#
# build.sh - build Celeste Classic for the Classic Macintosh with Retro68.
# Reuses the Retro68 toolchain at ~/Retro68-build (same as the JOB port).
# Output: Celeste.dsk
#
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
RETRO68_BUILD="$HOME/Retro68-build"
TOOLCHAIN="$RETRO68_BUILD/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"

if [ ! -f "$TOOLCHAIN" ]; then
  echo "!! Retro68 toolchain not found at $TOOLCHAIN"
  exit 1
fi

echo "==> Compiling Celeste..."
BUILD_DIR="$PROJECT_DIR/build"
rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"
cmake .. -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -GNinja
cmake --build .

if [ -f "$BUILD_DIR/Celeste.dsk" ]; then
  cp "$BUILD_DIR/Celeste.dsk" "$PROJECT_DIR/Celeste.dsk"
  cp "$BUILD_DIR/Celeste.bin" "$PROJECT_DIR/Celeste.bin"
  echo ""
  echo "==> Success!  $PROJECT_DIR/Celeste.dsk"
  echo "    Boot a System disk in Mini vMac, then drag Celeste.dsk on to run it."
else
  echo "!! Build finished but Celeste.dsk not found. Check output above."
  exit 1
fi
