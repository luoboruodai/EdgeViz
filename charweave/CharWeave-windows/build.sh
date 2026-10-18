#!/bin/zsh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SDK="${AE_SDK_ROOT:?Set AE_SDK_ROOT to your licensed Adobe After Effects SDK}"
CXX="${MINGW_CXX:-x86_64-w64-mingw32-g++}"
WINDRES="${MINGW_WINDRES:-x86_64-w64-mingw32-windres}"
if [[ ! -d "$SDK/Headers" ]]; then echo "Missing AE SDK headers: $SDK/Headers" >&2; exit 2; fi
command -v "$CXX" >/dev/null || { echo "Missing $CXX" >&2; exit 2; }
command -v "$WINDRES" >/dev/null || { echo "Missing $WINDRES" >&2; exit 2; }
mkdir -p "$ROOT/build"
"$WINDRES" --target=pe-x86-64 "$ROOT/pipl/CharWeave.rc" -O coff -o "$ROOT/build/CharWeave-res.o"
"$CXX" -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables \
  -D_WINDOWS -DWIN_ENV=1 -DCHARWEAVE_UI_TRACE=0 -DCHARWEAVE_HOST_TRACE=0 \
  -I"$SDK/Headers" -I"$SDK/Headers/SP" -I"$SDK" -I"$SDK/Util" -I"$ROOT/src" \
  -c "$ROOT/src/CharWeaveWin.cpp" -o "$ROOT/build/CharWeaveWin.o"
"$CXX" -std=c++17 -O2 -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-asynchronous-unwind-tables \
  -D_WINDOWS -DWIN_ENV=1 -DCHARWEAVE_UI_TRACE=0 -DCHARWEAVE_HOST_TRACE=0 \
  -I"$SDK/Headers" -I"$SDK/Headers/SP" -I"$SDK" -I"$SDK/Util" -I"$ROOT/src" \
  -shared -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp \
  -Wl,--subsystem,windows -Wl,--export-all-symbols \
  -o "$ROOT/build/CharWeave.aex" "$ROOT/build/CharWeaveWin.o" "$ROOT/build/CharWeave-res.o" -lgdi32 -luser32
python3 "$ROOT/patch_private_crt.py" "$ROOT/build/CharWeave.aex"
file "$ROOT/build/CharWeave.aex"
shasum -a 256 "$ROOT/build/CharWeave.aex"
