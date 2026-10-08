#!/usr/bin/env bash
# Cross-build EdgeViz.aex (Windows x64 PE plug-in) from macOS/Linux.
# AE runtime compatibility requires a real Windows host with each target AE version.
# Requires a licensed Adobe AE SDK and an x86_64 MinGW-w64 toolchain.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
SDK="${AE_SDK_ROOT:-$DIR/SDK}"
CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${WINDRES:-x86_64-w64-mingw32-windres}"
STRIP="${MINGW_STRIP:-x86_64-w64-mingw32-strip}"
OUT="$DIR/build-win"
AEX="$OUT/EdgeViz.aex"

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
python3 "$DIR/pipl/gen_win_pipl.py" "$OUT/EdgeViz.pipl"
cp "$DIR/pipl/EdgeVizOutlineWin.rc" "$OUT/EdgeViz.rc"
(
  cd "$OUT"
  "$WINDRES" -O coff -i EdgeViz.rc -o EdgeViz.pipl.res
)
python3 "$DIR/pipl/preserve_pipl_case.py" "$OUT/EdgeViz.pipl.res"

"$CXX" -shared -static -O2 -std=c++14 -fno-exceptions -fno-rtti \
  -D_WIN32 -DMSWindows -D_WINDOWS \
  -I"$SDK/Headers" -I"$SDK/Headers/Win" -I"$SDK/Headers/SP" \
  -I"$SDK" -I"$SDK/Util" -include "$SDK/Util/carbon_shims.h" \
  "$DIR/src/EdgeVizOutline.cpp" "$OUT/EdgeViz.pipl.res" \
  "$DIR/pipl/EdgeVizOutlineWin.def" \
  -Wl,--subsystem,windows -o "$AEX"
"$STRIP" --strip-unneeded "$AEX"

file "$AEX"
python3 "$DIR/test/check_windows_binary.py" "$AEX"
printf 'built: %s\n' "$AEX"
