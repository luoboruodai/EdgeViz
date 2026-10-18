# Building and testing / 构建与测试

Source root: `charweave/` in the EdgeViz repository; run commands below from that directory.

Source version: **0.4.4 beta 2**, PF/PiPL `0x00022402`, macOS bundle build `40402`.

## Dependencies supplied by the builder

- A licensed Adobe After Effects SDK with `Headers`, `Headers/SP`, and `Util`.
- macOS: Command Line Tools / clang++, Python 3, `Rez`, `DeRez`, `lipo`, `codesign`; the SDK's `AE_General.r` template.
- Windows cross-build on macOS: MinGW-w64 x86-64 GCC/windres and Python 3.
- Standalone JSX model tests: Node.js. GDI model tests: a C++17 compiler; the model does not require Windows, the Adobe SDK, or AE.

Adobe SDK files and resource templates are deliberately not included. No dependency download or toolchain installation is performed by the build scripts.

```sh
export AE_SDK_ROOT="/path/to/AfterEffectsSDK"
export AE_REZ_ROOT="/path/to/SDK/resource/templates" # directory containing AE_General.r
cd CharWeave-mac
./build.sh
./tests/regression.sh
./tests/style_library.sh
node ./tests/style_script.test.cjs
```

`AE_REZ_ROOT` defaults to the SDK's Headers directory; set it separately if `AE_General.r` is located elsewhere. The Mac output is `CharWeave-mac/build/CharWeave.plugin` and is ad-hoc signed. The build preserves an earlier plugin under its candidate build directory; it never installs into AE.

```sh
cd CharWeave-windows
./build.sh
./tests/gdi_glyph_test.sh
```

The Windows output is `CharWeave-windows/build/CharWeave.aex`. The existing private-CRT import workaround aliases that import to `ucrtbase.dll`. Loader and AE compatibility must be checked on Windows; cross-compilation does not certify them.

## SDK-free checks

```sh
node CharWeave-mac/tests/style_script.test.cjs
clang++ -std=c++17 -O1 -g -fsanitize=address,undefined \
  -fno-sanitize-recover=all CharWeave-windows/tests/gdi_glyph_test.cpp -o /tmp/charweave-gdi-test
/tmp/charweave-gdi-test
python3 tools/check_public.py --self-test .
python3 tools/check_metadata.py
```

CI runs these SDK-free model and metadata checks; a tag-targeted workflow dispatch also downloads and checks the frozen install assets against `release-assets.json`; it does not claim AE/Windows integration or a signed host-tested build. SDK-dependent engine tests and local AE sampling are recorded separately in release notes.

## Release inputs

Build outputs are not committed. `tools/package_release.py --mac-bundle ... --windows-aex ... --output ...` copies an explicit map of approved binaries, installers, guides, notices and manifests. It never collects a project directory recursively. Run `python3 tools/check_public.py --self-test <source> <each ZIP>` before upload. Rebuilding and re-signing later can change binary identity; do not silently replace approved release attachments with different builds under the same name/tag.

本仓库只包含本项目源码和合成测试。SDK、真实工程、日志、截图、本机交接与账户配置不提交；安装包放在 Release 附件，不放进 Git 历史。
