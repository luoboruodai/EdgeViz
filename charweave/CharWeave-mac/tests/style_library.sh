#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${AE_SDK_ROOT:?Set AE_SDK_ROOT to your licensed Adobe After Effects SDK}"
mkdir -p "$ROOT/build"
mkdir -p "$ROOT/tests/style-library"
clang++ -std=c++17 -O1 -g -fobjc-arc -arch arm64 -Wno-pragma-pack \
  -I"$SDK/Headers" -I"$SDK/Headers/SP" -I"$SDK" -I"$SDK/Util" \
  -o "$ROOT/build/style-library-test" "$ROOT/tests/style_library.mm" \
  -framework AppKit -framework CoreServices -framework Carbon -framework ApplicationServices
cd "$ROOT"
./build/style-library-test
