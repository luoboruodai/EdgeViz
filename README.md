# EdgeViz After Effects Plugin

**English** | [简体中文](README.zh-CN.md)

**Release: v0.1.0** · **PiPL: `0x00008601`** · **Platforms: macOS arm64 + Windows x64**
**Updated: 2026-10-08**

EdgeViz is an Adobe After Effects effect plug-in for visualizing layer frames, text glyph outlines, shape paths, Bezier vertices and handles, mask silhouettes, motion paths, and nested precomp content.

The effect match name remains `com.edgeviz.outline` for project compatibility. The user-facing plug-in name and delivered file names are unified as **EdgeViz**.

## Release files

- `artifacts/macos-arm64/EdgeViz.plugin` — macOS arm64 plug-in bundle.
- `artifacts/macos-arm64/EdgeViz-v0.1.0-macOS-arm64.zip` — macOS package.
- `artifacts/windows-x64/EdgeViz.aex` — Windows x64 PE32+ plug-in.
- `artifacts/windows-x64/EdgeViz-v0.1.0-Windows-x64.zip` — Windows package.
- `SHA256SUMS.txt` — checksums for the release artifacts.

## v0.1.0 highlights

- Complete mixed CJK/Latin text vertices and Bezier-handle visualization.
- Visible fallback handles for zero-tangent text and parametric-shape corners.
- Motion frames grow from the current object plus the already-travelled path prefix.
- Motion paths are drawn below the moving object and clipped by its current occlusion area.
- Separate motion/path/occlusion data for multiple shapes on one shape layer.
- Recursive precomp drill-down for shapes, text, footage edges, and nested precomps.
- Safer invalid-mask handling and faster complex silhouette chaining.
- Skips key-vertex and motion sampling work when those overlays are disabled.
- Point styles: Circle, Square, Triangle, Diamond, Cross, and Custom Layer.
- Handle length, handle size, point size, color, and pixel-outline controls.

## Install on macOS

```bash
cp -R "EdgeViz.plugin" \
  "$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/"
```

Fully quit and restart After Effects after installing. To uninstall:

```bash
rm -rf "$HOME/Library/Application Support/Adobe/Common/Plug-ins/7.0/MediaCore/EdgeViz.plugin"
```

The delivered macOS bundle is ad-hoc signed for development/testing. Production distribution requires appropriate signing and notarization.

## Build from source

The public repository intentionally excludes the Adobe SDK. Set `AE_SDK_ROOT` to a licensed SDK checkout.

### macOS arm64

```bash
AE_SDK_ROOT=/path/to/AfterEffectsSDK ./build.sh
```

Requires Apple Clang, `Rez`, and `codesign`.

### Windows x64

```bash
AE_SDK_ROOT=/path/to/AfterEffectsSDK ./build_win.sh
```

Requires Python 3 and an x86_64 MinGW-w64 toolchain (`x86_64-w64-mingw32-g++`, `windres`, and `strip`). The script builds a PE32+ DLL, embeds a `PiPL` resource ID 16000 with the Windows x64 entry key `8664`, and exports `EffectMain`.

The Windows artifact has passed PE format, x64 architecture, export, PiPL, resource-ID, version-resource, and dependency checks. This macOS build host does not have Windows After Effects, so Windows AE loading and rendering must still be verified on a Windows machine with the target AE version. For commercial distribution, rebuild with a matching Visual Studio/Adobe SDK toolchain and sign the binary.

## Tests

```bash
python3 test/check_params.py
python3 test/check_release.py
```

The repository contains sanitized verification images only; no `.aep` projects, user media, caches, logs, SDK files, or credentials are included.

## Repository and release

Repository: `luoboruodai/EdgeViz`
Release tag: `v0.1.0`
