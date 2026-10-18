#!/bin/zsh
# Build a universal Apple Silicon + Intel After Effects effect plug-in.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
SDK="${AE_SDK_ROOT:?Set AE_SDK_ROOT to your licensed Adobe After Effects SDK}"
if [[ ! -d "$SDK/Headers" ]]; then
  echo "Missing AE SDK headers: $SDK/Headers" >&2
  exit 2
fi

REZ_ROOT="${AE_REZ_ROOT:-$SDK/Headers}"
if [[ ! -f "$REZ_ROOT/AE_General.r" ]]; then
  echo "Set AE_REZ_ROOT to the SDK directory containing AE_General.r" >&2
  exit 2
fi

# Embed the UI helper from a single source of truth (also usable as a JSX file).
python3 - "$ROOT" <<'PYGEN'
import sys
from pathlib import Path
root=Path(sys.argv[1])
js=(root/'scripts/CharWeaveStyleLibrary.jsx').read_text()
assert ')CW_JS"' not in js
(root/'scripts/CharWeave Styles Manager.jsx').write_text(js+'\n$.global.CWStyleLibrary.showManager((typeof Panel!==\'undefined\' && this instanceof Panel)?this:null);\n')
(root/'src/StyleLibraryScript.inc').write_text('// Generated from scripts/CharWeaveStyleLibrary.jsx by build.sh.\nstatic const char kStyleLibraryScript[] = R"CW_JS('+js+')CW_JS";\n')
PYGEN

mkdir -p "$ROOT/build"
STAGE="$(mktemp -d "$ROOT/build/candidate.XXXXXX")"
OUT="$STAGE/CharWeave.plugin"
mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

Rez -useDF -o "$OUT/Contents/Resources/CharWeave.rsrc" \
  -I "$ROOT/pipl" -I "$SDK/Headers" -I "$REZ_ROOT" \
  "$ROOT/pipl/CharWeavePiPL.r"
cp "$ROOT/Info.plist" "$OUT/Contents/Info.plist"
cp "$ROOT/scripts/CharWeave Styles Manager.jsx" "$OUT/Contents/Resources/CharWeave Styles Manager.jsx"
printf 'eFKTFXTC' > "$OUT/Contents/PkgInfo"

for ARCH in arm64 x86_64; do
  MIN_OS=10.15
  [[ "$ARCH" == arm64 ]] && MIN_OS=11.0
  clang++ -std=c++17 -O2 -fobjc-arc -fvisibility=hidden -Wno-pragma-pack \
    -arch "$ARCH" -mmacosx-version-min="$MIN_OS" -DCHARWEAVE_UI_TRACE="${CHARWEAVE_UI_TRACE:-0}" -bundle \
    -I"$SDK/Headers" -I"$SDK/Headers/SP" -I"$SDK" -I"$SDK/Util" \
    -o "$STAGE/CharWeave.$ARCH" "$ROOT/src/CharWeave.mm" \
    -framework AppKit -framework CoreServices -framework Carbon -framework ApplicationServices
done
lipo -create "$STAGE/CharWeave.arm64" "$STAGE/CharWeave.x86_64" \
  -output "$OUT/Contents/MacOS/CharWeave"

lipo "$OUT/Contents/MacOS/CharWeave" -verify_arch arm64 x86_64
if command -v DeRez >/dev/null 2>&1; then
  DeRez -useDF "$OUT/Contents/Resources/CharWeave.rsrc" | grep -q '6D61 3634'
  DeRez -useDF "$OUT/Contents/Resources/CharWeave.rsrc" | grep -q '6D69 3634'
fi
codesign -s - --force "$OUT" >/dev/null
codesign --verify --deep --strict "$OUT"
file "$OUT/Contents/MacOS/CharWeave"
if [[ -d "$ROOT/build/CharWeave.plugin" ]]; then
  mv "$ROOT/build/CharWeave.plugin" "$STAGE/previous-CharWeave.plugin"
fi
mv "$OUT" "$ROOT/build/CharWeave.plugin"
echo "built: $ROOT/build/CharWeave.plugin"
echo "previous build and thin binaries retained in: $STAGE"
