#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SDK="${AE_SDK_ROOT:?Set AE_SDK_ROOT to your licensed Adobe After Effects SDK}"
mkdir -p "$ROOT/build"
clang++ -std=c++17 -O1 -g -fobjc-arc -arch arm64 -Wno-pragma-pack \
  -I"$SDK/Headers" -I"$SDK/Headers/SP" -I"$SDK" -I"$SDK/Util" \
  -o "$ROOT/build/regression-test" "$ROOT/tests/regression.mm" \
  -framework AppKit -framework CoreServices -framework Carbon -framework ApplicationServices
"$ROOT/build/regression-test"
