#!/usr/bin/env bash
# Cross-build EdgeViz Outline.aex (Windows x64 PE plug-in) from macOS/Linux.
# Requires a licensed Adobe AE SDK and an x86_64 MinGW-w64 toolchain.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
SDK="${AE_SDK_ROOT:-$DIR/SDK}"
CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
STRIP="${MINGW_STRIP:-x86_64-w64-mingw32-strip}"
OUT="$DIR/build-win"
AEX="$OUT/EdgeViz Outline.aex"

if [[ ! -d "$SDK/Headers" ]]; then
  echo "missing Adobe AE SDK headers: set AE_SDK_ROOT=/path/to/AfterEffectsSDK" >&2
  exit 2
fi
for tool in "$CXX" "$WINDRES" "$STRIP"; do
  if ! command -v "$tool" >/dev/null 2>&1 && [[ ! -x "$tool" ]]; then
    echo "missing Windows tool: $tool" >&2
    exit 2
  fi
done

mkdir -p "$OUT"
python3 "$DIR/pipl/gen_win_pipl.py" "$OUT/EdgeVizOutline.pipl"
printf '%s\n' '16000 "PiPL" DISCARDABLE "EdgeVizOutline.pipl"' > "$OUT/EdgeVizOutline.rc"
(
  cd "$OUT"
  "$WINDRES" -O coff -i EdgeVizOutline.rc -o EdgeVizOutline.pipl.res
)

"$CXX" -shared -static -O2 -std=c++14 -fno-exceptions -fno-rtti \
  -D_WIN32 -DMSWindows -D_WINDOWS \
  -I"$SDK/Headers" -I"$SDK/Headers/Win" -I"$SDK/Headers/SP" \
  -I"$SDK" -I"$SDK/Util" -include "$SDK/Util/carbon_shims.h" \
  "$DIR/src/EdgeVizOutline.cpp" "$OUT/EdgeVizOutline.pipl.res" \
  "$DIR/pipl/EdgeVizOutlineWin.def" \
  -Wl,--subsystem,windows -o "$AEX"
"$STRIP" --strip-unneeded "$AEX"

file "$AEX"
"$STRIP" --strip-debug "$AEX" >/dev/null 2>&1 || true
printf 'built: %s\n' "$AEX"
