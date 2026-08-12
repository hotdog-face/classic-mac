#!/bin/bash
#
# build.sh - build One Foe for Classic Macintosh with Retro68.
# Reuses the Retro68 toolchain already installed from the HelloClaude build.
# Output: OneFoe.dsk
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

echo "==> Compiling One Foe..."
BUILD_DIR="$PROJECT_DIR/build"
rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"
cmake .. -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -GNinja
cmake --build .

if [ -f "$BUILD_DIR/OneFoe.dsk" ]; then
  cp "$BUILD_DIR/OneFoe.dsk" "$PROJECT_DIR/OneFoe.dsk"
  echo ""
  echo "==> Success!  $PROJECT_DIR/OneFoe.dsk"
  echo "    Boot a System disk in Mini vMac, then drag OneFoe.dsk on to run it."
else
  echo "!! Build finished but OneFoe.dsk not found. Check output above."
  exit 1
fi
