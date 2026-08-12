#!/bin/bash
#
# build.sh - build Pip for Classic Macintosh with Retro68.
# Reuses the Retro68 toolchain already installed from the HelloClaude build.
# Output: Pip.dsk
#
set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
RETRO68_BUILD="$HOME/Retro68-build"
TOOLCHAIN="$RETRO68_BUILD/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake"

if [ ! -f "$TOOLCHAIN" ]; then
  echo "!! Retro68 toolchain not found at $TOOLCHAIN"
  echo "   Build HelloClaude first (its build.sh sets up the toolchain)."
  exit 1
fi

echo "==> Compiling Pip..."
BUILD_DIR="$PROJECT_DIR/build"
rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"
cmake .. -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -GNinja
cmake --build .

if [ -f "$BUILD_DIR/Pip.dsk" ]; then
  cp "$BUILD_DIR/Pip.dsk" "$PROJECT_DIR/Pip.dsk"
  echo ""
  echo "==> Success!  $PROJECT_DIR/Pip.dsk"
  echo "    Boot a System 7 disk in Mini vMac, then drag Pip.dsk on to run it."
else
  echo "!! Build finished but Pip.dsk not found. Check output above."
  exit 1
fi
