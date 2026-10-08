#!/bin/bash
# Build EdgeViz.plugin (universal arm64 + x86_64 Mach-O bundle, ad-hoc signed).
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

clang++ -std=c++14 -O2 -arch arm64 -arch x86_64 -bundle \
  -I"$SDK/Headers" -I"$SDK/Headers/SP" -I"$SDK" -I"$SDK/Util" \
  -include "$SDK/Util/carbon_shims.h" \
  -o "$OUT/Contents/MacOS/EdgeViz" \
  "$DIR/src/EdgeVizOutline.cpp" \
  -framework CoreServices -framework Carbon -framework ApplicationServices

# Verify both slices and both resource entry points before signing.
lipo "$OUT/Contents/MacOS/EdgeViz" -verify_arch arm64 x86_64
DeRez -useDF "$OUT/Contents/Resources/EdgeViz.rsrc" | grep -q '6D69 3634'  # mi64
DeRez -useDF "$OUT/Contents/Resources/EdgeViz.rsrc" | grep -q '6D61 3634'  # ma64
codesign -s - --force "$OUT"
codesign --verify --deep --strict "$OUT"
echo "built: $OUT"
