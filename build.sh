#!/bin/bash
# Build EdgeViz.plugin (arm64 Mach-O bundle, ad-hoc signed).
# Run from any location; never delete a pre-existing build directory.
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
SDK="${AE_SDK_ROOT:-$DIR/SDK}"
if [ ! -d "$SDK/Headers" ]; then
  echo "missing Adobe AE SDK headers: set AE_SDK_ROOT=/path/to/AfterEffectsSDK" >&2
  exit 2
fi
OUT="$DIR/build/EdgeViz.plugin"

mkdir -p "$OUT/Contents/MacOS" "$OUT/Contents/Resources"

Rez -useDF -o "$OUT/Contents/Resources/EdgeViz.rsrc" \
  -I "$DIR/pipl" -I "$SDK/Headers" \
  "$DIR/pipl/EdgeVizOutlinePiPL.r"
# Do not ship stale resources from a prior EdgeViz Outline build.
python3 -c 'import pathlib, sys; pathlib.Path(sys.argv[1]).unlink(missing_ok=True)' \
  "$OUT/Contents/Resources/EdgeVizOutline.rsrc"
cp "$DIR/Info.plist" "$OUT/Contents/Info.plist"
printf 'eFKTFXTC' > "$OUT/Contents/PkgInfo"

clang++ -std=c++14 -O2 -arch arm64 -bundle \
  -I"$SDK/Headers" -I"$SDK/Headers/SP" -I"$SDK" -I"$SDK/Util" \
  -include "$SDK/Util/carbon_shims.h" \
  -o "$OUT/Contents/MacOS/EdgeViz" \
  "$DIR/src/EdgeVizOutline.cpp" \
  -framework CoreServices -framework Carbon -framework ApplicationServices

codesign -s - --force "$OUT"
echo "built: $OUT"
