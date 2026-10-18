#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"
"${CXX:-clang++}" -std=c++17 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all \
  "$ROOT/tests/gdi_glyph_test.cpp" -o "$ROOT/build/gdi-glyph-test"
"$ROOT/build/gdi-glyph-test"
