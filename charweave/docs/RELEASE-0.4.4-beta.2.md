# CharWeave Native 0.4.4 beta 2 · 2026-10-18

独立标签 / Separate tag: **`charweave-v0.4.4-beta.2`** · PF/PiPL **`0x00022402`** · macOS build **`40402`**

This prerelease adds **CharWeave** to the `luoboruodai/EdgeViz` repository as the separate `charweave/` project. It does not replace EdgeViz; the stable EdgeViz v0.1.1 release remains unchanged and Latest.

本次将 CharWeave 作为独立项目加入 EdgeViz 仓库，不是 EdgeViz 升级。两个插件使用不同效果匹配名；请分别按对应指南安装。

## Installable downloads / 安装附件

- **`CharWeave-0.4.4-b2-macOS.zip`** — Apple Silicon + Intel universal plugin, ad-hoc signed. Local Apple Silicon / AE 26.5x89 startup and sample-frame export checked.
- **`CharWeave-0.4.4-b2-Windows-x64.zip`** — Windows x64 candidate targeting AE 2023–2026. Cross-built and statically/model-tested; **real Windows/AE startup and rendering are not verified**.
- **`SHA256SUMS.txt`** — hashes for the downloadable installation archives and separately labelled source snapshot.
- **`CharWeave-0.4.4-b2-source.zip`** — CharWeave source snapshot generated directly from this tag. Review/build only; not installable. GitHub's automatic Source code archives contain the full repository, not installable plugins.

安装前退出 AE / aerender，并把旧的 CharWeave / Probe 副本备份到 AE 扫描目录以外。一次只保留一个 CharWeave 注册项，不要把源代码 ZIP 当作安装包。

## Fixes / 修复

1. Release selected GDI fonts/bitmaps correctly on Windows, including failure and fallback paths.
2. Flush queued GDI drawing before reading DIB pixels.
3. Confirm real glyph coverage before choosing a fallback font.
4. Reject whitespace-only text/font input on both platforms while preserving visible BMP text mixed with whitespace.

No parameter IDs, TCW1 data format, render controls or existing Styles Manager workflow changed. Native Create/Manage buttons stay disabled; use the packaged standalone manager.

## Verification / 验证

- SDK-dependent ASan/UBSan engine and 8/16 bpc style regressions passed locally; JSX logic model passed.
- All 12 GDI model cases passed, including 1,000 repeated glyphs and failure paths. A GDI model is not a native Windows integration test.
- Both platforms built locally; final installable archive hashes and signed macOS/PE payload identity were checked. The Mac package payload matches the installed, sampled build.
- macOS AE 26.5x89 registered the installed effect as `0.4.4x2` and exported text, dispersion and layer-style frames without changing the saved test project.
- Repository CI checks metadata, privacy, release-tool rejection fixtures and SDK-free models. It does not install AE or certify Windows host compatibility.

## Limits / 限制

No Developer ID signature/notarization or Windows Authenticode signing is claimed. Intel runtime, other AE/OS versions, full text-dialog Undo/Redo, 32 bpc/HDR/GPU/MFR are not certified. Windows host acceptance is deferred, not passed. This is a beta/pre-release, not an all-version stable release.

[中文指南](INSTALL.zh-CN.md) · [English guide](INSTALL.en.md) · [Build instructions](BUILD.md) · [Changes](../CHANGELOG.md) · [Third-party notices](../THIRD_PARTY_NOTICES.md)
